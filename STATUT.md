# STATUT.md — Suivi du portage RSCW → Teensy 4.0

Plan : [PLAN.md](PLAN.md). Ce fichier est mis à jour à chaque étape (le plus récent en haut du journal).

## Tableau de bord

| Étape (PLAN §5) | État | Preuve |
|---|---|---|
| 1. PLAN / STATUT / licence | fait | `PLAN.md`, `STATUT.md`, `LICENSE` (GPL v2, celle de RSCW) |
| 2. Référence PC (rscw d'origine) | fait | `host/shim/`, `make -C host` → `build/rscw_orig`, `build/noisycw` |
| 3. Cœur portable + équivalence | fait | `host/tests/equiv.sh` : **30/30 identiques** à l'original |
| 4. Tampons réduits + pool | fait | pool max mesuré **30** hypothèses (pool : 128), aucun débordement |
| 5. Objet audio (décimation, file) | fait | `decim_test` : même CER qu'en 8 kHz direct, file sans perte |
| 6. Sketch harnais + CI arduino-cli | fait | CI verte : `arduino-cli compile` teensy40 (core 1.62.0) + simulation du sketch |
| 7. Documentation | fait | `README.md`, `HARNAIS.md` |
| 8. Essai sur le banc réel | fait (2026-10-17) | voir « Reste à faire » |

## Reste à faire (nécessite le matériel)

1. Flasher (`.hex` dans l'artefact CI `RSCW_Teensy-teensy40`, ou `arduino-cli upload`) et vérifier
   l'identité : `status` → `Decoder: RSCW 0.1.0 …`.
2. Vérifier dans `status` : `queue_drop=0` (le traitement suit le temps réel), `msgovf=0`,
   `proc_max_us` (durée de la rafale de traitement toutes les 0,5 s ; estimée à quelques dizaines de ms),
   `cpu` (interruption audio).
3. Codeur à 20 WPM, 900 Hz : `set wpm 20`, `log on`, mesurer le CER selon le SNR du codeur ; une
   capture `scope` doit montrer la trace magenta (bit RSCW) calée sur la cyan (pin 15).
4. Points non vérifiables ici : `setI2SFreq(48000)` avec l'Audio Shield (même fonction que
   Morse_decode_coherent à 43,2 kHz), niveau d'entrée (`peak`), rendu réel de l'écran.

## Journal

### 2026-10-17 — essai sur le banc réel (étape 8) : CER vs SNR

- Flashé sur la carte décodeur (`status` : `Decoder: RSCW 0.1.0 …`, porteuse 900,4 Hz, `queue_drop=0`, `msgovf=0`). Compilation sur cette machine : Bounce2 installé (2.55) sans namespace, compilé avec le Bounce2 du commit CI (`--libraries`).
- `set wpm` existe déjà (5–60) : aucun réglage série ajouté.
- CER (%) à 10/15/20 WPM, défauts RSCW, un passage de 150 s par point :

| SNR (dB) | 10 WPM | 15 WPM | 20 WPM |
|---|---|---|---|
| 0 | 3,3 | 2,4 | 2,4 |
| −4 | 8,1 | 2,4 | 2,4 |
| −8 | 17,1 | 11,0 | 6,5 |
| −10 | 16,3 | 32,9 | 16,3 |
| −12 | 40,7 | 45,1 | 38,6 |
| −14 | 71,5 | 65,4 | 62,2 |
| −16 | 78,9 | 79,9 | 78,9 |
| −18 | 81,3 | 81,0 | 80,5 |

Détail et réserves : `STATUT_COMPARATIF.md` §8 du dépôt parent (campagne `bench/rscw_chain.sh`).

### 2026-10-15 — sketch, CI, simulation du sketch complet

- **Objet audio** `AudioRSCW_F32` : I2S à 48 kHz, FIR de décimation /6 (181 coefficients, Kaiser β=7,
  coupure 3,8 kHz : −0,3 dB à 3,4 kHz, ≤ −77 dB au-delà de 5 kHz), file SPSC de 4096 éch. (0,5 s)
  vers `loop()`, broches 14/15 échantillonnées avec le signal. `host/decim_test` (12 essais,
  20 WPM) : CER **identique** entre 8 kHz direct et la chaîne 48 kHz complète
  (20 dB : 0,013/0,008 ; 14 dB : 0,042/0,039 ; 10 dB : 0,400/0,399 ; 8 dB : 0,706/0,726).
- **Mémoire** : les blocs `AudioMemory_F32` d'OpenAudio sont alloués par `new` (tas en RAM2). Les
  tampons du cœur occupaient 420 Ko de RAM2 → FFT faite en place (`rscw_rfft_power_inplace`, mêmes
  calculs) : 289 Ko. Compilation CI : RAM1 libre 231 Ko, **RAM2 libre pour malloc 233 Ko** (besoin
  ~108 Ko pour 200 blocs F32).
- **CPU** : table cos/sin de l'estimation d'horloge (mêmes appels, mêmes valeurs ; équivalence
  inchangée). Sur PC le cœur tourne à ×424 le temps réel à 5 WPM (pire cas), ×747 à 20 WPM.
- **Harnais** (`RSCW_Teensy.ino`) : repris de Morse_decode_coherent, adapté (HARNAIS.md). Le scope
  affiche toutes les traces au même instant retardé. **Bug trouvé par le test d'alignement** : le
  bit RSCW est horodaté au centre du point, il était maintenu du centre au point suivant (décalage
  d'un demi-point, accord bit/manipulation 74 %) ; corrigé → 97–99,9 % (5–50 WPM, 25 dB).
- Générateur interne : les modes `1.0` et `-1.0` étaient inversés dans le code repris de
  Morse_decode_coherent (`AudioSignalGenerator_F32.h`) — corrigé ici, **à reporter là-bas**.
- **CI** (`.github/workflows/build.yml`) verte : tests PC + `arduino-cli compile --fqbn
  teensy:avr:teensy40` avec le core PJRC 1.62.0 et les bibliothèques épinglées (OpenAudio, ILI9341_t3n,
  EncoderTool, Bounce2). Compilation aussi vérifiée localement avec arm-gcc 13.3
  (`tools/teensy_local_build.sh`, pjrc.com étant inaccessible depuis l'environnement de développement).
- **Simulation du sketch complet** (`host/sim`, `host/tests/sim_test.sh`) : le vrai `.ino` compilé sur
  PC avec des bouchons Arduino/OpenAudio/écran, alimenté en blocs de 128 éch. à 48 kHz avec les
  broches 14/15 du codeur, piloté par commandes série. Vérifie : identification `status`, réponses de
  15 commandes, capture `scope` déclenchée par la pin 14 (≥ 280 lignes, Detect = Tone sur 97 %),
  aucune perte de file, CER du texte affiché **0,5 %** à 20 WPM / 20 dB (1 erreur, premier caractère).
  Autres points (CER hors bruit avant/après le message) : 30 et 50 WPM / 20 dB : 0 ; 20 WPM / 14 dB :
  5,3 % ; 12 dB : 12 % ; 10 WPM / 20 dB : 6,3 % ; 5 WPM / 20 dB : 5,9 %.

### Observations sur l'algorithme d'origine (non modifié)

- Le CER chute vite sous Eb/N0 ≈ 12 dB (20 WPM : 4 % à 14 dB, 40 % à 10 dB). À 10 dB les bits durs
  sont faux à ~14 % (dont ~2 % d'effets de bord aux transitions), contre ~5 % pour un détecteur
  d'enveloppe idéal avec seuil au milieu : l'écart vient de l'estimation du seuil sur 11 bits et de
  la gigue de l'horloge bit.
- Aux vitesses lentes (5–10 WPM) le rscw d'origine perd des points en début de caractère même à
  fort SNR (« GARIS », « R1FGV ») — même texte que le portage ; CER ≈ 9–12 % à 10 WPM / 20 dB contre
  ~1 % à 20–30 WPM. Hypothèse testée et **écartée** : la porteuse estimée au pas de la FFT (0,98 Hz).
  Une interpolation parabolique du pic (essai, retiré du code) donne des CER identiques à 10 WPM sur
  5 fréquences de porteuse (dont 900,5 Hz, entre deux pas). Cause non identifiée.
- Charge mesurée sur PC (x86) : rafale de traitement toutes les 0,5 s de 2,5 ms au pire (5 WPM),
  1 ms à 20 WPM ; moyenne 0,25 % d'un cœur. Sur le M7 à 600 MHz, attendre ~10 à 20× plus : quelques
  dizaines de ms par rafale, pour une file de 500 ms.
- Pistes d'amélioration, **à faire en option désactivée par défaut** pour garder l'équivalence :
  interpolation parabolique du pic FFT, seuil sur une fenêtre plus longue, horloge bit plus lissée.

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
  SNR dans 4 kHz ≈ Eb/N0 − 23,8 dB (CER de l'original ≈ 4 % à Eb/N0 = 14 dB, soit SNR(4 kHz) ≈ −10 dB).
