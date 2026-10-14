/* rscw - morse decoder for soundcard\n"
 *
 * Copyright 2001,2011,2020 Pieter-Tjerk de Boer, pa3fwm@amsat.org
 *
 * Distributed on the conditions of the Gnu Public License, version 2.
 *
 * This program is part of the RSCW package; see
 *   https://www.pa3fwm.nl/software/rscw/
 */

#define VERSION "0.1d"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <fftw3.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>


float WPM=22;           /* morse code speed in words per minute */

#define SAMP (8000.0)       /* sample rate of raw audio, in samples/second */
#define DOWN 8              /* the downsampling factor */
#define SAMP2 (SAMP/DOWN)   /* sample rate after downsampling, in samples/second */
#define DOTSEC (1.2/WPM)    /* duration of one dot in seconds */
#define DOTLEN (SAMP2*1.2/WPM)    /* duration of one dot in samples (after downsampling) */

#define FFTBLOCKSIZE 8192     /* blocksize used for the FFT that is used for searching the carrier */

#define TRACKTOL 20           /* tolerance (in Hz) for tracking */


/* Below, a set of circular buffers are defined.
   Looking back, this was perhaps not the most suitable way to do this;
   in particular, the calculation of which timestamp corresponds to which
   element of the array, is a bit messy. Perhaps redesign this in a future
   version...
*/
/* the buffer for the raw audio: */
#define BRMASK 131071      /* mask for indexing */
float br[BRMASK+1];       /* the buffer itself */
uint brw;                 /* index for writing */
int brt;                 /* time (= sequence number of sample) of last sample written */

/* the buffer for the measured carrier frequencies: */
#define BCMASK 31         /* mask for indexing */
float bc[BCMASK+1];       /* the buffer itself */
uint bcw;                 /* index for writing */
int bct;                 /* time (= sequence number of sample) of last sample written */

/* the buffer for the downconverted audio, with I(n-phase) and Q(uadrature) components: */
#define BDMASK 131071      /* mask for indexing */
float bdi[BDMASK+1];      /* the buffer itself, I-part */
float bdq[BDMASK+1];      /* the buffer itself, Q-part */
uint bdw;                 /* index for writing */
int bdt;                  /* time (= sequence number of sample) of last sample written */
float bdphase;            /* phase of the carrier for the next sample */

/* the buffer for the low-pass filtered, downsampled, downconverted audio; still with I and Q components */
#define BSMASK 65535      /* mask for indexing */
float bsi[BSMASK+1];      /* the buffer itself, I-part */
float bsq[BSMASK+1];      /* the buffer itself, Q-part */
uint bsw;                 /* index for writing */
int bst;                  /* time (= sequence number of sample) of last sample written */

/* the buffer for the signal after calculating a moving average over 1 dottime, and calculating the magnitude from I and Q */
#define BFMASK 65535      /* mask for indexing */
float bf[BFMASK+1];       /* the buffer itself */
uint bfw;                 /* index for writing */
int bft;                  /* time (= sequence number of sample) of last sample written */

/* the buffer for the "morse phase" */
#define BMMASK 65535      /* mask for indexing */
float bm[BMMASK+1];       /* the buffer itself */
uint bmw;                 /* index for writing */
int bmt;                  /* time (= sequence number of sample) of last sample written */

/* the buffer for the bit-samples (i.e., one sample per time-interval corresponding to one dot) */
/* required buffer size: enough for the threshold estimation, i.e., THRESTLEN bits; but also long enough to store enough data for the next step */
#define BYMASK 255      /* mask for indexing */
float by[BYMASK+1];       /* the buffer itself */
int bY[BYMASK+1];         /* an extra buffer containing the corresponding timestamps */
uint byw;                 /* index for writing */
uint byr;                 /* index for reading */
int byphase;              /* 0 or 1: "phase" of the bit last written */

/* the buffer for those bit samples after the corresponding threshold-value has been subtracted */
#define BTMASK 63      /* mask for indexing */
float bt[BTMASK+1];       /* the buffer itself */
uint btw;                 /* index for writing */
uint btr;                 /* index for reading */
int bT[BTMASK+1];       /* the corresponding timestamps */
int btphase;              /* 0 or 1: "phase" of the bit last written */


/* for communication with the external graphical display program: */
FILE *xpipe=NULL;         /* file pointer for the pipe to the external program */
double *fftresult=NULL;   /* array into which the result of the FFT is written, for spectrum display */
int fftresult_shmid;      /* shared-memory id of the fftresult array */



                     /* modes for determining the carrier frequency: */
enum {  STRONGEST,      /* always take the strongest signal between Fmin and Fmax */
        TRACK           /* try to track a (possibly drifting) signal */
     };
int fmode=STRONGEST;

double Fmin=1, Fmax=SAMP/2-1;    /* absolute boundaries for modes 'strongest' and 'track' */
static double ffmin=1;            /* boundaries for the frequency range; can move in mode 'track' */
static double ffmax=SAMP/2-1;

double search_carrier(int bri, int num)
{
   fftw_plan plan;
   double *in;
   double *out;
   int i,iargmax=0;
   int imin,imax;
   double a,max;
   double df=SAMP/num;
   double avg;
   double f;
   int dcd;

   /* prepare the FFT library */
   in = fftw_malloc(num*sizeof(double));
   out = fftw_malloc(num*sizeof(double));
   plan = fftw_plan_r2r_1d( num, in, out, FFTW_R2HC, FFTW_ESTIMATE );

   /* apply a simple triangular windowing function */
   for (i=0; i<num/2; i++) in[i]=br[(bri+i)&BRMASK]*((i+1)/(num/2.0));
   for (i=num/2; i<num; i++) in[i]=br[(bri+i)&BRMASK]*((num-i)/(num/2.0));
   /* perform the fft */
   fftw_execute( plan );
   /* calculate the magnitude at each frequency */
   for (i=1; i<num/2-1; i++) {
      a = out[i]*out[i]+out[num-i]*out[num-i];
      fftresult[i]=a;
   }
   if (xpipe) fprintf(xpipe,"F0 %i %g %i\nF1 %g %g\n", num/2, 1.0/num*SAMP, fftresult_shmid, ffmin, ffmax);

   /* search the peak within the allowed range */
   imax=-1; max=0;
   avg=0;
   imin=ffmin/df; imax=ffmax/df;
   for (i=imin; i<=imax; i++) {
      a = sqrt(fftresult[i]);
      if (a>max) { iargmax=i; max=a; }
      avg +=a;
   }
   avg /= imax-imin+1;
   f = iargmax*df;
   if (fmode == STRONGEST) {
      /* accept the peak unconditionally */
      dcd=1;
   } else if (fmode == TRACK) {
      /* accept the peak (dcd = data carrier detect) if it is at least 3 dB above the average */
      dcd = (max>=avg*2);
      /* decide what range of frequencies to allow next time: */
      if (dcd) {
         /* if the carrier was found, that range can be made narrow */
         f = iargmax*df;
         ffmin = f-TRACKTOL;
         ffmax = f+TRACKTOL;
      } else {
         /* otherwise, widen it, to allow for a drifting carrier */
         ffmin = ffmin - TRACKTOL;
         if (ffmin<0) ffmin=0;
         ffmax = ffmax + TRACKTOL;
         if (ffmax>SAMP/2) ffmax=SAMP/2;
         /* and tell the caller that we didn't find the carrier */
         f=-1;
      }
   }

   if (xpipe) fprintf(xpipe,"F2 %g\n", f);

   fftw_destroy_plan(plan);
   fftw_free(in);
   fftw_free(out);

   return f;
}





int floatcmp(const void *a, const void *b)
{
   if (*(float*)a < *(float*)b) return -1;
   if (*(float*)a > *(float*)b) return 1;
   return 0;
}

float findmedian(float ff[],uint i0, uint mask, int nff)
{
   float median;
   float f[10];
   int i;

   assert(nff<=sizeof(f)/sizeof(f[0]));
   for (i=0;i<nff;i++) f[i]=ff[(i+i0)&mask];

   /* first, sort the array */
   qsort(f,nff,sizeof(f[0]),floatcmp);

   /* next, find the median */
   if (nff&0x01) median=f[nff/2];
   else median= (f[nff/2]+f[nff/2-1])/2;

   return median;
}




float estimate_threshold2a(uint i,int n)
{
   float a[500];
   float b[500];
   float t;
   float tmax,tmin,t0;

   assert (n<=500);
   {
      int j,k;
      k=0;
      for (j=0;j<n;j++) a[k++] = by[(i-j)&BYMASK];
      for (j=n/4;j<3*n/4;j++) a[k++] = by[(i-j)&BYMASK];
      n=k;
      qsort(a,n,sizeof(float),floatcmp);
   }

   b[0]=a[0];
   for (i=1;i<n;i++) b[i]=b[i-1]+a[i];
   i=n/2;
   t= ( (b[i-1]/i) + (b[n-1]-b[i-1])/(n-i) )/2;


   if (t<a[i-1]) {
      do {
         i--;
         t= ( (b[i-1]/i) + (b[n-1]-b[i-1])/(n-i) )/2;
      } while (t<a[i-1] && i>1);
   } else if (t>a[i]) {
      do {
         i++;
         t= ( (b[i-1]/i) + (b[n-1]-b[i-1])/(n-i) )/2;
      } while (t>a[i] && i<n);
   }   
   t0=t;
   do {
      i--;
      tmin=t;
      t= ( (b[i-1]/i) + (b[n-1]-b[i-1])/(n-i) )/2;
   } while (t>=a[i-1] && i>1);
   t=t0;
   do {
      i++;
      tmax=t;
      t= ( (b[i-1]/i) + (b[n-1]-b[i-1])/(n-i) )/2;
   } while (t<=a[i] && i<n);

   return (tmin+tmax)/2;
}




/************************************************************************************************************************/


/* for historical reasons, a few #defines */
#define bb bt
#define bB bT
#define BBMASK BTMASK
#define bbw btw
#define bbr btr

/* dibits: 'a' = 00, 'b' = 10, 'c' = 11; 't' = 00 as the terminating dibit of a character  */

typedef struct {
   char c;     /* the (ASCII) code of the character */
   char s[12]; /* the actual dibits */
} Morsechar;

Morsechar alphabet[] =
{
   {' ', "at"},        /* standard space */
   {' ', "aat"},       /* extra long space as used by RS12 telemetry */
   {'E', "bt"},
   {'I', "bbt"},
   {'T', "cbt"},
   {'S', "bbbt"},
   {'N', "cbbt"},
   {'A', "bcbt"},
   {'H', "bbbbt"},
   {'D', "cbbbt"},
   {'R', "bcbbt"},
   {'U', "bbcbt"},
   {'M', "cbcbt"},
   {'5', "bbbbbt"},
   {'B', "cbbbbt"},
   {'L', "bcbbbt"},
   {'F', "bbcbbt"},
   {'V', "bbbcbt"},
   {'G', "cbcbbt"},
   {'K', "cbbcbt"},
   {'W', "bcbcbt"},
   {'6', "cbbbbbt"},
   {'4', "bbbbcbt"},
   {'Z', "cbcbbbt"},
   {'C', "cbbcbbt"},
   {'X', "cbbbcbt"},
   {'P', "bcbcbbt"},
   {'O', "cbcbcbt"},
   {'7', "cbcbbbbt"},
   {'/', "cbbbcbbt"},
   {'=', "cbbbbcbt"},
   {'3', "bbbcbcbt"},
   {'Q', "cbcbbcbt"},
   {'Y', "cbbcbcbt"},
   {'J', "bcbcbcbt"},
   {'?', "bbcbcbbbt"},
   {'8', "cbcbcbbbt"},
   {'2', "bbcbcbcbt"},
   {'9', "cbcbcbcbbt"},
   {'1', "bcbcbcbcbt"},
   {'0', "cbcbcbcbcbt"},
};
#define Nmorsechar (sizeof(alphabet)/sizeof(Morsechar))


/* Below, we define a tree structure for the morse code table.
   The struct 'Branch' describes one point in the tree, which can be:
   - an intermediate branch, with links to successor branches for each possible next dibit.
   - a final branch (a "leaf"), with no further successor links, but with an ASCII character.
   Note: in principle, a node can be both of the above. The only example of this occurs if
   both types of space have been enabled (i.e., both the regular 7-dot space, and the 9-dot
   space used by RS12).
*/

typedef struct _Branch {
   struct _Branch *next_a;     /* pointer to next branch, after dibit 'a' */
   struct _Branch *next_b;     /* pointer to next branch, after dibit 'b' */
   struct _Branch *next_c;     /* pointer to next branch, after dibit 'c' */
   struct _Branch *up;         /* pointer to previous (i.e., closer to the root) branch; only used while constructing the tree */
   int c;                      /* the ASCII code of the character in a final branch, 0 otherwise */
   char s[16];                 /* a string representation of the dibits leading to this point; only useful for debugging */
   long long tails;            /* a 64 bit long integer, in which each bit corresponds to one possible sequence of dibits,
                                  taken from the array 'taillist' (see below). Each bit is 1 iff the corresponding tail is
                                  possible starting in this branch. This is used in the function prune(). */
} Branch;

Branch *tree;                  /* pointer to the root of the tree */

char taillist[64][12];         /* list of possible tails, i.e., sequences of dibits that lead to a valid morse character starting from one or more points in the tree */
int ntails=0;                  /* the number of entries in taillist[] */

void maketaillist(void)        /* fill the taillist[] array */
{
   int i;
   for (i=0;i<Nmorsechar;i++) {
      int j;
      Morsechar *a=alphabet+i;
      for (j=0;j<strlen(a->s);j++) {
         int k;
         for (k=0;k<ntails;k++) {
            if (strcmp(a->s+j,taillist[k])==0) break;
         }
         if (k==ntails) {
            strcpy(taillist[ntails],a->s+j);
            ntails++;
            assert(ntails<64);
         }
      }
   }
}


void maketree(void)        /* build the morse code tree */
{
   int i;
   maketaillist();
   tree=malloc(sizeof(Branch));
   tree->next_a = tree->next_b = tree->next_c = NULL;
   tree->c = 0;
   tree->up = NULL;
   tree->tails = 0;
   tree->s[0] = '\0';
   for (i=0;i<Nmorsechar;i++) {
      char *p;
      Branch *b=tree;
      p=alphabet[i].s;
      while (*p) {
         Branch **np=NULL;
         switch (*p) {
            case 'a': case 't': np=&b->next_a; break;
            case 'b': np=&b->next_b; break;
            case 'c': np=&b->next_c; break;
         }
         assert(np!=NULL);
         if (*np==NULL) {
            char *cp;
            Branch *t;
            t=malloc(sizeof(Branch));
            t->next_a = t->next_b = t->next_c = NULL;
            t->c = 0;
            t->up = b;
            t->tails = 0;
            cp=stpcpy(t->s,b->s); *cp++=*p; *cp='\0';
            *np=t;
         }
         b = *np;
         p++;
      }
      assert(b->c==0);
      b->c = alphabet[i].c;
      while (b->up) {
         int k;
         p--;
         for (k=0;k<ntails;k++) {
            if (strcmp(p,taillist[k])==0) break;
         }
         assert(k!=ntails);
         b->up->tails |= (1ll<<k);
         b=b->up;
      }
   }
}


void decision(Branch *b,int j)
{
   printf("%c",b->c);  fflush(stdout);
#ifdef VERBOSE_CORRELATION
   fprintf(stderr,"-----------------------------------------\n>> Decision: %c  %s\n",b->c,b->s);
#endif
   if (xpipe) {
      int t1,t2,tmin,tmax;
      char *p;
      p=b->s;
      tmin=bB[j];
      tmax=tmin;
      while (*p!='t') {
         switch (*p) {
            case 'b':
               t1 = bB[j];
               t2 = bB[(j+1)&BBMASK];
               fprintf(xpipe,"-2 %i %i\n",t1,t2-t1);
               tmax = t2;
               break;
            case 'c':
               t1 = bB[j];
               t2 = bB[(j+3)&BBMASK];
               fprintf(xpipe,"-2 %i %i\n",t1,t2-t1);
               tmax = t2;
               p++;
               assert(*p=='b');
               j=(j+2)&BBMASK;
               break;
         }
         p++;
         j=(j+2)&BBMASK;
      }
      if (b->c!=' ') fprintf(xpipe,"-3 %i %i\n",(tmax+tmin)/2,b->c);
      fflush(xpipe);
   }
}



#define Nbranch 20
typedef struct _msg {
   double cor;             /* cross-correlation of this message with received signal */
   Branch *(b[Nbranch]);   /* list of pointers to branches visited in the morse code tree */
   int ib;                 /* index of current branch in b[] */
   /* Note: b[ib] points to a non-final branch (the one we're still completing), while b[0]...b[ib-1] point to final branches */
   struct _msg *next;      /* pointer to next message in chain */
} Msg;


#ifdef VERBOSE_CORRELATION
char *printmsg(Msg *m)     /* for debugging: decode a Msg into a string of character representations of dibits */
{
   static char s[Nbranch*12];
   char *p;
   int i;
   p=s;
   for (i=0;i<=m->ib;i++) p=stpcpy(p,m->b[i]->s);
   return s;
}
#endif

Msg *prune(Msg *m0) {      
/* Remove unnecessary messages from the list.
   A message is unnecessary, if for every possible continuation of the message,
   there exists another message that can be continued in the same way, and already
   now has a higher cross-correlation.
*/
   Msg *m,*m1=NULL;
   while (m0) {
      int needed=1;
      m=m0;
      m0=m0->next;
      if (m->b[m->ib]!=tree) {
         long long tails=m->b[m->ib]->tails;
         Msg *n;
         int phase=0;
         needed=0;
         n=m0;
         if (n==0) { n=m1; phase=1; }
         while (n) {
            if (n->cor>m->cor) {
               tails &= ~(n->b[n->ib]->tails);
            }
            n=n->next;
            if (n==0 && phase==0) {
               n=m1;
               phase=1;
            }
         }
         if (tails!=0) { 
            needed=1; 
         }
      }
      if (needed) {
         m->next=m1;
         m1=m;
#ifdef VERBOSE_CORRELATION
         fprintf(stderr,"->   %40s  %12g\n",printmsg(m),m->cor);
#endif
      } else {
#ifdef VERBOSE_CORRELATION
         fprintf(stderr,"P>   %40s  %12g\n",printmsg(m),m->cor);
#endif
         free(m);
      }
   }
   return m1;
}

Msg *msgs;
uint bbr_oldest;      /* value of bbr of oldest dibit about which we still have to make a decision */


void decide(int phase)
{
   int nb;

   phase ^= (bbw-1-bbr)&0x01;
   if (phase) { bbr = (bbr+1)&BBMASK; phase^=1; bbr_oldest++; }     /* TODO: clean this up; it is only used the first time decide() is called */

   nb = (bbw-bbr)&BBMASK;     /* number of available received bits */

   if (msgs==NULL) {
      /* initialize the msgs-array */
      msgs=malloc(sizeof(Msg));
      msgs->cor=0;
      msgs->b[0]=tree;
      msgs->ib=0;
      msgs->next=NULL;
   }

   /* main loop: process the bits
   */
   while (nb>=2) {
      Msg *newmsgs;
      Msg *msg_t;
      double cor_a,cor_b,cor_c;
      int max_ib;

      /* calculate the correlation between the received group-of-two-bits and the dibit 'a', 'b' and 'c' (note: 't' is same as 'a') */
      cor_a = - bb[(bbr+0)&BBMASK] - bb[(bbr+1)&BBMASK];
      cor_b = + bb[(bbr+0)&BBMASK] - bb[(bbr+1)&BBMASK];
      cor_c = + bb[(bbr+0)&BBMASK] + bb[(bbr+1)&BBMASK];
#ifdef VERBOSE_CORRELATION
      fprintf(stderr,"-------------------------------------------------------------- %10g %10g -----------\n",bb[(bbr+0)&BBMASK],bb[(bbr+1)&BBMASK]);
#endif
      bbr = (bbr+2)&BBMASK;
      nb-=2;


      /* next, run over all existing messages, try to extend them, and collect the possible extensions */
      newmsgs=NULL;
      msg_t=NULL;
      max_ib=0;
      while (msgs) {
         Msg *p;

         if (msgs->b[msgs->ib]->next_a) {
            /* extend with 'a' (or 't', if it's the last dibit of a morse character) */
            Branch *b;
            b = msgs->b[msgs->ib]->next_a;
            if (b->c!=0) {
               /* it's a leaf node in the tree, i.e., a morse character has been completed */
               /* we only need to keep it if it has a higher cross-correlation than the best such message up to now. */
               p=malloc(sizeof(Msg)); memcpy(p,msgs,sizeof(Msg));
               p->cor+=cor_a; 
               p->b[p->ib]=p->b[p->ib]->next_a;
#ifdef VERBOSE_CORRELATION
               fprintf(stderr,"T>   %-40s  %12g\n",printmsg(p),p->cor);
#endif
               if (msg_t==NULL || p->cor > msg_t->cor) {
                  p->ib++;
                  if (p->ib>max_ib) max_ib=p->ib;
                  assert(p->ib < Nbranch);
                  p->b[p->ib]=tree;
                  p->next=NULL;
                  msg_t=p;
               } else free(p);
            }
            if (b->next_a || b->next_b || b->next_c) {
               /* it's a normal, non-leaf, node in the tree */
               p=malloc(sizeof(Msg)); memcpy(p,msgs,sizeof(Msg));
               p->cor+=cor_a; 
               p->b[p->ib]=p->b[p->ib]->next_a;
               p->next=newmsgs;
               newmsgs=p;
            }
         }

         if (msgs->b[msgs->ib]->next_b) {
            /* extend with 'b' */
            p=malloc(sizeof(Msg)); memcpy(p,msgs,sizeof(Msg));
            p->next=newmsgs;
            p->cor+=cor_b; 
            p->b[p->ib]=p->b[p->ib]->next_b;
            newmsgs=p;
         }

         if (msgs->b[msgs->ib]->next_c) {
            /* extend with 'c' */
            p=malloc(sizeof(Msg)); memcpy(p,msgs,sizeof(Msg));
            p->next=newmsgs;
            p->cor+=cor_c; 
            p->b[p->ib]=p->b[p->ib]->next_c;
            newmsgs=p;
         }

         /* throw away the msg we've just tried to extend, prepare for the next */
         p=msgs;
         msgs=msgs->next;
         free(p);
      }
      if (msg_t) {
         /* if any extension ending in 't' was found, it is now joined to the rest */
         msg_t->next=newmsgs;
         newmsgs=msg_t;
      }

      /* Remove all messages of which we already can be sure that they will never have the highest cross-correlation. */
      /* Note: this step is optional, but speeds up the algorithm. It may be replaced by  msgs=newmsgs; . */
      msgs=prune(newmsgs);

      /* Handle the possible growth of too long msgs (i.e., msgs with too long a branches list; this list can indeed grow without bounds) */
      if (max_ib>Nbranch-1) {
         Msg *p;
         p = msgs;
         /* search max_ib anew, since the msg with the too large ib might have been pruned */
         while (p) {
            if (p->ib>max_ib) max_ib=p->ib;
            p=p->next;
         }
         if (max_ib>=Nbranch-1) {
            /* Still too long? Then we retain the msg with the highest correlation, and all msgs with the same initial branch.
               Then the next step will decide that that step must be the best, and shorten the branches lists accordingly.
            */
            Msg *mp;
            double max;
            Branch *bb;
            max=msgs->cor;  mp=msgs;
            p=msgs->next;
            while (p) {
               if (p->cor > max) { max=p->cor; mp=p; }
               p=p->next;
            }
            bb=mp->b[0];
            mp=NULL;
            while (msgs) {
               if (msgs->b[0]!=bb) {
                  p=msgs;
                  msgs=msgs->next;
                  free(p);
               } else {
                  p=msgs;
                  msgs=msgs->next;
                  p->next=mp;
                  mp=p;
               }
            }
            msgs=mp;
         }
      }

      /* Finally, check whether all messages have an identical initial branch.
         If that's the case, then that initial segment is apparently the most likely morse character:
         the decoder has made a decision.
      */
      if (msgs->ib>0) {
         Msg *p;
         p=msgs->next;
         while (p) {
            if (msgs->b[0]!=p->b[0]) break;
            p=p->next;
         }
         if (p==NULL) {
            double dcor;
            /* present the decision to the user */
            decision(msgs->b[0],bbr_oldest);
            bbr_oldest = ( bbr_oldest + 2*strlen(msgs->b[0]->s) ) & BBMASK;
            dcor=msgs->cor;
            p=msgs;
            /* remove the initial segment from all messages */
            while (p) {
               p->cor-=dcor;  /* subtract a constant from the correlation coefficient, so those won't blow up to cause unnecessary numerical inaccuracies */
               memmove(p->b,p->b+1,p->ib*sizeof(p->b[0])); p->ib--;
               p=p->next;
            }
         }

      }

#ifdef VERBOSE_CORRELATION
      {
         Msg *p;
         p=msgs;
         while (p) {
            fprintf(stderr,"%-40s  %12g\n",printmsg(p),p->cor);
            p=p->next;
         }
      }
#endif
   }
}

/************************************************************************************************************************/

void init_buffers(void)
{
   int i;

   brw=0; for (i=0;i<=BRMASK;i++) br[i]=0; brt=-1;
   bcw=0; for (i=0;i<=BCMASK;i++) bc[i]=0; bct=-FFTBLOCKSIZE/2;
   bdw=0; for (i=0;i<=BDMASK;i++) bdi[i]=bdq[i]=0; bdt=-1;
   bsw=0; for (i=0;i<=BSMASK;i++) bsi[i]=bsq[i]=0; bst=-DOWN;
   bfw=0; for (i=0;i<=BFMASK;i++) bf[i]=0; bft=-DOWN;
   bmw=0; for (i=0;i<=BMMASK;i++) bm[i]=0; bmt=-DOWN;
   byw=0; for (i=0;i<=BYMASK;i++) by[i]=bY[i]=0;
   btw=0; for (i=0;i<=BTMASK;i++) bt[i]=bT[i]=0;
   byr=0;
   btr=0;
   bbr_oldest=0;
}


void decode(void)
{
   /* initialize things */
   if (!fftresult) fftresult=malloc(FFTBLOCKSIZE/2*sizeof(*fftresult));
   init_buffers();
   bdphase=0;
   ioctl(0, SNDCTL_DSP_SETFRAGMENT, 256);   /* if stdin is a sound device, this sets its fragment size to 256, to reduce delays */ 

   /* main decoding loop */
   for (;;) {
      int c;

      /* main loop */

      /* read raw audio byte if available */
      c=getchar();
      if (c==EOF) break;
      br[brw]=(c-128)/128.;
      brw = (brw+1)&BRMASK;
      brt++;

      /* measure the carrier frequency */
      if (brt>=bct+FFTBLOCKSIZE+FFTBLOCKSIZE/2) {
         double f;
         uint t;

         /* measure the frequency using FFT */
         t = bct;
         f = search_carrier((brw+(t-brt))&BRMASK,FFTBLOCKSIZE);
         if (f<0) f=bc[(bcw-1)&BCMASK];
         bc[bcw] = f;
         bcw = (bcw+1)&BCMASK;
         bct = t+FFTBLOCKSIZE/2;  /* note: this timestamp is halfway the time covered by the block on which we just did the FFT */
      }

      /* downconvert the signal to DC */
      assert (bct-FFTBLOCKSIZE<=bdt);
      {
         double f,df;
         uint i;
         f = bc[(bcw-2)&BCMASK];
         df = (bc[(bcw-1)&BCMASK]-f)/FFTBLOCKSIZE;
         i = (brw-(brt-bdt))&BRMASK;
         while (bct>bdt) {
            bdi[bdw] = br[i]*cos(bdphase);
            bdq[bdw] = br[i]*sin(bdphase);
            bdw = (bdw+1)&BDMASK;
            bdt++;
            i = (i+1)&BRMASK;
            bdphase+=f/SAMP*2*M_PI;
            if (bdphase>2*M_PI) bdphase-=2*M_PI;
            f+=df;
         }
      }

      /* a simple low-pass filter: average over 48 samples, followed by downsampling */
      while (bst+DOWN < bdt-48/2) {
         int i,i0;
         float si,sq;
         bst+=DOWN;
         i0 = bdw - (bdt-bst);
         si=sq=0;
         for (i=-48/2;i<48/2;i++) {      /* this can be done more efficiently; or we could choose a better FIR filter */
            si+=bdi[(i0+i)&BDMASK];
            sq+=bdq[(i0+i)&BDMASK];
         }
         bsi[bsw] = si/48;
         bsq[bsw] = sq/48;
         bsw = (bsw+1)&BSMASK;
      }

      /* calculate magnitude of moving average over one dot-time */
      while (bft+DOWN < bst-DOWN*DOTLEN/2) {
         int i,i0;
         float si,sq;
         bft+=DOWN;
         i0 = bsw - (bst-bft)/DOWN;
         si=sq=0;
         for (i=-DOTLEN/2;i<DOTLEN/2;i++) {      /* this can be done more efficiently... */
            si+=bsi[(i0+i)&BSMASK];
            sq+=bsq[(i0+i)&BSMASK];
         }
         si/=DOTLEN;
         sq/=DOTLEN;
         bf[bfw] = sqrt(si*si+sq*sq);
         bfw = (bfw+1)&BFMASK;
         if (xpipe) {
            fprintf(xpipe,"0 %i %i\n",bft,(int)(65536*bf[(bfw-1)&BFMASK]));
            fflush(xpipe);
         }
      }

      /* estimate the bit clock, and sample the bits */
#define MPHESTLEN 20
      while (bmt+DOWN < bft-DOWN*DOTLEN*MPHESTLEN/2-1) {
         float s;
         float si,sq,ph;
         static float prevph=0;
         int i0,i;
         int sign;
         bmt+=DOWN;
         i0 = bfw - (bft-bmt)/DOWN;
         s = bf[i0&BFMASK];
         sign=-1;
         for (i=1;i<MPHESTLEN/2;i++) {
            s += sign * ( bf[((uint)(i0+i*DOTLEN+0.5))&BFMASK] + bf[((uint)(i0-i*DOTLEN+0.5))&BFMASK] );
            sign = -sign;
         }
         bm[bmw] = s/(MPHESTLEN+1);
         if (xpipe) { fprintf(xpipe,"3 %i %i\n",bmt,(int)(65536*bm[bmw])); fflush(xpipe); }
         bmw = (bmw+1)&BMMASK;

         si=sq=0;
         i0 = (bmw-1-(int)DOTLEN/2);
         for (i=-DOTLEN/2;i<=DOTLEN/2;i++) {
            s = bm[(i0+i)&BMMASK];
            si += s * cos(i*M_PI/DOTLEN);
            sq += s * sin(i*M_PI/DOTLEN);
         }
         ph = atan2(sq,si);
         if ((ph>=0 && prevph<0) || (ph<0 && prevph>=0)) {
            int t;
            t = ( bmt - ((bmw-i0)&BMMASK)*DOWN );
            i0 = bfw - (bft-t)/DOWN;
            by[byw] = bf[i0&BFMASK];     
            bY[byw]=t;
            byw = (byw+1)&BYMASK;
            byphase = (ph<0);
            if (xpipe) {
               fprintf(xpipe,"-1 %i %i\n",t,byphase);
               fflush(xpipe);
            }
         }
         prevph=ph;
      }

#define THRESTLEN 11  /* should be odd */
      while (byr!=byw) {
         float a;
         int i;
         int t;

         byr=(byr+1)&BYMASK;
         a = estimate_threshold2a(byr-1,THRESTLEN);
         bt[btw] = by[(byr-(THRESTLEN+1)/2)&BYMASK]-a;
         t = bT[btw] = bY[(byr-(THRESTLEN+1)/2)&BYMASK];
         btw = (btw+1)&BTMASK;
         btphase = byphase ^ ((byr-(THRESTLEN+1)/2-byw)&0x01);
         if (xpipe) fprintf(xpipe,"1 %i %i\n",t,(int)(65536*a));

         decide(btphase);

         i=(btw-2)&BTMASK;
         if (xpipe) fprintf(xpipe,"2 %i %i\n",t,(int)(65536*bt[i]));
         i=(btw-1)&BTMASK;
         if (xpipe) fprintf(xpipe,"2 %i %i\n",t,(int)(65536*bt[i]));
      }
   }
}




int main(int argc,char **argv)
{
   int dox=1;

   struct option longopts[]={
      { "help",      0, NULL, 'h' },
      { "nox11",     0, NULL, 'x' },
      { "nox",       0, NULL, 'x' },
      { "x11",       0, NULL, 'x' },
      { "track",     0, NULL, 't' },
      { "strongest", 0, NULL, 's' },
      { "wpm",       1, NULL, 'w' },
      { "range",     1, NULL, 'f' },
      { "freq",      1, NULL, 'f' },
      { "frequency", 1, NULL, 'f' },
      { "fixed",     1, NULL, 'f' },
   };

   maketree();

   /* parse commandline parameters */
   while (1) {
      int c;
      c = getopt_long(argc,argv, "-hxtsw:f:", longopts, NULL);
      if (c==-1) break;
      switch (c) {
         case '?': 
         case 'h': 
            puts("RSCW -- morse decoder for soundcard (version " VERSION ")\n"
                 "\n"
                 "Reads audio data (unsigned, 8 bits per sample, 8000 samples per second) from\n"
                 "stdin, and writes decoded text to stdout. If the X11 window system is running,\n"
                 "additionally, a graphical display is shown.\n"
                 "\n"
                 "Options:\n"
                 "  -x       --nox11    : don't open the X11 windows\n"
                 "  -w num   --wpm num  : set speed in words per minute\n"
                 "  -t       --track    : set frequency mode to track drifting signals\n"
                 "  -s       --strongest: set frequency mode to always take the strongest signal\n"
                 "  -f num   --freq num : set frequency mode to fixed at num Hz\n"
                 "  -f num1-num2   --freq num1-num2  : set frequency range for track / strongest\n"
                 "\n"
                 "Usage example (for receiving live telemetry from the RS12 satellite):\n"
                 "  rscw -w 26.6 --track < /dev/dsp\n"
                 "\n"
                 "Keypresses in the \"history\" window:\n"
                 "  Z = zoom in, X = zoom out, H = go left, L = go right, K = go to end, Q = quit\n"
                 "Keypresses in the \"spectrum\" window:\n"
                 "  Z = zoom in,  X = zoom out,  C = center on carrier,  Q = quit\n"
                 "\n"
                );
            return 1;
         case 'x': dox=0; break;
         case 't': fmode=TRACK; break;
         case 's': fmode=STRONGEST; break;
         case 'w': WPM=atof(optarg); break;
         case 'f': 
            if (strchr(optarg,'-')) {
               int l=0;
               sscanf(optarg,"%lg%n",&Fmin,&l);
               sscanf(optarg+l+1,"%lg",&Fmax);
            }
            else Fmin=Fmax=atof(optarg); 
            break;
      }
   }

   printf("RSCW   %g wpm   mode %s   %g - %g Hz    ( rscw -h  for help)\n", 
      WPM, 
      (fmode==TRACK) ? "track" : (fmode==STRONGEST) ? "strongest" : "fixed",
      Fmin, Fmax);
   ffmin=Fmin;  ffmax=Fmax;

   /* if desired, start the graphical display */
   if (!getenv("DISPLAY") || strlen(getenv("DISPLAY"))==0) dox=0;
   if (dox) {
      fftresult_shmid = shmget(IPC_PRIVATE, FFTBLOCKSIZE/2*sizeof(*fftresult),0600);
      fftresult=shmat(fftresult_shmid,NULL,0);
      shmctl(fftresult_shmid,IPC_RMID,NULL);
      xpipe=popen("rscwx","w");
   }

   /* call the actual decoder */
   decode();

   /* free the shared memory allocation */
   shmdt(fftresult);

   return 0;
}


