# RSCW sur Teensy 4.0

Portage du décodeur CW **RSCW** de Pieter-Tjerk de Boer, PA3FWM
([site](https://www.pa3fwm.nl/software/rscw/), [algorithme](https://www.pa3fwm.nl/software/rscw/algorithm.html))
sur Teensy 4.0 + Audio Shield, compilé avec `arduino-cli`, avec le harnais (écran, oscilloscope,
encodeur, commandes série) de [Morse_decode_coherent](https://github.com/joseluu/Morse_decode_coherent).

RSCW est fait pour sortir du bruit des signaux **à timing parfait** (manipulation par machine :
balises, télémesure) : il ne mesure pas la vitesse, il faut la lui donner (`set wpm`), et il décode
avec ~2 s de retard. En échange, il décide par maximum de corrélation sur l'ensemble des séquences
Morse possibles, ce qui le rend robuste au bruit.

- [PLAN.md](PLAN.md) — ce qui est fait et pourquoi ; [STATUT.md](STATUT.md) — avancement et mesures.
- [HARNAIS.md](HARNAIS.md) — écran, scope, menus, langage série (différences avec Morse_decode_coherent en tête).
- [`original-rscw-0.1e/`](original-rscw-0.1e/) — sources d'origine, non modifiées.

## Contenu

| Chemin | Rôle |
|---|---|
| `RSCW_Teensy/RSCW_Teensy.ino` | sketch : harnais, câblage audio |
| `RSCW_Teensy/rscw_core.{h,cpp}` | cœur RSCW portable (même code que `rscw.c`, sans X11 ni FFTW) |
| `RSCW_Teensy/rscw_fft.h` | FFT réelle 8192 points (remplace FFTW) |
| `RSCW_Teensy/rscw_decim.h` | décimateur FIR 48→8 kHz et file interruption → `loop()` |
| `RSCW_Teensy/AudioRSCW_F32.*` | objet audio OpenAudio F32 qui alimente le cœur |
| `RSCW_Teensy/AudioMixer11_F32.*`, `AudioSignalGenerator_F32.h` | repris de Morse_decode_coherent |
| `host/` | référence PC (`rscw` d'origine via un shim FFTW), `rscw_host` (cœur porté), tests |
| `tools/teensy_local_build.sh` | compilation arm-gcc sans paquet PJRC (dépannage) |
| `.github/workflows/build.yml` | CI : tests PC + `arduino-cli compile` teensy40 |

## Compiler et flasher

```bash
PJRC=https://www.pjrc.com/teensy/package_teensy_index.json
arduino-cli core update-index --additional-urls $PJRC
arduino-cli core install teensy:avr --additional-urls $PJRC
# bibliothèques hors Teensyduino (commits validés par la CI, voir build.yml) :
#   chipaudette/OpenAudio_ArduinoLibrary, KurtE/ILI9341_t3n, luni64/EncoderTool, thomasfredericks/Bounce2
arduino-cli compile --fqbn teensy:avr:teensy40 --libraries <dossier des bibliothèques> RSCW_Teensy
arduino-cli upload  --fqbn teensy:avr:teensy40 -p <port teensy> RSCW_Teensy
```

Sur le banc à deux cartes, flasher avec le port `teensy` retrouvé par `/dev/serial/by-id`, puis vérifier
l'identité par `status` (ligne `Decoder: RSCW …`) — voir HARNAIS.md §1. La CI publie aussi le `.hex`
(artefact `RSCW_Teensy-teensy40`).

Premiers essais sur le banc :

```
set wpm 20          (la vitesse du codeur)
log on              (texte décodé sur le port série)
status              (porteuse trouvée ? queue_drop = 0 ? msgovf = 0 ?)
```

## Vérifier sur PC

```bash
make -C host                   # rscw d'origine, noisycw, rscw_host, decim_test
host/tests/equiv.sh            # le cœur porté doit sortir le même texte que le rscw d'origine
host/build/decim_test 20 900 4 # CER : 8 kHz direct vs 48 kHz + décimation + file
host/build/noisycw -w 20 -e 12 -f 900 < texte.txt | host/build/rscw_host -w 20
```

`noisycw` définit le rapport signal/bruit comme Eb/N0 sur l'énergie d'un point. À 20 WPM,
SNR (bande 4 kHz) ≈ Eb/N0 − 23,8 dB.

## Licence

GPL v2 (voir `LICENSE`), comme RSCW dont ce projet est dérivé.
