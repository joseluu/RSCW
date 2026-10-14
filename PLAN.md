# PLAN.md — Portage de RSCW (PA3FWM) sur Teensy 4.0 avec le harnais de Morse_decode_coherent

Suivi d'avancement : [STATUT.md](STATUT.md). Sources d'origine, **non modifiées** : [`original-rscw-0.1e/`](original-rscw-0.1e/).

## 1. Objectif

Faire tourner l'algorithme de décodage CW de RSCW 0.1e (Pieter-Tjerk de Boer, PA3FWM, GPL v2) sur un
Teensy 4.0 + Audio Shield, compilé avec `arduino-cli`, avec le même harnais que
`Morse_decode_coherent` (écran ILI9341, oscilloscope intégré, encodeur rotatif + bouton, langage de
commandes série compatible avec le banc), tel que décrit dans
[HARNAIS.md de Morse_decode_coherent](https://github.com/joseluu/Morse_decode_coherent/blob/main/HARNAIS.md).

Contrainte de fidélité : **l'algorithme de décision doit rester celui de `rscw.c`**. Le portage
change la plomberie (E/S, tampons, allocation mémoire) mais pas les calculs : un test sur PC doit
montrer que le cœur porté produit **le même texte** que le `rscw` d'origine sur les mêmes fichiers.

## 2. Rappel de l'algorithme (lu dans `original-rscw-0.1e/rscw.c`)

Entrée : audio 8 kHz. Chaîne de traitement, tout en flux, chaque étage avec son tampon circulaire et
son horodatage (numéro d'échantillon) :

| Étape | Code d'origine | Ce qu'elle fait |
|---|---|---|
| 1. Recherche de porteuse | `search_carrier()` | FFT de 8192 points (fenêtre triangulaire) toutes les 4096 éch. (0,5 s) ; pic dans [Fmin, Fmax]. Modes `strongest` (pic toujours accepté) ou `track` (pic accepté s'il est ≥ 2× la moyenne, fenêtre ±20 Hz qui se resserre ou s'élargit). |
| 2. Transposition en bande de base | `decode()`, bloc « downconvert » | mélange I/Q avec la porteuse interpolée linéairement entre les deux dernières mesures FFT. |
| 3. Passe-bas + décimation | moyenne glissante sur 48 éch., décimation par 8 | → 1 kHz complexe. |
| 4. Module sur 1 point | moyenne complexe sur `DOTLEN` éch. puis module | intégration **cohérente** sur la durée d'un point → `bf`. |
| 5. Horloge bit | « morse phase » `bm` = somme alternée de `bf` sur ±10 points (`MPHESTLEN=20`), puis phase de la composante à 1/(2·point) | un échantillon `by` par point, pris au passage par zéro de la phase. |
| 6. Seuil | `estimate_threshold2a()` sur 11 bits (`THRESTLEN`), milieu pondéré | `bt = by − seuil` (bit « mou », signé). |
| 7. Décision | `decide()` + arbre Morse + `prune()` | décodage par **maximum de corrélation** sur les séquences de « dibits » (a=00, b=10, c=11, t=fin de caractère) autorisées par l'alphabet ; les hypothèses dominées sont élaguées ; un caractère est émis quand toutes les hypothèses restantes commencent par le même. |

Le décodeur ne fait **pas** d'estimation de vitesse : le WPM doit être donné (comme `rscw -w`). Il
introduit ~2 s de retard (README d'origine).

## 3. Architecture cible

```
RSCW_Teensy/                         sketch arduino-cli (dossier = nom du .ino)
  RSCW_Teensy.ino                    harnais : TFT, scope, encodeur, bouton, série, câblage audio
  rscw_core.h / rscw_core.cpp        cœur RSCW portable (C++ sans dépendance Arduino) — compilé aussi sur PC
  rscw_fft.h                         FFT radix-2 réelle 8192 points (remplace FFTW)
  AudioRSCW_F32.h / .cpp             objet audio F32 : décimation vers 8 kHz, file vers loop(), sorties analogiques
  AudioMixer11_F32.*, AudioSignalGenerator_F32.h   repris de Morse_decode_coherent
host/                                outils et tests sur PC (gcc)
  Makefile
  rscw_host.cpp                      même interface que rscw d'origine : audio u8 8 kHz sur stdin → texte sur stdout
  shim/fftw3.h, shim/sys/soundcard.h pour compiler le rscw.c d'origine sans FFTW (référence)
  decim_test.cpp, cwgen.h            chaîne 48 kHz -> décimation -> file -> cœur, CER, alignement du scope
  sim/                               simulation du sketch complet (bouchons Arduino/OpenAudio/écran)
  tests/                             equiv.sh (équivalence), sim_test.sh (harnais)
tools/teensy_local_build.sh          compilation arm-gcc sans paquet PJRC (dépannage)
.github/workflows/build.yml          CI : tests PC + `arduino-cli compile` pour teensy40
```

### 3.1 Cœur portable (`rscw_core`)

- Classe `Rscw` contenant tout l'état global de `rscw.c` (tampons, `msgs`, arbre, `prevph`…), avec
  `push(sample)` (remplace le `getchar()` de la boucle), `setWpm()`, `setFreqMode()`, `reset()`, et un
  rappel `onChar(char)` (remplace le `printf` de `decision()`).
- **Mêmes formules, même ordre des opérations**, mêmes constantes (`DOWN=8`, boxcar 48, `MPHESTLEN=20`,
  `THRESTLEN=11`, `Nbranch=20`, FFT 8192 / pas 4096, `TRACKTOL=20`).
- **Mémoire** : l'original réserve ~1,7 Mo de tampons (masques 131071/65535), trop pour le Teensy (1 Mo
  de RAM en tout). Les masques sont réduits au strict nécessaire (calcul des retards maximaux de chaque
  étage, documenté dans le code), les gros tampons vont en `DMAMEM` (RAM2). Comme tous les index sont
  modulo masque, le résultat est inchangé tant que la profondeur d'historique suffit — vérifié par le
  test d'équivalence.
- **Allocation** : `malloc`/`free` des messages de `decide()` remplacés par un pool fixe (liste libre)
  pour éviter la fragmentation du tas ; taille de pool mesurée sur PC (pire cas sur bruit pur), avec un
  compteur de débordement exposé dans `status`.
- **FFT** : FFTW remplacée par une FFT radix-2 maison (8192 points réels via 4096 complexes). Seul le
  module est utilisé → résultat identique aux arrondis près.
- Accesseurs pour le harnais : dernier `bf` (module), dernier seuil, dernier bit dur (`bt > 0`), porteuse
  estimée, horodatages, et un historique horodaté pour l'oscilloscope (§3.3).

### 3.2 Objet audio `AudioRSCW_F32`

- I2S réglé à **48 kHz** (`setI2SFreq`, comme le projet de référence le fait à 43,2 kHz), décimation
  **par 6** vers 8 kHz avec un FIR anti-repliement (passe-bande utile 0–3,4 kHz). Option de compilation
  pour faire tourner l'I2S directement à 8 kHz (sans décimation) si on veut comparer.
- `update()` (interruption audio) : décimation, mise en file des échantillons 8 kHz (file SPSC sans
  verrou) + échantillonnage des broches 14/15 au même rythme, pour que l'oscilloscope les montre
  **alignées** sur le signal retardé. Le traitement RSCW (dont la FFT) tourne dans `loop()`, pas dans
  l'interruption.
- Sorties analogiques pour l'oscilloscope externe (sélection par les menus `Out Left`/`Out Right`) :
  entrée décimée, I et Q en bande de base, module `bf`, seuil, bit dur — plus `INPUT` et `SIG INT`
  comme dans le harnais d'origine.

### 3.3 Harnais (repris de Morse_decode_coherent, adapté)

Repris à l'identique : brochage, écran d'accueil, zone texte, menu 5 lignes + ligne « scope », codes
couleur, encodeur/bouton, oscilloscope 300 px × 10 ms, capture série `scope` synchronisée sur la broche
14, lecture série 64 octets, identification `status` → ligne `Decoder: RSCW <VERSION>`.

Adaptations, imposées par l'algorithme :

| Élément | Morse_decode_coherent | RSCW_Teensy |
|---|---|---|
| Ligne de menu 4 | `Marge` (seuil fixe) | **`WPM`** (vitesse, pas 0,5, bornée 5–60) : c'est le seul réglage indispensable de RSCW |
| Sources `Out Left/Right` | 9 sorties du démodulateur cohérent | `MAG`, `I_BB`, `Q_BB`, `THRESH`, `BIT`, `DECIM`, `INPUT`, `SIG INT` |
| Trace jaune | puissance | module `bf` (sortie de l'étape 4) |
| Trace rouge | seuil Otsu | seuil `estimate_threshold2a` |
| Trace magenta | décision tonalité | bit dur `bt > 0` (étape 6) |
| Retard | ~temps réel | RSCW décide avec ~2–4 s de retard : **tout le scope (y compris broches 14/15) est affiché avec un retard fixe** calculé depuis le WPM, pour rester comparable à la vérité terrain |
| `set wpm` | préréglages 10/15/20 | toute valeur 5–60 (réinitialise le décodeur) |
| Commandes propres | Otsu, ML, démodulateur | `set fmode strongest\|track\|fixed`, `set freq <Hz>`, `set range <fmin> <fmax>`, `freq` (porteuse estimée) |

Commandes conservées avec la même syntaxe/réponse : `help`, `status`, `reset`, `log on|off`, `peak`,
`peak reset`, `scope`, `set out0|out1`, `set gain`, `set siggen`, `set wpm`. `HARNAIS.md` du présent
dépôt documentera le harnais adapté (même plan que l'original).

## 4. Vérification (sans matériel dans cet environnement)

L'environnement de développement n'a pas accès à `pjrc.com` ni à `downloads.arduino.cc` : la
compilation Teensy est faite par **GitHub Actions** (`arduino-cli` + core Teensy + bibliothèques), et
l'algorithme est validé sur PC :

1. **Référence** : compiler le `rscw.c` d'origine sur PC (shim FFTW + `soundcard.h`, sans X11).
2. **Équivalence** : `noisycw` (d'origine) génère des fichiers à plusieurs Eb/N0, vitesses et
   fréquences ; `rscw` d'origine et `rscw_host` (cœur porté) doivent produire **le même texte**.
   Cas couverts : modes `strongest`/`track`/`fixed`, 10–30 WPM, signal sur bruit élevé et faible,
   bruit pur (stress du pool de messages).
3. **Chaîne Teensy simulée** : le décimateur 48→8 kHz testé sur PC (signal 48 kHz → décimation →
   cœur) : taux d'erreur caractère (CER) comparable à l'entrée 8 kHz directe.
4. **Charge CPU / mémoire** : budget estimé et rapporté par `status` (temps max d'un appel de traitement,
   profondeur max de la file, taille max du pool) ; occupation RAM lue dans la sortie de
   `arduino-cli compile`.
5. **Simulation du sketch complet** (ajoutée en cours de route) : le vrai `RSCW_Teensy.ino` compilé
   sur PC avec des bouchons Arduino/OpenAudio/écran (`host/sim`), alimenté en blocs audio de 48 kHz
   et piloté par commandes série — vérifie le harnais de bout en bout (`host/tests/sim_test.sh`).
6. **Sur banc** (à faire par l'utilisateur, non faisable ici) : flash, `status` → `Decoder: RSCW …`,
   CER face au codeur `Codeur_CW_V1_5_T4` (même banc que Morse_decode_coherent).

## 5. Étapes

1. PLAN.md / STATUT.md, licence (GPL v2 — dérivé de RSCW).
2. Référence PC : shim, build du `rscw` d'origine + `noisycw`, jeu de fichiers de test.
3. Cœur portable `rscw_core` + `rscw_fft` + `rscw_host` ; test d'équivalence.
4. Tampons réduits + pool de messages ; re-test d'équivalence ; mesure du pire cas.
5. Objet audio (décimation, file, sorties) ; test PC du décimateur.
6. Sketch harnais ; workflow CI `arduino-cli compile` ; corriger jusqu'au vert.
7. Simulation PC du sketch complet, intégrée à la CI.
8. Documentation : README.md, HARNAIS.md adapté, STATUT.md final (résultats chiffrés, reste à faire).

## 6. Hors périmètre (pistes)

- Estimation automatique de la vitesse (absente de RSCW 0.1e).
- Portage de `rscwx` (affichage X11) — remplacé par le scope TFT ; `rs12tlmdec` (télémesure RS-12)
  non porté (l'espace long « aat » de RS-12 est en revanche conservé dans l'alphabet, comme l'original).
