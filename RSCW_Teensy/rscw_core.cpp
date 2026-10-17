/* rscw_core.cpp — voir rscw_core.h.
 *
 * Portage de original-rscw-0.1e/rscw.c, Copyright 2001,2011,2020 Pieter-Tjerk de Boer, PA3FWM.
 * Distribué sous les conditions de la GNU General Public License, version 2.
 *
 * Les commentaires « rscw.c: » renvoient aux fonctions/blocs de l'original. Les expressions
 * (types, ordre des opérations, troncatures) sont recopiées telles quelles : le test
 * d'équivalence avec le rscw d'origine en dépend.
 */
#include "rscw_core.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "rscw_fft.h"
#ifdef RSCW_TRACE
#include <stdio.h>
#endif

#if defined(ARDUINO) && defined(__IMXRT1062__)
#include <Arduino.h>
#undef RSCW_BIGBUF
#define RSCW_BIGBUF DMAMEM
#endif

typedef unsigned int uint;

#define SAMP2 (SAMP/DOWN)          /* rscw.c: fréquence après décimation */
#define DOTLEN (SAMP2*1.2/WPM)     /* rscw.c: durée d'un point en échantillons décimés (double) */
#define TRACKTOL 20

/* Conversion double -> unsigned telle que la fait gcc sur x86-64 (troncature vers 0 sur 64 bits,
 * puis 32 bits de poids faible). rscw.c fait (uint)(x) avec x parfois négatif, ce qui est un
 * comportement indéfini en C : sur Cortex-M7 l'instruction sature à 0 au lieu de boucler. */
static inline uint to_uint(double x) { return (uint)(int64_t)x; }
/* Note C++ : <math.h> y fournit cos(float), sqrt(float)... en simple précision, alors qu'en C
 * rscw.c appelle les versions double. Les arguments float sont donc convertis explicitement
 * en double ci-dessous (cos((double)bdphase)...), sinon le résultat diffère de l'original. */

/* gros tampons, hors de l'objet pour pouvoir les placer en RAM2 sur Teensy */
static RSCW_BIGBUF float g_br[Rscw::BRMASK + 1];
static RSCW_BIGBUF float g_bdi[Rscw::BDMASK + 1];
static RSCW_BIGBUF float g_bdq[Rscw::BDMASK + 1];
static RSCW_BIGBUF float g_bsi[Rscw::BSMASK + 1];
static RSCW_BIGBUF float g_bsq[Rscw::BSMASK + 1];
static RSCW_BIGBUF float g_bf[Rscw::BFMASK + 1];
static RSCW_BIGBUF float g_bm[Rscw::BMMASK + 1];
static RSCW_BIGBUF double g_fftz[Rscw::FFTBLOCKSIZE];

Rscw::Rscw()
   : WPM(22), AVGF(1.0f), fmode(STRONGEST), Fmin(1), Fmax(SAMP / 2 - 1), ffmin(1), ffmax(SAMP / 2 - 1),
     lastSearchOk(false), onChar(0), onCharCtx(0),
     br(g_br), bdi(g_bdi), bdq(g_bdq), bsi(g_bsi), bsq(g_bsq), bf(g_bf), bm(g_bm),
     fftz(g_fftz),
     nbranch(0), tree(0), ntails(0), freeMsgs(0), nmsgs(0), msgHigh(0), msgOverflow(0),
     msgs(0), nchars(0)
{
}

void Rscw::begin(CharCallback cb, void *ctx)
{
   onChar = cb;
   onCharCtx = ctx;
   if (!tree) maketree();
   reset();
}

void Rscw::setWpm(float w)
{
   if (w < WPM_MIN) w = WPM_MIN;
   if (w > WPM_MAX) w = WPM_MAX;
   WPM = w;
   makePhaseTable();
}

/* table de l'estimation de phase de l'horloge bit : cos/sin(i*M_PI/DOTLEN), ne dépend que de WPM */
void Rscw::makePhaseTable()
{
   for (int i = -PHTAB/2; i < PHTAB/2; i++) {
      phCos[i+PHTAB/2] = cos(i*M_PI/DOTLEN);
      phSin[i+PHTAB/2] = sin(i*M_PI/DOTLEN);
   }
}

void Rscw::setRange(double fmin, double fmax)
{
   Fmin = fmin; Fmax = fmax;
   ffmin = Fmin; ffmax = Fmax;          /* rscw.c main(): ffmin=Fmin; ffmax=Fmax; */
}

/* rscw.c: decode(), partie initialisation */
void Rscw::reset()
{
   init_buffers();
   bdphase = 0;
   prevph = 0;
   ffmin = Fmin; ffmax = Fmax;
   lastSearchOk = false;
   /* pool d'hypothèses vidé */
   freeMsgs = 0;
   for (int i = MSGPOOL - 1; i >= 0; i--) { msgPool[i].next = freeMsgs; freeMsgs = &msgPool[i]; }
   nmsgs = 0;
   msgs = 0;
   for (uint i = 0; i <= BHMASK; i++) { bH[i].t = -0x40000000; bH[i].soft = 0; bH[i].thr = 0; }
   makePhaseTable();
   bhw = 0;
}

/* rscw.c: init_buffers() */
void Rscw::init_buffers()
{
   uint i;
   brw=0; for (i=0;i<=BRMASK;i++) br[i]=0; brt=-1;
   bcw=0; for (i=0;i<=BCMASK;i++) bc[i]=0; bct=-FFTBLOCKSIZE/2;
   bdw=0; for (i=0;i<=BDMASK;i++) bdi[i]=bdq[i]=0; bdt=-1;
   bsw=0; for (i=0;i<=BSMASK;i++) bsi[i]=bsq[i]=0; bst=-DOWN;
   bfw=0; for (i=0;i<=BFMASK;i++) bf[i]=0; bft=-DOWN; bfwOrig=0;
   bmw=0; for (i=0;i<=BMMASK;i++) bm[i]=0; bmt=-DOWN;
   byw=0; for (i=0;i<=BYMASK;i++) by[i]=bY[i]=0;
   btw=0; for (i=0;i<=BTMASK;i++) bt[i]=bT[i]=0;
   byr=0;
   btr=0;
   bbr_oldest=0;
   byphase=0;
   btphase=0;
}

/* rscw.c: search_carrier() — sans l'affichage X11 */
double Rscw::search_carrier(uint bri, int num)
{
   int i,iargmax=0;
   int imin,imax;
   double a,max;
   double df=SAMP/num;
   double avg;
   double f;
   int dcd;

   /* apply a simple triangular windowing function */
   for (i=0; i<num/2; i++) fftz[i]=br[(bri+i)&BRMASK]*((i+1)/(num/2.0));
   for (i=num/2; i<num; i++) fftz[i]=br[(bri+i)&BRMASK]*((num-i)/(num/2.0));
   /* perform the fft ; en sortie fftz[2i] = |X_i|^2 (voir rscw_fft.h) */
   rscw_rfft_power_inplace(fftz, num);

   /* search the peak within the allowed range */
   /* (rscw.c calcule fftresult[i] = out[i]^2 + out[num-i]^2 pour 1 <= i < num/2-1) */
   imax=-1; max=0;
   avg=0;
   imin=ffmin/df; imax=ffmax/df;
   for (i=imin; i<=imax; i++) {
      double p = (i>=1 && i<num/2-1) ? fftz[2*i] : 0;
      a = sqrt(p);
      if (a>max) { iargmax=i; max=a; }
      avg +=a;
   }
   avg /= imax-imin+1;
   f = iargmax*df;
   if (fmode == STRONGEST) {
      dcd=1;
   } else {
      dcd = (max>=avg*2);
      if (dcd) {
         f = iargmax*df;
         ffmin = f-TRACKTOL;
         ffmax = f+TRACKTOL;
      } else {
         ffmin = ffmin - TRACKTOL;
         if (ffmin<0) ffmin=0;
         ffmax = ffmax + TRACKTOL;
         if (ffmax>SAMP/2) ffmax=SAMP/2;
         f=-1;
      }
   }
   lastSearchOk = dcd;
   return f;
}

static int floatcmp(const void *a, const void *b)
{
   if (*(float*)a < *(float*)b) return -1;
   if (*(float*)a > *(float*)b) return 1;
   return 0;
}

/* rscw.c: estimate_threshold2a() (tableaux ramenés à la taille utile) */
float Rscw::estimate_threshold2a(uint i,int n)
{
   float a[2*THRESTLEN];
   float b[2*THRESTLEN];
   float t;
   float tmax,tmin,t0;

   {
      int j,k;
      k=0;
      for (j=0;j<n;j++) a[k++] = by[(i-j)&BYMASK];
      for (j=n/4;j<3*n/4;j++) a[k++] = by[(i-j)&BYMASK];
      n=k;
      qsort(a,n,sizeof(float),floatcmp);
   }

   b[0]=a[0];
   for (i=1;i<(uint)n;i++) b[i]=b[i-1]+a[i];
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
      } while (t>a[i] && i<(uint)n);
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
   } while (t<=a[i] && i<(uint)n);

   return (tmin+tmax)/2;
}

/* ---------------------------------------------------------------- arbre Morse (rscw.c) */

/* dibits: 'a' = 00, 'b' = 10, 'c' = 11; 't' = 00 as the terminating dibit of a character  */
struct Morsechar { char c; char s[12]; };

static const Morsechar alphabet[] =
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
#define Nmorsechar ((int)(sizeof(alphabet)/sizeof(alphabet[0])))

void Rscw::maketaillist()
{
   int i;
   ntails = 0;
   for (i=0;i<Nmorsechar;i++) {
      int j;
      const Morsechar *a=alphabet+i;
      for (j=0;j<(int)strlen(a->s);j++) {
         int k;
         for (k=0;k<ntails;k++) {
            if (strcmp(a->s+j,taillist[k])==0) break;
         }
         if (k==ntails) {
            strcpy(taillist[ntails],a->s+j);
            ntails++;
         }
      }
   }
}

Rscw::Branch *Rscw::newBranch()
{
   Branch *t = &branchPool[nbranch++];
   t->next_a = t->next_b = t->next_c = 0;
   t->c = 0;
   t->up = 0;
   t->tails = 0;
   t->s[0] = '\0';
   return t;
}

void Rscw::maketree()
{
   int i;
   maketaillist();
   nbranch = 0;
   tree = newBranch();
   for (i=0;i<Nmorsechar;i++) {
      const char *p;
      Branch *b=tree;
      p=alphabet[i].s;
      while (*p) {
         Branch **np=0;
         switch (*p) {
            case 'a': case 't': np=&b->next_a; break;
            case 'b': np=&b->next_b; break;
            case 'c': np=&b->next_c; break;
         }
         if (*np==0) {
            Branch *t = newBranch();
            size_t l = strlen(b->s);
            t->up = b;
            memcpy(t->s, b->s, l); t->s[l] = *p; t->s[l+1] = '\0';
            *np=t;
         }
         b = *np;
         p++;
      }
      b->c = alphabet[i].c;
      while (b->up) {
         int k;
         p--;
         for (k=0;k<ntails;k++) {
            if (strcmp(p,taillist[k])==0) break;
         }
         b->up->tails |= (1ll<<k);
         b=b->up;
      }
   }
}

/* ---------------------------------------------------------------- pool d'hypothèses */

Rscw::Msg *Rscw::allocMsg(const Msg *src)
{
   Msg *p = freeMsgs;
   if (!p) { msgOverflow++; return 0; }
   freeMsgs = p->next;
   memcpy(p, src, sizeof(Msg));
   nmsgs++;
   if (nmsgs > msgHigh) msgHigh = nmsgs;
   return p;
}

void Rscw::freeMsg(Msg *m)
{
   m->next = freeMsgs;
   freeMsgs = m;
   nmsgs--;
}

/* rscw.c: prune() */
Rscw::Msg *Rscw::prune(Msg *m0)
{
   Msg *m,*m1=0;
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
      } else {
         freeMsg(m);
      }
   }
   return m1;
}

/* rscw.c: decision() — sans l'affichage X11 */
void Rscw::decision(Branch *b)
{
   nchars++;
   if (onChar) onChar((char)b->c, onCharCtx);
}

#define bb bt
#define BBMASK BTMASK
#define bbw btw
#define bbr btr

/* rscw.c: decide() */
void Rscw::decide(int phase)
{
   int nb;

   phase ^= (bbw-1-bbr)&0x01;
   if (phase) { bbr = (bbr+1)&BBMASK; phase^=1; bbr_oldest++; }

   nb = (bbw-bbr)&BBMASK;

   if (msgs==0) {
      Msg init;
      memset(&init, 0, sizeof(init));
      init.cor=0;
      init.b[0]=tree;
      init.ib=0;
      init.next=0;
      msgs=allocMsg(&init);
   }

   while (nb>=2) {
      Msg *newmsgs;
      Msg *msg_t;
      double cor_a,cor_b,cor_c;
      int max_ib;

      cor_a = - bb[(bbr+0)&BBMASK] - bb[(bbr+1)&BBMASK];
      cor_b = + bb[(bbr+0)&BBMASK] - bb[(bbr+1)&BBMASK];
      cor_c = + bb[(bbr+0)&BBMASK] + bb[(bbr+1)&BBMASK];
      bbr = (bbr+2)&BBMASK;
      nb-=2;

      newmsgs=0;
      msg_t=0;
      max_ib=0;
      while (msgs) {
         Msg *p;

         if (msgs->b[msgs->ib]->next_a) {
            Branch *b;
            b = msgs->b[msgs->ib]->next_a;
            if (b->c!=0) {
               /* caractère terminé : on ne garde que la meilleure extension en 't' */
               double cor = msgs->cor + cor_a;
               if (msg_t==0 || cor > msg_t->cor) {
                  /* rscw.c alloue d'abord puis compare ; ici on compare d'abord pour ne pas
                     consommer le pool, et on libère l'ancien msg_t (rscw.c le perdait : fuite) */
                  p = allocMsg(msgs);
                  if (p) {
                     p->cor = cor;
                     p->b[p->ib]=p->b[p->ib]->next_a;
                     p->ib++;
                     if (p->ib>max_ib) max_ib=p->ib;
                     p->b[p->ib]=tree;
                     p->next=0;
                     if (msg_t) freeMsg(msg_t);
                     msg_t=p;
                  }
               }
            }
            if (b->next_a || b->next_b || b->next_c) {
               p = allocMsg(msgs);
               if (p) {
                  p->cor+=cor_a;
                  p->b[p->ib]=p->b[p->ib]->next_a;
                  p->next=newmsgs;
                  newmsgs=p;
               }
            }
         }

         if (msgs->b[msgs->ib]->next_b) {
            p = allocMsg(msgs);
            if (p) {
               p->next=newmsgs;
               p->cor+=cor_b;
               p->b[p->ib]=p->b[p->ib]->next_b;
               newmsgs=p;
            }
         }

         if (msgs->b[msgs->ib]->next_c) {
            p = allocMsg(msgs);
            if (p) {
               p->next=newmsgs;
               p->cor+=cor_c;
               p->b[p->ib]=p->b[p->ib]->next_c;
               newmsgs=p;
            }
         }

         p=msgs;
         msgs=msgs->next;
         freeMsg(p);
      }
      if (msg_t) {
         msg_t->next=newmsgs;
         newmsgs=msg_t;
      }

      msgs=prune(newmsgs);
      if (msgs==0) {
         /* ne peut arriver que si le pool est épuisé : on repart de la racine */
         Msg init;
         memset(&init, 0, sizeof(init));
         init.b[0]=tree;
         msgs=allocMsg(&init);
         continue;
      }

      if (max_ib>NBRANCH-1) {
         Msg *p;
         p = msgs;
         while (p) {
            if (p->ib>max_ib) max_ib=p->ib;
            p=p->next;
         }
         if (max_ib>=NBRANCH-1) {
            Msg *mp;
            double max;
            Branch *bb0;
            max=msgs->cor;  mp=msgs;
            p=msgs->next;
            while (p) {
               if (p->cor > max) { max=p->cor; mp=p; }
               p=p->next;
            }
            bb0=mp->b[0];
            mp=0;
            while (msgs) {
               if (msgs->b[0]!=bb0) {
                  p=msgs;
                  msgs=msgs->next;
                  freeMsg(p);
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

      if (msgs->ib>0) {
         Msg *p;
         p=msgs->next;
         while (p) {
            if (msgs->b[0]!=p->b[0]) break;
            p=p->next;
         }
         if (p==0) {
            double dcor;
            decision(msgs->b[0]);
            bbr_oldest = ( bbr_oldest + 2*strlen(msgs->b[0]->s) ) & BBMASK;
            dcor=msgs->cor;
            p=msgs;
            while (p) {
               p->cor-=dcor;
               memmove(p->b,p->b+1,p->ib*sizeof(p->b[0])); p->ib--;
               p=p->next;
            }
         }
      }
   }
}

#undef bb
#undef BBMASK
#undef bbw
#undef bbr

/* ---------------------------------------------------------------- boucle principale */

/* rscw.c: corps de la boucle for(;;) de decode(), pour un échantillon */
void Rscw::push(float sample)
{
   br[brw]=sample;
   brw = (brw+1)&BRMASK;
   brt++;

   /* measure the carrier frequency */
   if (brt>=bct+FFTBLOCKSIZE+FFTBLOCKSIZE/2) {
      double f;
      uint t;
      t = bct;
      f = search_carrier((brw+(t-brt))&BRMASK,FFTBLOCKSIZE);
      if (f<0) f=bc[(bcw-1)&BCMASK];
#ifdef RSCW_TRACE
      fprintf(stderr,"BC %.9g\n",f);
#endif
      bc[bcw] = f;
      bcw = (bcw+1)&BCMASK;
      bct = t+FFTBLOCKSIZE/2;
   }

   /* downconvert the signal to DC */
   {
      double f,df;
      uint i;
      f = bc[(bcw-2)&BCMASK];
      df = (bc[(bcw-1)&BCMASK]-f)/FFTBLOCKSIZE;
      i = (brw-(brt-bdt))&BRMASK;
      while (bct>bdt) {
         bdi[bdw] = br[i]*cos((double)bdphase);
         bdq[bdw] = br[i]*sin((double)bdphase);
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
      for (i=-48/2;i<48/2;i++) {
         si+=bdi[(i0+i)&BDMASK];
         sq+=bdq[(i0+i)&BDMASK];
      }
      bsi[bsw] = si/48;
      bsq[bsw] = sq/48;
      bsw = (bsw+1)&BSMASK;
   }

   /* calculate magnitude of moving average over one dot-time */
   const double AVGLEN = DOTLEN*AVGF;   /* extension : fenêtre = AVGF points (AVGF=1 : identique à l'original) */
   while (bft+DOWN < bst-DOWN*AVGLEN/2) {
      int i,i0;
      float si,sq;
      bft+=DOWN;
      i0 = bsw - (bst-bft)/DOWN;
      si=sq=0;
      for (i=-AVGLEN/2;i<AVGLEN/2;i++) {
         si+=bsi[(i0+i)&BSMASK];
         sq+=bsq[(i0+i)&BSMASK];
      }
      si/=AVGLEN;
      sq/=AVGLEN;
      bf[bfw] = sqrt((double)(si*si+sq*sq));
      bfw = (bfw+1)&BFMASK;
      bfwOrig = (bfwOrig+1)&BFMASK_ORIG;
   }

   /* estimate the bit clock, and sample the bits */
   while (bmt+DOWN < bft-DOWN*DOTLEN*MPHESTLEN/2-1) {
      float s;
      float si,sq,ph;
      int i0,i,io;
      int sign;
      bmt+=DOWN;
      i0 = bfw - (bft-bmt)/DOWN;
      s = bf[i0&BFMASK];
      sign=-1;
      /* rscw.c calcule ici (uint)(i0±i*DOTLEN+0.5) avec l'index de SON tampon (masque 65535) :
         quand cette valeur est négative (juste après un rebouclage), la troncature vers 0 décale
         l'échantillon lu d'une case. Pour reproduire exactement ce comportement avec un tampon
         plus petit, on calcule la même expression avec l'index qu'aurait l'original (bfwOrig),
         puis on réduit au masque local (65536 est un multiple de BFMASK+1). */
      io = bfwOrig - (bft-bmt)/DOWN;
      for (i=1;i<MPHESTLEN/2;i++) {
         s += sign * ( bf[to_uint(io+i*DOTLEN+0.5)&BFMASK] + bf[to_uint(io-i*DOTLEN+0.5)&BFMASK] );
         sign = -sign;
      }
      bm[bmw] = s/(MPHESTLEN+1);
      bmw = (bmw+1)&BMMASK;

      si=sq=0;
      i0 = (bmw-1-(int)DOTLEN/2);
      for (i=-DOTLEN/2;i<=DOTLEN/2;i++) {
         s = bm[(i0+i)&BMMASK];
         /* cos/sin(i*M_PI/DOTLEN) précalculés dans reset() (mêmes appels, mêmes valeurs) */
         si += s * phCos[i+PHTAB/2];
         sq += s * phSin[i+PHTAB/2];
      }
      ph = atan2((double)sq,(double)si);
      if ((ph>=0 && prevph<0) || (ph<0 && prevph>=0)) {
         int t;
         t = ( bmt - ((bmw-i0)&BMMASK)*DOWN );
         i0 = bfw - (bft-t)/DOWN;
         by[byw] = bf[i0&BFMASK];
         bY[byw]=t;
         byw = (byw+1)&BYMASK;
         byphase = (ph<0);
      }
      prevph=ph;
   }

   while (byr!=byw) {
      float a;
      int t;

      byr=(byr+1)&BYMASK;
      a = estimate_threshold2a(byr-1,THRESTLEN);
      bt[btw] = by[(byr-(THRESTLEN+1)/2)&BYMASK]-a;
      t = bT[btw] = bY[(byr-(THRESTLEN+1)/2)&BYMASK];
      bH[bhw].t = t; bH[bhw].soft = bt[btw]; bH[bhw].thr = a;
      bhw = (bhw+1)&BHMASK;
      btw = (btw+1)&BTMASK;
      btphase = byphase ^ ((byr-(THRESTLEN+1)/2-byw)&0x01);

#ifdef RSCW_TRACE
      fprintf(stderr,"BT %d %.9g %.9g %d\n",t,bt[(btw-1)&BTMASK],a,btphase);
#endif
      decide(btphase);
   }
}

/* ---------------------------------------------------------------- accès pour le harnais */

float Rscw::magAt(int t) const
{
   int back = (bft - t) / DOWN;
   if (back < 0 || back > (int)BFMASK - 600) return 0;   /* hors de l'historique sûr */
   return bf[(bfw - 1 - back) & BFMASK];
}

/* L'horodatage d'un bit (bY dans rscw.c) est le CENTRE du point échantillonné : le bit couvre
 * [t_bit - point/2, t_bit + point/2). On cherche donc le dernier bit avec t_bit <= t + point/2. */
bool Rscw::bitAt(int t, float *soft, float *thr) const
{
   int tc = t + (int)(dotSamples() / 2);
   for (uint k = 1; k <= BHMASK; k++) {
      const BitHist &h = bH[(bhw - k) & BHMASK];
      if (h.t <= tc) {
         if (h.t < -0x3fffffff) return false;
         *soft = h.soft; *thr = h.thr;
         return true;
      }
   }
   return false;
}

/* Retard (en échantillons 8 kHz) entre l'échantillon le plus récent et l'instant dont le bit
 * vient d'être décidé : 1,5 FFT + boxcar + MPHESTLEN/2 + (THRESTLEN+1)/2 points + 1 point
 * pour la phase, + 2 points de marge. Vérifié sur PC (host/tests/latency). */
int Rscw::scopeDelay() const
{
   double dot = SAMP * 1.2 / WPM;
   return (int)(1.5 * FFTBLOCKSIZE + 48 + (MPHESTLEN / 2 + (THRESTLEN + 1) / 2 + 3) * dot);
}
