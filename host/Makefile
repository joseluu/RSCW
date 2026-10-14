# Outils PC : référence (rscw d'origine), cœur porté, générateur de test.
CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
ORIG = ../original-rscw-0.1e
CORE = ../RSCW_Teensy

all: build/rscw_orig build/rscw_host build/noisycw build/decim_test build/sim_rscw

build:
	mkdir -p build

# rscw d'origine, sans X11 ni FFTW (shim/). Le fichier d'origine n'est pas modifié.
build/rscw_orig: $(ORIG)/rscw.c shim/fftw3.h $(CORE)/rscw_fft.h | build
	$(CC) -O2 -g -w -Ishim $(ORIG)/rscw.c -o $@ -lm

# noisycw d'origine ; seule retouche (copie dans build/) : xx[] initialisé à 0
# (lu non initialisé dans l'original -> NaN selon l'optimisation)
build/noisycw: $(ORIG)/noisycw.c | build
	sed 's/^   double xx\[Nfir\];/   double xx[Nfir]={0};/' $< > build/noisycw_fixed.c
	$(CC) -O2 -w build/noisycw_fixed.c -o $@ -lm

build/rscw_host: rscw_host.cpp $(CORE)/rscw_core.cpp $(CORE)/rscw_core.h $(CORE)/rscw_fft.h | build
	$(CXX) $(CXXFLAGS) rscw_host.cpp $(CORE)/rscw_core.cpp -o $@ -lm

build/decim_test: decim_test.cpp cwgen.h $(CORE)/rscw_decim.h $(CORE)/rscw_core.cpp $(CORE)/rscw_core.h | build
	$(CXX) $(CXXFLAGS) decim_test.cpp $(CORE)/rscw_core.cpp -o $@ -lm

test: all
	./tests/equiv.sh
	./build/decim_test 20 900 3
	./tests/sim_test.sh

clean:
	rm -rf build

.PHONY: all test clean

# Simulation du sketch complet (vrai RSCW_Teensy.ino + bouchons Arduino/OpenAudio/écran)
SIMSRC = $(CORE)/RSCW_Teensy.ino $(CORE)/AudioRSCW_F32.cpp $(CORE)/AudioMixer11_F32.cpp $(CORE)/rscw_core.cpp
build/sim_rscw: sim/sim_main.cpp $(SIMSRC) $(wildcard sim/mock/*.h) $(wildcard $(CORE)/*.h) cwgen.h | build
	cp $(CORE)/RSCW_Teensy.ino build/RSCW_Teensy_sim.cpp
	$(CXX) -O2 -g -w -Isim/mock -I$(CORE) sim/sim_main.cpp build/RSCW_Teensy_sim.cpp \
	  $(CORE)/AudioRSCW_F32.cpp $(CORE)/AudioMixer11_F32.cpp $(CORE)/rscw_core.cpp -o $@ -lm
