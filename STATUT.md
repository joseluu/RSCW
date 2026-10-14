# STATUT.md — Suivi du portage RSCW → Teensy 4.0

Plan : [PLAN.md](PLAN.md). Ce fichier est mis à jour à chaque étape (le plus récent en haut du journal).

## Tableau de bord

| Étape (PLAN §5) | État | Preuve |
|---|---|---|
| 1. PLAN / STATUT / licence | fait | `PLAN.md`, `STATUT.md`, `LICENSE` (GPL v2, celle de RSCW) |
| 2. Référence PC (rscw d'origine) | fait | `host/shim/`, `make -C host` → `build/rscw_orig`, `build/noisycw` |
| 3. Cœur portable + équivalence | fait | `host/tests/equiv.sh` : **30/30 identiques** à l'original |
| 4. Tampons réduits + pool | fait | pool max mesuré **30** hypothèses (pool : 128), aucun débordement |
| 5. Objet audio (décimation, file) | à faire | |
| 6. Sketch harnais + CI arduino-cli | à faire | |
| 7. Documentation finale | à faire | |

## Journal

### 2026-10-14 — cœur portable équivalent à l'original

- Le site pa3fwm.nl est inaccessible depuis l'environnement de développement : les sources ont été
  déposées par l'utilisateur dans `original-rscw-0.1e/` (non modifiées depuis).
- **Référence PC** : `rscw.c` d'origine compilé tel quel avec un shim FFTW (`host/shim/fftw3.h`, qui
  utilise la FFT du portage) et sans X11. `noisycw.c` d'origine compilé après une seule retouche,
  faite sur une copie dans `host/build/` : le tableau `xx[]` est lu sans être initialisé (NaN en `-O2`).
- **Cœur portable** `RSCW_Teensy/rscw_core.{h,cpp}` : classe `Rscw`, même code que `rscw.c`.
  Différences volontaires, toutes sans effet sur le texte décodé (vérifié) :
  - tampons réduits de ~1,7 Mo à ~290 Ko (masques justifiés dans `rscw_core.h`), gros tampons en `DMAMEM` ;
  - `malloc`/`free` → pools fixes ; correction d'une **fuite mémoire de l'original** dans `decide()`
    (l'ancien meilleur candidat `msg_t` remplacé n'était jamais libéré : ~45 Ko perdus en 30 s, vu à
    l'AddressSanitizer — sans conséquence sur PC, fatal à terme sur Teensy) ;
  - FFTW → `rscw_fft.h` (FFT réelle radix-2 en double, erreur 5e-14 face à une DFT directe).
- Deux pièges trouvés en traçant les étages intermédiaires, qui faisaient diverger le texte :
  1. **C++ vs C** : en C++, `cos(float)`/`sqrt(float)`/`atan2(float,float)` appellent les versions
     simple précision ; `rscw.c` (C) appelle les versions double. Arguments convertis en `double`.
  2. **`(uint)(double négatif)`** dans l'estimation d'horloge (`bf[(uint)(i0±i*DOTLEN+0.5)]`) :
     comportement indéfini en C, qui sur x86 boucle modulo 2^32 et décale d'une case au rebouclage
     du tampon ; sur Cortex-M7 la conversion sature à 0. Le portage reproduit le comportement x86
     de l'original avec l'index qu'aurait le tampon d'origine (`bfwOrig`, masque 65535).
- **Test d'équivalence** (`host/tests/equiv.sh`) : 30 cas — Eb/N0 20/8/4/0 dB, 6 à 50 WPM, modes
  strongest/track/fixe/plage, bruit filtré ou non, fichier long de ~4 min (au-delà des rebouclages
  de tous les tampons), WPM mal réglé, bruit seul : **texte strictement identique** dans les 30 cas.
- **Mesures** (`rscw_host -S`, 5–60 WPM, Eb/N0 −5 à 30 dB, bruit seul) : au plus 30 hypothèses
  simultanées, aucun débordement de pool ; latence des bits 1,4 s (60 WPM) à 5,5 s (5 WPM) ; le retard
  d'affichage du scope (`scopeDelay()`) est toujours au-dessus de cette latence.
- Repère pour comparer au banc : noisycw définit Eb/N0 sur l'énergie d'un point ; à 20 WPM,
  SNR dans 4 kHz ≈ Eb/N0 − 23,8 dB (l'original décode proprement vers Eb/N0 ≥ 10–12 dB à 20 WPM,
  soit SNR(4 kHz) ≈ −12 dB).
