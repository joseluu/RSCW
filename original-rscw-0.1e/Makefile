
CFLAGS = -O9 -g -Wall
CFLAGSX = -O9 -g -Wall `pkg-config --cflags gtk+-2.0`

LDFLAGS = -lm
LDFLAGSX = `pkg-config --libs gtk+-2.0` -lm

all:	rscw rscwx noisycw rs12tlmdec

rscw:	rscw.c
	$(CC) $(CFLAGS) rscw.c -o rscw -lfftw3 $(LDFLAGS)

rscwx:	rscwx.c
	$(CC) $(CFLAGSX) rscwx.c -o rscwx $(LDFLAGSX)

noisycw:	noisycw.c
	gcc -O9 noisycw.c -o noisycw -g -lm

rs12tlmdec:	rs12tlmdec.c
	gcc -O9 rs12tlmdec.c -o rs12tlmdec
