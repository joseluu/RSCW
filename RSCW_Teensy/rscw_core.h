/* rscw_core.h — cœur du décodeur RSCW, portable (PC et Teensy).
 *
 * Portage de original-rscw-0.1e/rscw.c (RSCW 0.1d/0.1e, Copyright 2001,2011,2020
 * Pieter-Tjerk de Boer, PA3FWM, GPL v2). Les calculs sont ceux de l'original, dans le même
 * ordre ; seuls changent :
 *  - l'état global devient les membres de la classe Rscw ;
 *  - getchar() devient push(échantillon), printf() de decision() devient le rappel onChar ;
 *  - les tampons circulaires sont réduits à la profondeur réellement nécessaire (voir les
 *    commentaires des masques) ;
 *  - malloc/free des hypothèses (Msg) et des nœuds de l'arbre remplacés par des pools fixes ;
 *  - FFTW remplacée par rscw_fft.h ;
 *  - l'interface X11 (xpipe/rscwx) est supprimée ; à la place, des accesseurs et un
 *    historique horodaté servent à l'oscilloscope du harnais.
 * Le test host/tests/equiv.sh vérifie que le texte décodé est identique à celui du rscw d'origine.
 *
 * Distribué sous les conditions de la GNU General Public License, version 2.
 */
#ifndef RSCW_CORE_H
#define RSCW_CORE_H

#include <stdint.h>

#ifndef RSCW_BIGBUF
#define RSCW_BIGBUF          /* sur Teensy : DMAMEM (RAM2) pour les gros tampons, voir rscw_core.cpp */
#endif

class Rscw {
public:
   typedef void (*CharCallback)(char c, void *ctx);

   enum { STRONGEST, TRACK };            /* modes de recherche de la porteuse (fmode) */

   static const int FFTBLOCKSIZE = 8192; /* FFT de recherche de porteuse */
   static const int DOWN = 8;            /* facteur de décimation 8 kHz -> 1 kHz */
   static const int MPHESTLEN = 20;
   static const int THRESTLEN = 11;
   static const int NBRANCH = 20;
   static const int MSGPOOL = 128;       /* hypothèses simultanées : max mesuré 30 sur PC (voir STATUT.md) */

   /* --- masques des tampons circulaires (taille = masque+1, puissance de 2) ---
    * Original : 131071 / 65535. Profondeurs nécessaires (en échantillons de l'étage) :
    *  br : la FFT lit [bct, bct+N) quand brt = bct+1.5N -> > 1.5N = 12288       -> 16383
    *  bd : écrit par rafales de N/2 = 4096, relu par le boxcar 48 en retard      ->  8191
    *  bs : rafales de 512 + DOTLEN (<= 240 à 5 WPM)                              ->  4095
    *  bf : rafales de 512 + MPHESTLEN·DOTLEN (<= 4800) ; sert aussi au scope     ->  8191
    *  bm : DOTLEN + 1                                                            ->  2047
    *  by / bt : inchangés (255 / 63).
    */
   static const unsigned BRMASK = 16383;
   static const unsigned BCMASK = 31;
   static const unsigned BDMASK = 8191;
   static const unsigned BSMASK = 4095;
   static const unsigned BFMASK = 8191;
   static const unsigned BMMASK = 2047;
   static const unsigned BYMASK = 255;
   static const unsigned BTMASK = 63;
   static const unsigned BHMASK = 255;   /* historique des bits pour le scope */
   static const unsigned BFMASK_ORIG = 65535;

   static constexpr double SAMP = 8000.0;
   static constexpr float WPM_MIN = 5.0f, WPM_MAX = 60.0f;

   Rscw();
   void begin(CharCallback cb, void *ctx);          /* construit l'arbre, remet à zéro */
   void reset();                                     /* équivalent d'un redémarrage de rscw */
   void push(float sample);                          /* un échantillon audio à 8 kHz, ~[-1,1] */

   /* réglages (comme les options de la ligne de commande de rscw) */
   void setWpm(float w);                             /* -w ; à faire suivre de reset() */
   float wpm() const { return WPM; }
   void setFreqMode(int mode) { fmode = mode; }      /* -s / -t */
   int freqMode() const { return fmode; }
   void setRange(double fmin, double fmax);          /* -f fmin-fmax (ou fmin==fmax : fixe) */
   double fmin() const { return Fmin; }
   double fmax() const { return Fmax; }

   /* état exposé pour le harnais */
   int time() const { return brt; }                  /* horodatage du dernier échantillon reçu (8 kHz) */
   double carrier() const { return bc[(bcw - 1) & BCMASK]; }
   bool carrierFound() const { return lastSearchOk; }
   float lastMag() const { return bf[(bfw - 1) & BFMASK]; }
   float magAt(int t) const;                         /* module bf à l'instant t (0 si hors historique) */
   bool bitAt(int t, float *soft, float *thr) const; /* dernier bit dont l'horodatage <= t */
   int latestBitTime() const { return bH[(bhw - 1) & BHMASK].t; }
   float dotSamples() const { return (float)(SAMP * 1.2 / WPM); } /* durée d'un point à 8 kHz */
   int scopeDelay() const;                           /* retard conseillé pour l'affichage (éch. 8 kHz) */
   int msgCount() const { return nmsgs; }
   int msgHighWater() const { return msgHigh; }
   uint32_t msgOverflows() const { return msgOverflow; }
   uint32_t charCount() const { return nchars; }

private:
   struct Branch {
      Branch *next_a, *next_b, *next_c, *up;
      int c;
      char s[16];
      long long tails;
   };
   struct Msg {
      double cor;
      Branch *b[NBRANCH];
      int ib;
      Msg *next;
   };
   struct BitHist { int t; float soft; float thr; };

   /* paramètres */
   float WPM;
   int fmode;
   double Fmin, Fmax, ffmin, ffmax;
   bool lastSearchOk;

   CharCallback onChar;
   void *onCharCtx;

   /* tampons (voir rscw.c) */
   float *br;  unsigned brw; int brt;
   float bc[BCMASK + 1]; unsigned bcw; int bct;
   float *bdi, *bdq; unsigned bdw; int bdt; float bdphase;
   float *bsi, *bsq; unsigned bsw; int bst;
   float *bf; unsigned bfw; int bft;
   unsigned bfwOrig;                     /* index qu'aurait bfw dans rscw.c (masque 65535), voir push() */
   float *bm; unsigned bmw; int bmt;
   float by[BYMASK + 1]; int bY[BYMASK + 1]; unsigned byw, byr; int byphase;
   float bt[BTMASK + 1]; unsigned btw, btr; int bT[BTMASK + 1]; int btphase;
   float prevph;
   BitHist bH[BHMASK + 1]; unsigned bhw;

   /* recherche de porteuse */
   double *fftin, *fftz, *fftre, *fftim;

   /* arbre Morse et décodeur */
   Branch branchPool[128]; int nbranch;
   Branch *tree;
   char taillist[64][12]; int ntails;
   Msg msgPool[MSGPOOL]; Msg *freeMsgs; int nmsgs, msgHigh; uint32_t msgOverflow;
   Msg *msgs;
   unsigned bbr_oldest;
   uint32_t nchars;

   double search_carrier(unsigned bri, int num);
   float estimate_threshold2a(unsigned i, int n);
   void maketaillist();
   void maketree();
   Branch *newBranch();
   Msg *allocMsg(const Msg *src);
   void freeMsg(Msg *m);
   Msg *prune(Msg *m0);
   void decision(Branch *b);
   void decide(int phase);
   void init_buffers();
};

#endif
