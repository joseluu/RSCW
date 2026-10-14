/* rs12tlmdec - decode telemetry from the RS12 amateur radio satellite
 *
 * Copyright 2001,2017 Pieter-Tjerk de Boer, pa3fwm@amsat.org
 *
 * Distributed on the conditions of the Gnu Public License, version 2.
 *
 * This program is part of the RSCW package; see
 *   http://www.cs.utwente.nl/~ptdeboer/ham/rscw/
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>


char dmsg[16][2][28]= {
     { "Telem.sampl.period: 90 min.",         "Telem.sampl.period: 10 min."   },
     { "2 mtr RX: attenuator 20 dB",          "2 mtr RX: attenuator  0 dB"    },
     { "15 mtr RX: attenuator 10 dB",         "15 mtr RX: attenuator  0 dB"   },
     { "15 mtr uplink: OFF",                  "15 mtr uplink: ON"             },
     { "2 mtr RX: OFF",                       "2 mtr RX: ON"                  },
     { "special cmd.stat.ch.: OFF",           "special cmd.stat.ch.: ON"      },
     { "pwr 10 mtr beacon no.1: MAX",         "pwr 10 mtr beacon no.1: MIN"   },
     { "pwr 10 mtr beacon no.2: MAX",         "pwr 10 mtr beacon no.2: MIN"   },
     { "first memory board: OFF",             "first memory board: ON"        },
     { "second memory board: OFF",            "second memory board: ON"       },
     { "there is infos in memory 1",          "no infos in memory 1"          },
     { "there is infos in memory 2",          "no infos in memory 2"          },
     { "mem. data send via beacon 2",          "mem. data send via beacon 1"  },
     { "15 mtr robot RX att.: -10dB",         "15 mtr robot RX att.:  0 dB"   },
     { "2 mtr robot RX att.: -10dB",          "2 mtr robot RX att.:  0 dB"    },
     { "outp.pwr spec.cmd ch.: MAX",          "outp.pwr spec.cmd ch.: MIN"    }
   };

char *amsg(int ch,int val)
{
   static char s[80];
   s[0]=0;
   switch (ch) {
      case 1:  sprintf(s,"power supply           = %5g V", (double)val/4); break;
      case 2:  sprintf(s,"power output 2 mtr TX  = %5g W", (double)val/10); break;
      case 3:  sprintf(s,"power output 10 mtr TX = %5g W", (double)val/10); break;
      case 4:  sprintf(s,"15 mtr RX-AGC voltage  = %5g V", (double)val/5); break;
      case 5:  sprintf(s,"2 mtr RX-AGC voltage   = %5g V", (double)val/5); break;
      case 6:  sprintf(s,"spec.cmd. AGC voltage  = %5g V", (double)val/5); break;
      case 7:  break;
      case 8:  break;
      case 9:  sprintf(s,"temp. of 10 mtr TX     = %5i C", val-10); break;
      case 10: sprintf(s,"temp. of 2 mtr TX      = %5i C", val-10); break;
      case 11: sprintf(s,"temp. of 20 V pwr sup. = %5i C", val-10); break;
      case 12: sprintf(s,"temp. of 9 V pwr sup.  = %5i C", val-10); break;
      case 13: sprintf(s,"9 V pwr supply cntrl   = %5g V", (double)val/5); break;
      case 14: sprintf(s,"15 mtr robot RX AGC    = %5g V", (double)val/5); break;
      case 15: sprintf(s,"2 mtr robot RX AGC     = %5g V", (double)val/5); break;
      case 16: 
         if (val==0) sprintf(s,"less than 32 QSOs in log");
         else if (val>=80) sprintf(s,"more than 32 QSOs in log");
         else sprintf(s,"(undefined?)");
         break;
   }
   return s;
} 



void dec(char *s)
{
   int ch;
   int d,a;
   
   if (!isdigit(s[3]) || !isdigit(s[4])) return;

   switch (s[0]) {
      case 'I': ch=0; break;
      case 'N': ch=4; break;
      case 'A': ch=8; break;
      case 'M': ch=12; break;
      default: return;
   }

   switch (s[1]) {
      case 'I': ch+=1; break;
      case 'N': ch+=2; break;
      case 'A': ch+=3; break;
      case 'M': ch+=4; break;
      default: return;
   }

   switch (s[2]) {
      case 'S': case 'D': case 'R': case 'G': d=0; break;
      case 'U': case 'K': case 'W': case 'O': d=1; break;
      default: return;
   }

   a = s[4]-'0' + 10*(s[3]-'0');

   printf("\n--------> ch.%-2i  %-28s  %-30s\n",ch,dmsg[ch-1][d],amsg(ch,a));
}


int main(int argc, char **argv)
{
   char s[]="       ";
   int c;

   if (argc>1) {
      puts("RS12TLMDEC - decode telemetry from the RS12 amateur radio satellite\n"
           "\n"
           "The RS12 satellite transmits its telemetry in morse code (CW) format on\n"
           "29.408 MHz. This program expects to read an ASCII representation of this\n"
           "data on its input, and will print it, annotated with whatever can be\n"
           "decoded from it in terms of actual telemetered voltages, temperatures, etc.\n"
           "\n"
           "The companion program  RSCW  performs the task of extracting a morse code\n"
           "signal from data read from a soundcard and translating that into ASCII.\n"
           "Thus, you could decode RS12's telemetry using the command\n"
           "  rscw < /dev/dsp | rs12tlmdec\n"
           "\n");
       return 1;
   }

   while ( (c=getchar()) != EOF) {
      putchar(c);
      memcpy(s,s+1,4);
      s[4]=toupper(c);
      dec(s);
      fflush(stdout);
   }
   return 0;
}


