/* shim : permet de compiler le rscw.c d'origine sur PC sans OSS (seul SNDCTL_DSP_SETFRAGMENT est utilisé) */
#include <linux/soundcard.h>
