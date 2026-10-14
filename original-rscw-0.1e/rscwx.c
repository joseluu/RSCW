/* rscwx - auxiliary program for graphical display of DSP "signals"
 *
 * Copyright 2001,2011,2020 Pieter-Tjerk de Boer, pa3fwm@amsat.org
 *
 * Distributed on the conditions of the Gnu Public License, version 2.
 *
 * This program is part of the RSCW package; see
 *   https://www.pa3fwm.nl/software/rscw/
 */


#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>


/* for display of history: */
GtkWidget *gdraw=NULL;
GdkGC *gc = NULL;
GdkPixmap *pixmap=NULL;
long tnow=0;   /* timestamp of right-most column of plot, after it is next updated */
long tlastdraw=0; /* timestamp of right-most column of plot, as visible now on the screen */
long tnewest=0;  /* timestamp of most recent data */
long tf=1;     /* number of time-units per pixel */
gint xsize=500;
#define maxx (xsize-1)
gint ysize=300;
#define maxy (ysize-1)
int couldredraw=0;    /* flag: !=0 if something has changed and a redraw would be useful if nothing more urgent needs to be done */

/* for display of spectrum: */
GtkWidget *gdrawf=NULL;
GdkGC *gcf = NULL;
gint xsizef=500;
gint ysizef=150;
GdkPixmap *pixmapf=NULL;
GdkFont *fontf=NULL;

GdkColormap *colormap=NULL;
GdkColor CRed = { 0, 65535, 0, 0 };
GdkColor CGreen = { 0, 0, 65535, 0 };
GdkColor CCyan = { 0, 0, 65535, 65535 };
GdkColor CBlue = { 0, 0, 0, 65535 };
GdkColor CDarkgray = { 0, 16384, 16384, 16384 };
GdkColor CDarkergray = { 0, 12288, 12288, 12288 };
GdkColor CWhite = { 0, 65535, 65535, 65535 };
GdkColor CYellow = { 0, 65535, 65535, 0 };
GdkColor CBrown = { 0, 16384, 16384, 0 };


/*---------------------------- graph -------------------------------------------------------------------------*/
/* code relating to a ``graph'': samples from a function of time */

typedef struct {
   int y0;     /* height of baseline (i.e., the zero level) */
   float f;    /* scale: multiply data by this value to find number of pixels above y0 */
   ulong m;    /* mask for indexing the t and d arrays */
   long *t;   /* array of timestamps */
   int *d;     /* array of corresponding data values */
   ulong h;    /* index of head in the t and d arrays (they are used as circular buffers; h is the index of the next element to be written */
   ulong l;    /* index just beyond latest drawn sample in t and d arrays, for partial drawing */
   GdkColor *co;   /* colour */
   GdkColor *zero; /* if !=NULL, colour for zero-axis */
} Graph;

Graph *new_graph(ulong m)
{
   Graph *g;
   int i;
   assert((m&(m+1))==0);
   g=malloc(sizeof(Graph));
   if (!g) exit(1);
   g->t=malloc((m+1)*sizeof(*g->t));
   g->d=malloc((m+1)*sizeof(*g->d));
   if (!g->t || !g->d) exit(1);
   for (i=0;i<=m;i++) { g->d[m-i]=0; g->t[m-i]=-i*8192; }
   g->m=m;
   g->f=1;
   g->y0=0;
   g->h=0;
   g->l=0;
   g->co=g->zero=NULL;
   return g;
}

void draw_graph(Graph *g,GdkPixmap *pm)
{
   int i;
   gint x,y;
   gint xx,yy;
   ulong l;

   if (g->zero) {
      gdk_gc_set_foreground(gc,g->zero);
      gdk_draw_line(pm,gc,0,g->y0,maxx,g->y0);
   }

   i=g->h;
   do {
      i = (i-1)&g->m;
   } while (g->t[i]>tnow && i!=g->h);
   xx = maxx - (tnow-g->t[i])/tf;
   yy = g->y0 - g->d[i]*g->f;
   gdk_gc_set_foreground(gc,g->co);
   l=i;
   while (xx>=0 && i!=g->h) {
      i = (i-1)&g->m;
      x = maxx - (tnow-g->t[i])/tf;
      y = g->y0 - g->d[i]*g->f;
      gdk_draw_line(pm,gc,xx,yy,x,y);
      xx=x; yy=y;
   }
   g->l=l;
}

Graph *gg[10];
int ngg=0;

/*---------------------------- vgrid -------------------------------------------------------------------------*/
/* code relating to a ``vertical grid'': vertical lines, of possibly two colours */

typedef struct {
   ulong m;    /* mask for indexing the t and d arrays */
   long *t;   /* array of timestamps */
   int *d;     /* array of corresponding data values (only 0 and 1 allowed) */
   ulong h;    /* index of head in the t and d arrays (they are used as circular buffers; h is the index of the next element to be written */
   GdkColor *co0;   /* colour */
   GdkColor *co1;   /* colour */
} VGrid;

VGrid *new_vgrid(ulong m)
{
   VGrid *g;
   int i;
   assert((m&(m+1))==0);
   g=malloc(sizeof(VGrid));
   if (!g) exit(1);
   g->t=malloc((m+1)*sizeof(*g->t));
   g->d=malloc((m+1)*sizeof(*g->d));
   if (!g->t || !g->d) exit(1);
   for (i=0;i<=m;i++) { g->d[i]=-1; g->t[i]=0; }
   g->m=m;
   g->h=0;
   g->co0=g->co1=NULL;
   return g;
}

void draw_vgrid(VGrid *g,GdkPixmap *pm)
{
   int i;
   gint x;

   i=g->h;
   do {
      i = (i-1)&g->m;
   } while (g->t[i]>tnow && i!=g->h);
   x = maxx - (tnow-g->t[i])/tf;
   while (x>=0 && i!=g->h) {
      if (g->d[i]<0) break;
      if (g->d[i]==0) gdk_gc_set_foreground(gc,g->co0);
      else gdk_gc_set_foreground(gc,g->co1);
      gdk_draw_line(pm,gc,x,0,x,maxy);
      i = (i-1)&g->m;
      x = maxx - (tnow-g->t[i])/tf;
   }
}

VGrid *vg;


/*---------------------------- mgraph -------------------------------------------------------------------------*/
/* code relating to a ``morse graph'': dits and dahs, given by start time and duration */

typedef struct {
   ulong m;    /* mask for indexing the t and d arrays */
   long *t;   /* array of start timestamps */
   int *d;     /* array of durations */
   ulong h;    /* index of head in the t and d arrays (they are used as circular buffers; h is the index of the next element to be written */
   int y0;     /* height of the line */
   GdkColor *co;   /* colour */
} MGraph;

MGraph *new_mgraph(ulong m)
{
   MGraph *g;
   int i;
   assert((m&(m+1))==0);
   g=malloc(sizeof(MGraph));
   if (!g) exit(1);
   g->t=malloc((m+1)*sizeof(*g->t));
   g->d=malloc((m+1)*sizeof(*g->d));
   if (!g->t || !g->d) exit(1);
   for (i=0;i<=m;i++) { g->d[i]=-1; g->t[i]=0; }
   g->m=m;
   g->h=0;
   g->co=NULL;
   g->y0=0;
   return g;
}

void draw_mgraph(MGraph *g,GdkPixmap *pm)
{
   int i;
   gint x;

   i=g->h;
   do {
      i = (i-1)&g->m;
   } while (g->t[i]>tnow && i!=g->h);
   x = maxx - (tnow-g->t[i])/tf;
   gdk_gc_set_foreground(gc,g->co);
   while (x>=0 && i!=g->h) {
      if (g->d[i]<0) break;
      gdk_draw_line(pm,gc,x,g->y0,x+g->d[i]/tf,g->y0);
      i = (i-1)&g->m;
      x = maxx - (tnow-g->t[i])/tf;
   }
}

MGraph *mg;


/*---------------------------- gtext -------------------------------------------------------------------------*/
/* code relating to a line of text in the graph: the decoded characters */


typedef struct {
   ulong m;    /* mask for indexing the t and d arrays */
   long *t;   /* array of timestamps at which characters are to be written */
   char *d;    /* array of characters */
   ulong h;    /* index of head in the t and d arrays (they are used as circular buffers; h is the index of the next element to be written */
   int y0;     /* height of the line */
   GdkColor *co;   /* colour */
   GdkFont *font;  /* font */
} GText;

GText *new_gtext(ulong m)
{
   GText *g;
   int i;
   assert((m&(m+1))==0);
   g=malloc(sizeof(MGraph));
   if (!g) exit(1);
   g->t=malloc((m+1)*sizeof(*g->t));
   g->d=malloc((m+1)*sizeof(*g->d));
   if (!g->t || !g->d) exit(1);
   for (i=0;i<=m;i++) { g->d[i]=0; g->t[i]=0; }
   g->m=m;
   g->h=0;
   g->co=NULL;
   g->y0=0;
   g->font=gdk_font_load("-*-fixed-medium-r-*--12-*-*-*-*-*-iso8859-1");
   if (!g->font) exit(1);
   return g;
}

void draw_gtext(GText *g,GdkPixmap *pm)
{
   int i;
   gint x;

   i=g->h;
   do {
      i = (i-1)&g->m;
   } while (g->t[i]>tnow && i!=g->h);
   x = maxx - (tnow-g->t[i])/tf;
   gdk_gc_set_foreground(gc,g->co);
   while (x>=0 && i!=g->h) {
      if (g->d[i]==0) break;
      gdk_draw_text(pm,g->font,gc,x-gdk_char_width(g->font,g->d[i])/2,g->y0,g->d+i,1);
      i = (i-1)&g->m;
      x = maxx - (tnow-g->t[i])/tf;
   }
}

GText *tg;

/*---------------------------- spectrum ---------------------------------------------------------------------*/

int fft_n;       /* number of points in the FFT spectrum */
float fft_df;    /* frequency step per FFT point */
int fft_i;       /* counter of FFT points */
float fft_m;     /* frequency of the marker */
double *fft_a=NULL;    /* array of amplitudes, residing in shared memory and written by the main program */
float fft_fmin=0,fft_fmax=4000;  /* interval of frequencies to be displayed */
float fft_amax=0;    /* maximum amplitude ever seen */
float fft_graymin=0,fft_graymax=4000;    /* range of frequencies that get a black background, rest gets gray */

#define ysizefa (ysizef-13)
#define DBRANGE 40  /* dynamic range of display, in dB */

#define FtoX(f) ((int)(((f)-fft_fmin)/(fft_fmax-fft_fmin)*xsizef))

int AtoY(double a)
{
   a/=fft_amax;
   a = 10*log10(a);
   if (a<-DBRANGE) a=-DBRANGE;
   return (int)(-a*ysizefa/DBRANGE);
}

void fft_draw(void)
{
   int i;

   gdk_gc_set_foreground(gcf,&CDarkgray);
   gdk_draw_rectangle(pixmapf,gcf,1,0,0,xsizef,ysizef);
   gdk_draw_rectangle(pixmapf,gdrawf->style->black_gc,1,
      FtoX(fft_graymin),0,
      FtoX(fft_graymax)-FtoX(fft_graymin), ysizef);

   gdk_gc_set_foreground(gcf,&CRed);
   gdk_draw_line(pixmapf,gcf,
      FtoX(fft_m), 0, FtoX(fft_m), ysizef-1);

   for (i=0;i<fft_n;i++) if (fft_a[i]>fft_amax) fft_amax=fft_a[i];

   gdk_gc_set_foreground(gcf,&CGreen);
   {
      int x,xx=-1;
      int y,yy=-1;
      int ymin=0,ymax=0;
      for (i=0;i<fft_n;i++) {
         if (i*fft_df<fft_fmin) continue;
         if (i*fft_df>fft_fmax) break;
         x = FtoX(fft_df*i);
         y = AtoY(fft_a[i]);
         if (x!=xx) {
            if (xx>=0) {
               gdk_draw_line(pixmapf,gcf,xx,ymin,xx,ymax);
               gdk_draw_line(pixmapf,gcf,xx,yy,x,y);
            }
            xx=x;
            ymin=ymax=y;
         } else {
            if (y<ymin) ymin=y;
            if (y>ymax) ymax=y;
         }
         yy=y;
      }
      gdk_draw_line(pixmapf,gcf,xx,ymin,xx,ymax);
   }

   {
      float w = fft_fmax-fft_fmin;
      float dd[] = { 5000, 2000, 1000, 500, 200, 100, 50, 20, 10, 5, 2,   1, 0.5, 0.2, 0.1, 0 };
      float de[] = { 1000, 1000,  100, 100, 100,  10, 10, 10,  1, 1, 1, 0.1, 0.1, 0.1, 0.01 };
      int n;
      n=xsizef/100; if (n<2) n=2;
      i=0;
      while (n*dd[i]>w) i++;
      if (dd[i]!=0) {
         gdk_gc_set_foreground(gcf,&CWhite);
         n = 1+fft_fmin/de[i];
         while (n*de[i]<=fft_fmax) {
            int x;
            x = FtoX(n*de[i]);
            gdk_draw_point(pixmapf,gcf,x,ysizef-12);
            n++;
         }
         n = 1+fft_fmin/dd[i];
         while (n*dd[i]<=fft_fmax) {
            int x;
            char s[16];
            x = FtoX(n*dd[i]);
            gdk_draw_line(pixmapf,gcf,x,ysizef-10,x,ysizef-12);
            sprintf(s,"%g",n*dd[i]);
            gdk_draw_text(pixmapf,fontf,gcf,x-gdk_char_width(fontf,'0')*strlen(s)/2,ysizef-1,s,strlen(s));
            n++;
         }
      }
   }

   gdk_window_clear(gdrawf->window);
}

void read_stdin_f(char *s)
{
   int n;
   int shmid;
   switch (s[1]) {
      case '0':
         sscanf(s,"F0%i%g%i",&n,&fft_df,&shmid);
         fft_n=n;
         fft_i=0;
         if (fft_a==NULL) fft_a = shmat(shmid,NULL,SHM_RDONLY);
         break;
      case '1':
         sscanf(s,"F1%g%g",&fft_graymin,&fft_graymax);
         break;
      case '2':
         sscanf(s,"F2%g",&fft_m);
         fft_draw();
         break;
   }
}

void cmd_zoomin_f(void)
{
   if (fft_fmax-fft_fmin<=fft_df) return;
   fft_fmin = (fft_fmin+fft_m)/2;
   fft_fmax = (fft_fmax+fft_m)/2;
   fft_draw();
}

void cmd_zoomout_f(void)
{
   float o=fft_fmin;
   fft_fmin = 2*fft_fmin-fft_m;
   if (o>=0 && fft_fmin<0) fft_fmin=0;
   if (fft_fmin>fft_m) fft_fmin=fft_m;
   fft_fmax = 2*fft_fmax-fft_m;
   if (fft_fmax>fft_df*fft_n) fft_fmax=fft_df*fft_n;
   if (fft_fmax<fft_m) fft_fmax=fft_m;
   fft_draw();
}

void cmd_center_f(void)
{
   float w = fft_fmax-fft_fmin;
   fft_fmin = fft_m-w/2;
   fft_fmax = fft_m+w/2;
   fft_draw();
}


/*---------------------------- rest -------------------------------------------------------------------------*/


void redraw(void)
{
   int i;
   GdkPixmap *pm=pixmap;
   couldredraw=0;
   gdk_draw_rectangle(pm,gdraw->style->black_gc,1,0,0,xsize,ysize);
   draw_vgrid(vg,pm);
   for (i=0;i<ngg;i++) draw_graph(gg[i],pm);
   draw_mgraph(mg,pm);
   draw_gtext(tg,pm);
   gdk_window_clear(gdraw->window);
   tlastdraw=tnow;
}

gint idleredraw(gpointer data)
{
   if (couldredraw) redraw();
   return 0;
}

void planredraw(void)
{
   if (!couldredraw) {
      gtk_idle_add(idleredraw,NULL);
      couldredraw=1;
   }
}

guint input_id;

void read_stdin(gpointer data, gint source, GdkInputCondition condition)
{
   static char s[100];
   static char *p=s;
   int i=-1000;
   int l=0;
   while (read(0,p,1)==1) {
      i=*p;
      p++;
      if (p>=s+99) p=s+99;
      if (i=='\n') {
         long t;
         int d;
         int w;
         *p=0; 
/*
         fprintf(stderr,"> %s",s);
*/
         if (s[0]=='F') read_stdin_f(s);
         else if (sscanf(s,"%i%li%i",&w,&t,&d)==3) {
            if (w==-1) {
               vg->t[vg->h]=t;
               vg->d[vg->h]=d;
               vg->h = (vg->h+1)&vg->m;
            } else if (w==-2) {
               mg->t[mg->h]=t;
               mg->d[mg->h]=d;
               mg->h = (mg->h+1)&mg->m;
            } else if (w==-3) {
               tg->t[tg->h]=t;
               tg->d[tg->h]=d;
               tg->h = (tg->h+1)&tg->m;
            } else if (w>=0 && w<ngg) {
               Graph *g=gg[w];
               g->t[g->h]=t;
               g->d[g->h]=d;
               g->h = (g->h+1)&g->m;
            }
            if (t>tnewest) {
               if (tnow==tnewest) tnow=t;
               tnewest=t;
            }
         }
         p=s;
      }
      l++;
      if (l>1000) break;
   }
   if (i==-1000) {
      gdk_input_remove(input_id);
      return;
   }
   if (tnow==tnewest) {
      if (tnow-tlastdraw>=32*tf) redraw();
      else planredraw();
   }
}

void cmd_quit(void)
{
   exit(0);
}

void cmd_left(void)
{
   tnow-=xsize*tf/5;
   redraw();
}

void cmd_right(void)
{
   tnow+=xsize*tf/5;
   if (tnow>tnewest) tnow=tnewest;
   redraw();
}

void cmd_end(void)
{
   tnow=tnewest;
   redraw();
}

void cmd_zoomin(void)
{
   tf/=2;
   redraw();
}

void cmd_zoomout(void)
{
   tf*=2;
   redraw();
}


int running=0; 


static gint configure_event(GtkWidget *w, GdkEventConfigure *ev)
{
   if (pixmap) gdk_pixmap_unref(pixmap);
   xsize = w->allocation.width;
   ysize = w->allocation.height;
   pixmap = gdk_pixmap_new(w->window, xsize, ysize, -1);
   gdk_window_set_back_pixmap(w->window, pixmap,0);

   if (running) redraw();

   return TRUE;
}

static gint configure_event_f(GtkWidget *w, GdkEventConfigure *ev)
{
   if (pixmapf) gdk_pixmap_unref(pixmapf);

   xsizef = w->allocation.width;
   ysizef = w->allocation.height;
   pixmapf = gdk_pixmap_new(w->window, xsizef, ysizef, -1);

   gdk_window_set_back_pixmap(w->window, pixmapf,0);

   return TRUE;
}


int main(int argc,char **argv)
{
   GtkWidget *mainwin;
   GtkWidget *mainwinf;
   GtkWidget *w;
   GtkAccelGroup *acc;

   gtk_init(&argc,&argv);

   /* - - - - - - the history-window - - - - - - */

   mainwin=gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(mainwin),"RSCW history");
   gtk_window_set_policy(GTK_WINDOW(mainwin),TRUE,TRUE,TRUE);
   acc=gtk_accel_group_new();

   w=gtk_button_new_with_label("quit");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_quit), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_Q, 0,0);

   w=gtk_button_new_with_label("back");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_left), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_Left, 0,0);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_H, 0,0);

   w=gtk_button_new_with_label("fwd");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_right), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_Right, 0,0);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_L, 0,0);

   w=gtk_button_new_with_label("end");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_end), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_End, 0,0);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_K, 0,0);

   w=gtk_button_new_with_label("zoom in");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_zoomin), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_Z, 0,0);

   w=gtk_button_new_with_label("zoom out");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_zoomout), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_X, 0,0);

   gtk_window_add_accel_group(GTK_WINDOW(mainwin), acc);

   gdraw=gtk_drawing_area_new();
   gtk_drawing_area_size(GTK_DRAWING_AREA(gdraw),xsize,ysize);
   gtk_signal_connect(GTK_OBJECT(gdraw), "configure_event", GTK_SIGNAL_FUNC(configure_event), NULL);

   gtk_container_add(GTK_CONTAINER(mainwin), gdraw);
   gtk_widget_show(gdraw);
   gtk_widget_show(mainwin);

   colormap = gdk_colormap_new(gdk_visual_get_system(),0);
   if ( !gdk_colormap_alloc_color(colormap,&CRed,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CGreen,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CCyan,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CBlue,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CDarkgray,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CDarkergray,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CWhite,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CYellow,FALSE,TRUE)
      ||!gdk_colormap_alloc_color(colormap,&CBrown,FALSE,TRUE)
      ) printf("Failure allocating colours.\n");

   gc = gdk_gc_new(gdraw->window);

   /* - - - - - - the spectrum-window - - - - - - */

   mainwinf=gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(mainwinf),"RSCW spectrum");
   gtk_window_set_policy(GTK_WINDOW(mainwinf),TRUE,TRUE,TRUE);
   acc=gtk_accel_group_new();

   w=gtk_button_new_with_label("quit");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_quit), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_Q, 0,0);

   w=gtk_button_new_with_label("zoom in");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_zoomin_f), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_Z, 0,0);

   w=gtk_button_new_with_label("zoom out");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_zoomout_f), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_X, 0,0);

   w=gtk_button_new_with_label("center");
   gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(cmd_center_f), (gpointer)NULL);
   gtk_widget_add_accelerator(w, "clicked", acc, GDK_C, 0,0);

   gtk_window_add_accel_group(GTK_WINDOW(mainwinf), acc);

   gdrawf=gtk_drawing_area_new();
   gtk_drawing_area_size(GTK_DRAWING_AREA(gdrawf),xsizef,ysizef);
   gtk_signal_connect(GTK_OBJECT(gdrawf), "configure_event", GTK_SIGNAL_FUNC(configure_event_f), NULL);

   gtk_container_add(GTK_CONTAINER(mainwinf), gdrawf);
   gtk_widget_show(gdrawf);
   gtk_widget_show(mainwinf);

   gcf = gdk_gc_new(gdrawf->window);
   fontf=gdk_font_load("-*-fixed-medium-r-*--12-*-*-*-*-*-iso8859-1");

   /* - - - - - - rest - - - - - - */

   {
      Graph *g;

      g=new_graph(1048575); g->y0=150; g->f=0.005; g->h=0; gg[ngg++]=g; g->co = &CGreen;
      g=new_graph(65535); g->y0=150; g->f=0.005; g->h=0; gg[ngg++]=g; g->co = &CRed;
      g=new_graph(65535); g->y0=225; g->f=0.005; g->h=0; gg[ngg++]=g; g->co = &CCyan;  g->zero=&CBlue;
      g=new_graph(1048575); g->y0=50; g->f=0.005; g->h=0; gg[ngg++]=g; g->co = &CYellow;  g->zero=&CBrown;

/* extra graph for testing: */
      g=new_graph(1048575); g->y0=450; g->f=0.005; g->h=0; gg[ngg++]=g; g->co = &CGreen;

      vg=new_vgrid(65535); vg->co0 = &CDarkergray; vg->co1 = &CDarkgray; 
      tg=new_gtext(65535); tg->co = &CWhite; tg->y0=298-tg->font->descent;
      mg=new_mgraph(65535); mg->co = &CWhite; mg->y0=tg->y0-tg->font->ascent-2;

      tnow=100; tf=64;
      tnewest=tnow;
   }

   {
      int nonblocking=1;
      ioctl(0,FIONBIO,&nonblocking);
   }
   input_id = gdk_input_add(0, GDK_INPUT_READ, read_stdin, NULL);
   redraw();
   running=1;
   gtk_main();

   return 0;
}
