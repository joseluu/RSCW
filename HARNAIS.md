# HARNAIS.md — Harnais du décodeur RSCW (écran, oscilloscope, bouton rotatif, liaison série)

Ce document décrit le harnais de `RSCW_Teensy/RSCW_Teensy.ino` (version `0.1.0 2026-10-14 RSCW 0.1e`).
Il reprend celui de [Morse_decode_coherent](https://github.com/joseluu/Morse_decode_coherent/blob/main/HARNAIS.md)
(même matériel, même écran, mêmes couleurs, même encodeur, même syntaxe série), avec les
adaptations imposées par RSCW, résumées ici puis détaillées section par section.

| Élément | Morse_decode_coherent | RSCW_Teensy |
|---|---|---|
| Ligne de menu 4 | `Marge` | **`WPM`** (vitesse, indispensable à RSCW) |
| Sources des sorties ligne | 9 sorties du démodulateur cohérent + INPUT + SIG INT | 6 sorties RSCW + INPUT + SIG INT (§1) |
| Fréquence I2S | 43,2 kHz | **48 kHz**, décimé ×6 → 8 kHz (fréquence de RSCW) |
| Oscilloscope | temps réel | **retardé** de 1,9 à 6,1 s selon le WPM (§3), broches 14/15 comprises |
| `set wpm` | préréglages 10/15/20 | toute valeur 5–60, réinitialise le décodeur |
| Commandes propres | Otsu, ML, démodulateur | `set fmode`, `set freq`, `set range`, `freq` |
| Identification `status` | `Decoder: Morse_decode_coherent …` | `Decoder: RSCW …` |

---

## 1. Matériel et brochage (Teensy 4.0) — identique

| Élément | Broche / réf. | Détail |
|---|---|---|
| Carte audio | Audio Shield Rev D (SGTL5000) | entrée ligne **canal droit** (`lineInLevel(0)` = 3,12 Vpp max), sortie ligne G/D |
| Écran | ILI9341 320×240 SPI, `ILI9341_t3n`, `setRotation(1)` | CS=10, DC=2, RST=1, MOSI=11, SCLK=13, MISO=12 |
| Encodeur rotatif | `EncoderTool`, `encoder.begin(3, 0)` | A=3, B=0 |
| Bouton | pin 4, `Bounce2::Button`, `INPUT_PULLUP`, 5 ms, appui = BAS | |
| LED | pin 5 | bit décidé par RSCW (retardé comme le scope, §3) |
| Entrées debug | pin 14 = sync (codeur pin 6), pin 15 = tone (codeur pin 9) | échantillonnées **dans l'interruption audio** avec le signal, pour être affichées alignées sur lui |
| Série USB | 115 200 bauds | §5 |

Réglages audio : `AudioMemory(20)`, `AudioMemory_F32(200)`, `lineInLevel(0)`, `lineOutLevel(13)`,
`volume(0.6)`, `setI2SFreq(48000)`.

### Chaîne audio

```
Line In (droit) ─► AudioInputI2S_F32 (48 kHz) ─► AudioRSCW_F32 ── décimation FIR /6 ─► file ─► loop() : cœur RSCW
                                                      │ 6 sorties
     AudioSignalGenerator_F32 (SIG INT) + entrée brute (INPUT)
                                                      ▼
                         2 × AudioMixer11_F32 ─► AudioOutputI2S_F32 (gain I2s2.setGain)
```

| Idx | Nom | Contenu |
|---|---|---|
| 0 | `MAG` | module `bf` de RSCW (moyenne complexe sur 1 point), **retardé**, ×2 |
| 1 | `I_BB` | bande de base I : signal 8 kHz × cos(porteuse estimée), moyenne sur 48 éch., ×2 (temps réel) |
| 2 | `Q_BB` | bande de base Q, idem |
| 3 | `THRESH` | seuil de décision de RSCW, retardé, ×2 |
| 4 | `BIT` | bit dur décidé (0 ou 0,5), retardé |
| 5 | `DECIM` | signal décimé à 8 kHz (maintenu) |
| 6 | `INPUT` | entrée ligne brute |
| 7 | `SIG INT` | générateur interne |

Au démarrage : **Out Left = I_BB, Out Right = MAG**, gain 1.0, générateur mode 0 (`1.0`).
`I_BB`/`Q_BB` montrent le battement entre le signal et la porteuse estimée : quasi continu quand
RSCW est bien calé. Le générateur interne a été corrigé : dans Morse_decode_coherent les modes
`1.0` et `-1.0` produisaient l'inverse de leur nom.

### Liaison avec le codeur

Identique à Morse_decode_coherent (`Codeur_CW_V1_5_T4`, audio + pins 6→14 et 9→15, masse commune).
Identification des cartes : même procédure ; la carte RSCW répond à `status` par une ligne
`Decoder: RSCW <VERSION>` (au lieu de `Decoder: Morse_decode_coherent …`). Les règles de sûreté du
flash restent valables (port `teensy` via `by-id`, `status` 2 s après l'ouverture du port).

---

## 2. Affichage TFT — identique sauf la ligne 4

Zone texte (y 0..36), menu 5 lignes de 22 px à partir de y=38, oscilloscope y=148..222 avec la barre
« scope » de 20 px à gauche, version en jaune à y=222, écran d'accueil de 3 s
(`decodeur Morse ` / VERSION / ` RSCW de PA3FWM`).

| Ligne | Nom | Valeur |
|---|---|---|
| 0 | `Out Left` | source (§1) |
| 1 | `Out Right` | source |
| 2 | `Gain` | `0.1`, `1.0`, `10.0` |
| 3 | `Sig Gen` | `1.0`, `0.0`, `-1.0`, `0.9Hz`, `9Hz`, `90Hz`, `900Hz` |
| 4 | `WPM` | vitesse, 1 décimale (ex. `20.0`) |

Les caractères décodés, **espaces compris**, s'affichent comme dans l'original (15 px par caractère,
2 lignes, effacement de page). Ils arrivent avec ~2 s de retard sur l'émission (propre à RSCW).

---

## 3. Oscilloscope intégré — géométrie identique, affichage retardé

300 px × 10 ms = 3 s, effacement 10 colonnes devant le curseur, balayage libre, synchronisation de la
capture série sur le front montant de la pin 14 — comme l'original.

**Différence essentielle** : RSCW ne décide un bit qu'après 1,5 bloc FFT (1,5 s) + ~17 points (horloge bit
sur ±10 points, seuil sur 11 bits). Pour que les traces restent comparables entre elles, **toutes** sont
lues au même instant audio retardé `T = maintenant − scopeDelay()` :

| WPM | 5 | 10 | 20 | 40 | 60 |
|---|---|---|---|---|---|
| retard affiché | 6,1 s | 3,8 s | 2,7 s | 2,1 s | 1,9 s |
| latence mesurée des bits (PC) | 5,5 s | 3,3 s | 2,1 s | 1,6 s | 1,4 s |

Les broches 14/15 sont échantillonnées dans l'interruption audio (une fois par bloc de 128 éch. à 48 kHz,
soit 2,7 ms) et rangées dans un historique au rythme du décodeur : elles sont donc retardées exactement
comme le signal, et la trace cyan (enveloppe réellement émise) reste alignée sur la décision magenta.

| Bande | Contenu | Couleur |
|---|---|---|
| Entrées debug | pin 14 (sync) et pin 15 (tone), retardées | vert / cyan |
| Décision | **bit dur de RSCW** : `bt = by − seuil > 0` (étape 6 de RSCW), maintenu jusqu'au bit suivant | magenta |
| Analogique | module `bf` (jaune) et seuil `estimate_threshold2a` (rouge), même échelle, échelle auto (max glissant ~20 s) | jaune / rouge |

Remarque : la trace magenta est la décision **bit par bit**. Le texte, lui, sort du décodeur à maximum
de corrélation sur l'arbre Morse (étape 7), qui peut corriger un bit isolé faux : un bit magenta erroné
n'implique pas un caractère faux.

### Capture série (`scope`)

Même déclenchement (commande `scope` ou bouton sur la ligne 5) et même format, séparateur tabulation :

```
Time	Sync	Tone	Detect	Power
<millis>	<pin14 retardée 0/1>	<pin15 retardée 0/1>	<bit RSCW 0/1>	<module bf %.5f>
```

`Power` est le module `bf` (amplitude, pas une puissance), avec 5 décimales car ses valeurs sont
petites (de l'ordre de la moitié de l'amplitude du signal d'entrée).

---

## 4. Bouton rotatif — identique sauf la ligne 4

| Ligne | Paramètre | Pas | Plage | Effet |
|---|---|---|---|---|
| 0 | Out Left | ±1 | cycle des 8 sources | `selectOutputChannel(0, idx)` |
| 1 | Out Right | ±1 | idem | `selectOutputChannel(1, idx)` |
| 2 | Gain | ±1 | cycle {0.1, 1.0, 10.0} | `I2s2.setGain` |
| 3 | Sig Gen | ±1 | cycle des 7 modes | `sigGen.setMode` |
| 4 | **WPM** | **±0,5** | bornée à [5, 60] | `setWpm` + réinitialisation du décodeur à chaque cran |

Ligne 5 (scope) : l'appui demande une capture série et fait clignoter la barre en jaune 200 ms.

---

## 5. Liaison série

115 200 bauds, une commande par ligne, tampon 64 octets, sensible à la casse — comme l'original.

### 5.1 Commandes communes avec Morse_decode_coherent

| Commande | Réponse / effet |
|---|---|
| `help` | liste des commandes, sources, modes du générateur |
| `status` | §5.3 |
| `reset` | `reset ok` — vide la file et remet RSCW à zéro (réglages conservés) |
| `log on` / `log off` | `log on` / `log off` — envoie les caractères décodés (et les espaces) sur le port série |
| `peak` / `peak reset` | `ADC peak max=… min=…` / `peak reset` (remet aussi à zéro les statistiques de charge) |
| `scope` | `Scope serial requested` |
| `set out0 <SRC>` / `set out1 <SRC>` | `Out Left -> SRC` ; sources : `MAG I_BB Q_BB THRESH BIT DECIM INPUT SIG INT` |
| `set gain <0.1\|1.0\|10.0>` | `Gain = 1.0` |
| `set siggen <0..6>` | `Sig Gen = 900Hz` |
| `set wpm <5..60>` | `wpm = 20.0 (decoder reset)` ; erreur `Error: wpm must be 5..60` |

### 5.2 Commandes propres à RSCW (équivalents des options de `rscw`)

| Commande | Option rscw | Effet |
|---|---|---|
| `set fmode strongest` | `-s` | porteuse = pic le plus fort de la plage (défaut) |
| `set fmode track` | `-t` | poursuite : pic accepté s'il dépasse 2× la moyenne, fenêtre ±20 Hz |
| `set freq <Hz>` | `-f Hz` | porteuse fixe |
| `set range <fmin> <fmax>` | `-f fmin-fmax` | plage de recherche |
| `freq` | — | `freq: <Hz> found=<0\|1>` : porteuse estimée par la dernière FFT |

Chacune réinitialise le décodeur. Défaut au démarrage : 20 WPM, `strongest`, 1–3999 Hz.

### 5.3 Sortie de `status`

```
Out Left: I_BB
Out Right: MAG
Gain: 1.0
Sig Gen: 1.0
WPM: 20.0
Decoder: RSCW 0.1.0 2026-10-14 RSCW 0.1e
wpm=20.0 fmode=strongest fmin=1.0 fmax=3999.0 log=0
carrier=900.4 found=1
dot=60.0ms delay=2682ms chars=<n> msgs=<n> msgmax=<n> msgovf=<n>
fs_i2s=48000 decim=6 queue_max=<n> queue_drop=<n> proc_max_us=<n> cpu=<%> mem_f32_max=<n>
```

- `msgs` / `msgmax` / `msgovf` : hypothèses du décodeur à corrélation maximale en cours / maximum
  atteint / refus faute de place dans le pool (128 ; le maximum mesuré sur PC est 30).
- `queue_max` / `queue_drop` : remplissage maximal de la file interruption → `loop()` (4096 éch. = 0,5 s)
  et échantillons perdus. `proc_max_us` : durée maximale d'un `push()` du cœur (rafale FFT + traitement
  de 0,5 s de signal). `cpu` : `AudioProcessorUsageMax()` (interruption audio seule).

---

## 6. Points de couplage avec l'algorithme

| Donnée | Source dans le cœur (`Rscw`) | Utilisée pour |
|---|---|---|
| module `bf` à l'instant T | `magAt(T)` | trace jaune, colonne `Power`, sortie `MAG` |
| seuil et bit « mou » `bt` | `bitAt(T, &soft, &thr)` | traces rouge et magenta, LED, `Detect`, sorties `THRESH`/`BIT` |
| broches 14/15 à l'instant T | `AudioRSCW_F32::pinsAt(T)` | traces verte/cyan, synchro de capture |
| retard d'affichage | `scopeDelay()` | instant T |
| caractères décodés | rappel `onChar` (= `decision()` de rscw.c) | zone texte, `log on` |
| porteuse | `carrier()`, `carrierFound()` | `freq`, sorties `I_BB`/`Q_BB` |
