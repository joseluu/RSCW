/* noisycw - generate noisy morse code audio signals
 *
 * Copyright 2001, Pieter-Tjerk de Boer, pa3fwm@amsat.org
 *
 * Distributed on the conditions of the Gnu Public License, version 2.
 *
 * This program is part of the RSCW package; see
 *   https://www.pa3fwm.nl/software/rscw/
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <getopt.h>

#define VERSION "0.1"


double FREQ=1000.0;    /* frequency of the CW tone */
double WPM=24;         /* morse speed in words per minute */

#define SAMP 8000.0    /* sample rate; 8000 Hz is the default for /dev/dsp */
#define DOTLEN (SAMP*1.2/WPM)   /* duration of one dot in samples */


/* this algorithm for generating samples from the standard-normal distribution
   is from the C-FAQ (1996-09-05 version), slightly optimized */
double gaussrand()
{
   static double V1, V2, S;
   static int phase = 0;
   double X;

   if(phase == 0) {
      do {
         double U1 = (double)rand()/RAND_MAX;
         double U2 = (double)rand()/RAND_MAX;

         V1 = 2 * U1 - 1;
         V2 = 2 * U2 - 1;
         S = V1 * V1 + V2 * V2;
      } while(S >= 1 || S == 0);
      S=sqrt(-2 * log(S) / S);
      X = V1*S;
   } else
      X = V2*S;

   phase = 1 - phase;

   return X;
}


#define Nfir  127         /* number of taps for the FIR filter */
#define BWf   64          /* factor governing the bandwidth of the filter; the actual bandwidth is roughly 13 kHz / BWf */  

#define Center (Nfir/2)   /* index of center tap; assumes Nfir is odd! */
double fircoef[Nfir];     /* array for the FIR filter coefficients */
void prep_fir(void)
{
   int i;
   for (i=0;i<Nfir;i++) {
      int j=i-Center;
      fircoef[i] = cos(FREQ*(double)j/SAMP*2*M_PI);    /* carrier */
      if (j!=0) fircoef[i] *= sin((double)j/BWf*2*M_PI) / j;   /* sinc() factor */
      else fircoef[i] = 2*M_PI/BWf;
      fircoef[i] *= cos((double)j/Nfir*M_PI) ;            /* windowing */
      fircoef[i] /= 1.6 ;                                 /* correction factor, to make the filter's gain approximately 1 at the passband center */
   }
}


char alphabet[256][32]= {
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "0000", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",

 "1110111011101110111000", "10111011101110111000", "101011101110111000", "1010101110111000", "10101010111000",
 "101010101000", "11101010101000", "1110111010101000", "111011101110101000", "11101110111011101000",
 "", "", "", "", "", "",

 "","10111000","111010101000","11101011101000","1110101000","1000","101011101000","111011101000",
 "1010101000","101000","1011101110111000","111010111000","101110101000","1110111000","11101000","11101110111000",
 "10111011101000","1110111010111000","1011101000","10101000","111000","1010111000","101010111000","101110111000",
 "11101010111000","1110101110111000","11101110101000","","","","","",

 "","10111000","111010101000","11101011101000","1110101000","1000","101011101000","111011101000",
 "1010101000","101000","1011101110111000","111010111000","101110101000","1110111000","11101000","11101110111000",
 "10111011101000","1110111010111000","1011101000","10101000","111000","1010111000","101010111000","101110111000",
 "11101010111000","1110101110111000","11101110101000","","","","","",

 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
 "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};


int main(int argc, char **argv)
{
   double carrierphase=0.0;
   double bitphase=0;
   double snr=10;
   double anoise,asig,asum;
   char *q;
   int c;
   double xx[Nfir];
   double sumnoise,sumsig;
   double sumNb;
   int nnoise,nsig,nNb;
   double noise_i, noise_q;
   int niq;
   int noisefilter=0;
   int quiet=0;


   struct option longopts[]={
      { "help",      0, NULL, 'h' },
      { "wpm",       1, NULL, 'w' },
      { "freq",      1, NULL, 'f' },
      { "frequency", 1, NULL, 'f' },
      { "eb",        1, NULL, 'e' },
      { "snr",       1, NULL, 'e' },
      { "bandpass",  0, NULL, 'b' },
      { "quiet",     0, NULL, 'q' },
   };

   /* parse commandline parameters */
   while (1) {
      int c;
      c = getopt_long(argc,argv, "-hbqe:f:w:", longopts, NULL);
      if (c==-1) break;
      switch (c) {
         case '?': 
         case 'h': 
            puts("noisycw -- generate noisy morse signals (version " VERSION ")\n"
                 "\n"
                 "Reads ASCII characters from stdin, and writes corresponding morse code audio\n"
                 "(unsigned, 8 bits per sample, 8000 samples per second) to stdout.\n"
                 "\n"
                 "Options:\n"
                 "  -b       --bandpass : bandpass filter the noise\n"
                 "  -e num   --eb num   : set Eb/N0 (signal to noise ratio) to num dB\n"
                 "                        (note: Eb is the energy in one dot or one third dash,\n"
                 "                        not the average over dots/dashes and pauses)\n"
                 "  -f num   --freq num : set carrier frequency to num Hz\n"
                 "  -w num   --wpm num  : set speed in words per minute\n"
                 "  -q       --quiet    : don't print output about settings and measured SNR\n" 
                 "\n"
                 "See  https://www.pa3fwm.nl/software/rscw/  for more information and\n"
                 "examples.\n"
                );
            return 1;
         case 'b': noisefilter=1; break;
         case 'e': snr=atof(optarg); break;
         case 'f': FREQ=atof(optarg); break;
         case 'w': WPM=atof(optarg); break;
         case 'q': quiet=1; break;
      }
   }

   if (!quiet) fprintf(stderr,"NOISYCW   %g wpm   %g Hz   %g dB\n", WPM, FREQ, snr);

   sumnoise=sumsig=sumNb=0;
   nnoise=nsig=nNb=0;
   noise_i=noise_q=0;
   niq=0;

   /* calculate the coefficients for the noise and carrier amplitudes */
   anoise=1.0/M_SQRT2;                  /* noise power spectral density N0 = 1.0 */
   asig = sqrt(pow(10,snr/10)/DOTLEN*2);  /* bit energy Eb = snr (dB) above 1.0 */
   asum = anoise+asig;            
   anoise = 32*anoise/asum;       /* normalize anoise and asig, such that the resulting signals fit nicely in -128...+127 */
   asig = 32*asig/asum;

   /* prepare the FIR filter */
   prep_fir(); 

   /* start the main loop: convert incoming ASCII into morse audio */
   c=getchar();
   if (c==EOF) return 0;
   q=alphabet[c];
   for (;;) {
      double x;
      double y;
      double z;
      int i;

      x=gaussrand();
      for (i=0;i<Nfir-1;i++) xx[i]=xx[i+1];
      xx[Nfir-1]=x;
      if (noisefilter) {
         x=0;
         for (i=0;i<Nfir;i++) x+=xx[i]*fircoef[i];
      } else {
         x = xx[Center];
      }
      x*=anoise;
      y=asig*cos(carrierphase);
      z=x+((*q!='0')?y:0);

      putchar(128 + z);

      sumnoise+=x*x; nnoise++;
      sumsig += y*y; nsig++;
      noise_i+=M_SQRT2*x*cos(carrierphase);
      noise_q+=M_SQRT2*x*sin(carrierphase);
      niq++;

      carrierphase+=2*M_PI*FREQ/SAMP;
      bitphase+=1/DOTLEN;
      if (bitphase>=1.0) {
         noise_i/=niq; noise_q/=niq;
         sumNb += noise_i*noise_i + noise_q*noise_q; nNb++;
         noise_i=noise_q=0; niq=0;
         bitphase-=1.0;
         q++;
         while (*q==0) {
            c=getchar();
            if (c==EOF) goto end;
            q=alphabet[c];
         }
      }
   }

end:
   sumnoise/=nnoise;
   sumsig/=nsig;
   sumNb/=nNb;
   if (!quiet) {
      fprintf(stderr,"Noise power = %g\n",sumnoise);
      fprintf(stderr,"Noise power in receiver bandwidth = %g\n",sumNb);
      fprintf(stderr,"Signal power = %g\n",sumsig);
      fprintf(stderr,"S/N = %g dB\n",10*log10(sumsig/sumnoise));
      fprintf(stderr,"Eb/N0 = %g dB\n",10*log10(sumsig/sumNb));
   }


   return 0;
}

