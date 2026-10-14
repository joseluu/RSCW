/*
  RSCW_Teensy — décodeur CW RSCW (PA3FWM) sur Teensy 4.0 + Audio Shield.

  Algorithme : portage de RSCW 0.1e, https://www.pa3fwm.nl/software/rscw/
  (Copyright 2001,2011,2020 Pieter-Tjerk de Boer, PA3FWM, GPL v2), voir rscw_core.cpp.
  Harnais (écran, oscilloscope, encodeur, commandes série) : repris de
  joseluu/Morse_decode_coherent (HARNAIS.md), adapté à RSCW — voir HARNAIS.md de ce dépôt.

  Distribué sous les conditions de la GNU General Public License, version 2.
*/

#include <Arduino.h>
#include <OpenAudio_ArduinoLibrary.h>
#include <Audio.h>
#include <SPI.h>
#include <EncoderTool.h>
#include <utility/imxrt_hw.h>
#include <Wire.h>
#include <output_i2s_f32.h>
#include <input_i2s_f32.h>
#include <AudioStream_F32.h>
#include "ILI9341_t3n.h"
#include "ili9341_t3n_font_Arial.h"
#include <Bounce2.h>
#include "AudioRSCW_F32.h"
#include "AudioMixer11_F32.h"
#include "AudioSignalGenerator_F32.h"

#define VERSION "0.1.0 2026-10-14 RSCW 0.1e"
#define AUTEUR " RSCW de PA3FWM"

//------------------------------- affichage
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define DECODE_AREA_Y      0
#define DECODE_AREA_HEIGHT 36       // 2 lignes Arial_14
#define MENU_AREA_Y        38
#define MENU_LINE_HEIGHT   22
#define VERSION_LINE_Y     222

// Oscilloscope
#define SCOPE_AREA_Y       (MENU_AREA_Y + 5 * MENU_LINE_HEIGHT)
#define SCOPE_AREA_HEIGHT  (VERSION_LINE_Y - SCOPE_AREA_Y)
#define SCOPE_MS_PER_PIXEL 10
#define SCOPE_WIPE_AHEAD   10
#define SCOPE_X_OFFSET     20
#define SCOPE_WIDTH        (SCREEN_WIDTH - SCOPE_X_OFFSET)
#define PIN_INPUT_1        RSCW_PIN_SYNC    // 14
#define PIN_INPUT_2        RSCW_PIN_TONE    // 15
#define SCOPE_SYNC_HIGH    (SCOPE_AREA_Y + 5)
#define SCOPE_SYNC_LOW     (SCOPE_SYNC_HIGH + 10)
#define SCOPE_DETECT_HIGH  (SCOPE_SYNC_LOW + 10)
#define SCOPE_DETECT_LOW   (SCOPE_DETECT_HIGH + 10)
#define SCOPE_POWER_TOP    (SCOPE_DETECT_LOW + 5)
#define SCOPE_POWER_BOT    (VERSION_LINE_Y - 8)

int posH = 10;
int posV = DECODE_AREA_Y + 2;

// État de l'oscilloscope. Toutes les traces sont lues à l'instant audio T = maintenant - retard,
// retard = rscw.core().scopeDelay() : RSCW ne décide un bit que ~2 à 6 s après l'avoir reçu.
int scopeX = 0;
unsigned long scopeLastUpdate = 0;
bool scopePin14Prev = false;
bool scopePin14Level = false;
bool scopePin15Level = false;
bool scopeDetectLevel = false;
float scopePowerPrev = 0.0f;
float scopeThrPrev = 0.0f;
float scopeRunMax = 1e-9f;
bool scopeActive = false;
bool scopeSerialRequested = false;
bool scopeSerialActive = false;
unsigned long scopeBarFlashTime = 0;

// Valeurs retardées courantes (calculées par sampleDelayed())
struct Delayed { bool pin14, pin15, bit, valid; float mag, thr; };
Delayed dl = {false, false, false, false, 0, 0};

// Ligne de commande série
#define CMD_BUF_SIZE 64
char cmdBuffer[CMD_BUF_SIZE];
int cmdIndex = 0;

bool logOn = false;

//------------------------------- menu
#define NUM_OUTPUTS 2
#define NUM_MENU_ROWS 6
#define NUM_GAIN_VALUES 3
#define NUM_SIGGEN_MODES 7

const char* menuRowNames[5] = {
  "Out Left",
  "Out Right",
  "Gain",
  "Sig Gen",
  "WPM"
};

// sources des sorties ligne : 6 sorties de AudioRSCW_F32 + entrée brute + générateur interne
#define NUM_SOURCES    8
#define INPUT_SRC      6
#define SIGNAL_INT_SRC 7
const char* sourceNames[NUM_SOURCES] = {
  "MAG",
  "I_BB",
  "Q_BB",
  "THRESH",
  "BIT",
  "DECIM",
  "INPUT",
  "SIG INT"
};

int outputSourceSel[NUM_OUTPUTS] = { RSCW_OUT_I, RSCW_OUT_MAG };

int gainSelIdx = 1;
float gainValues[NUM_GAIN_VALUES] = { 0.1f, 1.0f, 10.0f };
const char* gainNames[NUM_GAIN_VALUES] = { "0.1", "1.0", "10.0" };

int sigGenSelIdx = 0;

int menuSelectedRow = 0;
bool menuEditMode = false;

#define WPM_DEFAULT 20.0f
#define WPM_STEP    0.5f

//------------------------------- matériel
#define TFT_DC      2
#define TFT_CS      10
#define TFT_RST     1
#define TFT_MOSI    11
#define TFT_SCLK    13
#define TFT_MISO    12
ILI9341_t3n tft = ILI9341_t3n(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

volatile int8_t Pos_enc = 0;
volatile int8_t old_Pos_enc = 0;
#define TUNESW 4
Bounce2::Button tunesw = Bounce2::Button();
using namespace EncoderTool;
Encoder encoder;

const short LED = 5;

//------------------------------- chaîne audio (F32, OpenAudio_ArduinoLibrary)
AudioInputI2S_F32            I2s1;
AudioOutputI2S_F32           I2s2;
AudioRSCW_F32                rscw;
AudioSignalGenerator_F32     sigGen;
AudioMixer11_F32             outputSelector[NUM_OUTPUTS];

// Entrée ligne droite -> RSCW
AudioConnection_F32 patchCord1(I2s1, 1, rscw, 0);

AudioConnection_F32 cR00(rscw, RSCW_OUT_MAG,    outputSelector[0], RSCW_OUT_MAG);
AudioConnection_F32 cR01(rscw, RSCW_OUT_I,      outputSelector[0], RSCW_OUT_I);
AudioConnection_F32 cR02(rscw, RSCW_OUT_Q,      outputSelector[0], RSCW_OUT_Q);
AudioConnection_F32 cR03(rscw, RSCW_OUT_THRESH, outputSelector[0], RSCW_OUT_THRESH);
AudioConnection_F32 cR04(rscw, RSCW_OUT_BIT,    outputSelector[0], RSCW_OUT_BIT);
AudioConnection_F32 cR05(rscw, RSCW_OUT_DECIM,  outputSelector[0], RSCW_OUT_DECIM);
AudioConnection_F32 cR10(rscw, RSCW_OUT_MAG,    outputSelector[1], RSCW_OUT_MAG);
AudioConnection_F32 cR11(rscw, RSCW_OUT_I,      outputSelector[1], RSCW_OUT_I);
AudioConnection_F32 cR12(rscw, RSCW_OUT_Q,      outputSelector[1], RSCW_OUT_Q);
AudioConnection_F32 cR13(rscw, RSCW_OUT_THRESH, outputSelector[1], RSCW_OUT_THRESH);
AudioConnection_F32 cR14(rscw, RSCW_OUT_BIT,    outputSelector[1], RSCW_OUT_BIT);
AudioConnection_F32 cR15(rscw, RSCW_OUT_DECIM,  outputSelector[1], RSCW_OUT_DECIM);

AudioConnection_F32 cINP0(I2s1, 1, outputSelector[0], INPUT_SRC);
AudioConnection_F32 cINP1(I2s1, 1, outputSelector[1], INPUT_SRC);
AudioConnection_F32 cSIG0(sigGen, 0, outputSelector[0], SIGNAL_INT_SRC);
AudioConnection_F32 cSIG1(sigGen, 0, outputSelector[1], SIGNAL_INT_SRC);

AudioConnection_F32 connectSel0(outputSelector[0], 0, I2s2, 1);   // Out Left
AudioConnection_F32 connectSel1(outputSelector[1], 0, I2s2, 0);   // Out Right

AudioControlSGTL5000 sgtl5000_1;

//------------------------------- prototypes
void handleEncoder(int direction);
void handleButton();
void drawMenu();
void drawMenuRow(int row);
void drawScopeBar();
void displayVersion();
void sendToTFT1(char character);
int setI2SFreq(int freq);

//------------------------------- sélection des sorties
void selectOutputChannel(int outputIdx, int newSourceIdx) {
  for (int i = 0; i < NUM_SOURCES; i++) outputSelector[outputIdx].gain(i, 0.0f);
  outputSelector[outputIdx].gain(newSourceIdx, 1.0f);
}

void applyAllOutputSelections() {
  for (int i = 0; i < NUM_OUTPUTS; i++) selectOutputChannel(i, outputSourceSel[i]);
}

//------------------------------- caractères décodés (rappel du cœur RSCW)
void onDecodedChar(char c, void*) {
  if (logOn) Serial.print(c);
  sendToTFT1(c);
}

void resetDecoder() {
  rscw.resetDecoder();
  scopeRunMax = 1e-9f;
}

void applyWpm(float w) {
  rscw.core().setWpm(w);
  resetDecoder();
}

//------------------------------------------------------------ setup()
void setup() {
  Serial.begin(115200);

  sgtl5000_1.enable();
  AudioMemory(20);
  AudioMemory_F32(200);
  sgtl5000_1.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000_1.volume(0.6);
  sgtl5000_1.lineInLevel(0);          // 3,12 Vpp max
  sgtl5000_1.lineOutLevel(13);

  setI2SFreq(AudioRSCW_F32::FS_IN);   // 48 kHz : décimation exacte par 6 vers les 8 kHz de RSCW

  applyAllOutputSelections();
  I2s2.setGain(gainValues[gainSelIdx]);
  sigGen.setSampleRate(AudioRSCW_F32::FS_IN);
  sigGen.setMode(sigGenSelIdx);

  encoder.begin(3, 0);
  tunesw.attach(TUNESW, INPUT_PULLUP);
  tunesw.interval(5);
  tunesw.setPressedState(LOW);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  pinMode(PIN_INPUT_1, INPUT);
  pinMode(PIN_INPUT_2, INPUT);

  // décodeur : 20 WPM, porteuse « strongest » sur toute la bande (comme rscw sans option)
  rscw.core().setWpm(WPM_DEFAULT);
  rscw.core().setFreqMode(Rscw::STRONGEST);
  rscw.core().setRange(1, Rscw::SAMP / 2 - 1);
  rscw.begin(onDecodedChar, nullptr);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(30, 7);
  tft.setTextColor(ILI9341_ORANGE);
  tft.setFont(Arial_14);
  tft.print("decodeur Morse ");
  tft.setCursor(30, 50);
  tft.print(VERSION);
  tft.setCursor(30, 93);
  tft.print(AUTEUR);
  delay(3000);
  tft.fillScreen(ILI9341_BLACK);
  drawMenu();
  displayVersion();
  resetDecoder();                     // ignore l'audio accumulé pendant l'écran d'accueil
  rscw.resetStats();                  // (et les pertes de file correspondantes)
}

// ----------------------------
// Oscilloscope
// ----------------------------
// Lit toutes les grandeurs à l'instant audio retardé T (horloge 8 kHz du décodeur).
void sampleDelayed() {
  Rscw &d = rscw.core();
  int T = d.time() - d.scopeDelay();
  float soft = 0, thr = 0;
  dl.valid = (T > 0) && d.bitAt(T, &soft, &thr);
  uint8_t p = (T > 0) ? rscw.pinsAt(T) : 0;
  dl.pin14 = p & 1;
  dl.pin15 = (p & 2) != 0;
  dl.mag = (T > 0) ? d.magAt(T) : 0;
  dl.thr = dl.valid ? thr : 0;
  dl.bit = dl.valid && soft > 0;
  rscw.setDelayedOutputs(dl.mag, dl.thr, dl.bit);
  digitalWrite(LED, dl.bit ? HIGH : LOW);
}

float scopeLevel(float v) {
  scopeRunMax = fmaxf(v, scopeRunMax * 0.9995f);
  return v / scopeRunMax;
}

void updateOscilloscope() {
  drawScopeBar();

  bool pin14Now = dl.pin14;
  bool freeRunRestart = scopeActive && scopeX >= SCOPE_WIDTH && !scopeSerialRequested;
  if ((pin14Now && !scopePin14Prev && scopeSerialRequested) || freeRunRestart || !scopeActive) {
    scopeSerialActive = false;
    scopeX = 0;
    scopeActive = true;
    scopeLastUpdate = millis();
    scopePin14Level = pin14Now;
    scopePin15Level = dl.pin15;
    scopeDetectLevel = dl.bit;
    scopePowerPrev = scopeLevel(dl.mag);
    scopeThrPrev = 0.0f;
    if (scopeSerialRequested) {
      scopeSerialRequested = false;
      scopeSerialActive = true;
      Serial.println("Time\tSync\tTone\tDetect\tPower");
    }
  }
  scopePin14Prev = pin14Now;

  if (!scopeActive) return;
  if (scopeX >= SCOPE_WIDTH) {
    scopeSerialActive = false;
    return;
  }

  unsigned long now = millis();
  if (now - scopeLastUpdate < SCOPE_MS_PER_PIXEL) return;
  scopeLastUpdate = now;

  bool newPin14 = dl.pin14;
  bool newPin15 = dl.pin15;
  bool newDetect = dl.bit;
  float newPower = dl.mag;
  float newLevel = scopeLevel(newPower);

  if (scopeSerialActive) {
    Serial.print(now);
    Serial.print('\t');
    Serial.print(newPin14 ? 1 : 0);
    Serial.print('\t');
    Serial.print(newPin15 ? 1 : 0);
    Serial.print('\t');
    Serial.print(newDetect ? 1 : 0);
    Serial.print('\t');
    Serial.println(newPower, 5);
  }

  int screenX = scopeX + SCOPE_X_OFFSET;
  int wipeEnd = min(screenX + SCOPE_WIPE_AHEAD, SCREEN_WIDTH);
  if (screenX < wipeEnd) {
    tft.fillRect(screenX, SCOPE_AREA_Y, wipeEnd - screenX, SCOPE_AREA_HEIGHT, ILI9341_BLACK);
  }

  int y14 = newPin14 ? SCOPE_SYNC_HIGH : SCOPE_SYNC_LOW;
  tft.drawPixel(screenX, y14, ILI9341_GREEN);
  if (newPin14 != scopePin14Level) {
    tft.drawFastVLine(screenX, SCOPE_SYNC_HIGH, SCOPE_SYNC_LOW - SCOPE_SYNC_HIGH + 1, ILI9341_GREEN);
  }

  int y15 = newPin15 ? SCOPE_SYNC_HIGH : SCOPE_SYNC_LOW;
  tft.drawPixel(screenX, y15, ILI9341_CYAN);
  if (newPin15 != scopePin15Level) {
    tft.drawFastVLine(screenX, SCOPE_SYNC_HIGH, SCOPE_SYNC_LOW - SCOPE_SYNC_HIGH + 1, ILI9341_CYAN);
  }

  int yDet = newDetect ? SCOPE_DETECT_HIGH : SCOPE_DETECT_LOW;
  tft.drawPixel(screenX, yDet, ILI9341_MAGENTA);
  if (newDetect != scopeDetectLevel) {
    tft.drawFastVLine(screenX, SCOPE_DETECT_HIGH, SCOPE_DETECT_LOW - SCOPE_DETECT_HIGH + 1, ILI9341_MAGENTA);
  }

  int powerRange = SCOPE_POWER_BOT - SCOPE_POWER_TOP;
  // seuil RSCW (rouge), même échelle que le module (jaune) ; absent tant qu'aucun bit n'est décidé
  float thrNow = dl.thr;
  float thrLevel = thrNow / scopeRunMax;
  int yThr = constrain(SCOPE_POWER_BOT - (int)(thrLevel * powerRange), SCOPE_POWER_TOP, SCOPE_POWER_BOT);
  int yThrPrev = constrain(SCOPE_POWER_BOT - (int)(scopeThrPrev * powerRange), SCOPE_POWER_TOP, SCOPE_POWER_BOT);
  if (thrNow > 0.0f) {
    tft.drawPixel(screenX, yThr, ILI9341_RED);
    if (yThr != yThrPrev && scopeThrPrev > 0.0f) {
      tft.drawFastVLine(screenX, min(yThr, yThrPrev), abs(yThr - yThrPrev) + 1, ILI9341_RED);
    }
  }
  scopeThrPrev = thrLevel;

  int yPow = constrain(SCOPE_POWER_BOT - (int)(newLevel * powerRange), SCOPE_POWER_TOP, SCOPE_POWER_BOT);
  int yPowPrev = constrain(SCOPE_POWER_BOT - (int)(scopePowerPrev * powerRange), SCOPE_POWER_TOP, SCOPE_POWER_BOT);
  tft.drawPixel(screenX, yPow, ILI9341_YELLOW);
  if (yPow != yPowPrev) {
    int yMin = min(yPow, yPowPrev);
    int yMax = max(yPow, yPowPrev);
    tft.drawFastVLine(screenX, yMin, yMax - yMin + 1, ILI9341_YELLOW);
  }

  scopePin14Level = newPin14;
  scopePin15Level = newPin15;
  scopeDetectLevel = newDetect;
  scopePowerPrev = newLevel;

  scopeX++;
}

// ----------------------------
// Langage de commandes série
// ----------------------------
int findSourceIndex(const char* name) {
  for (int i = 0; i < NUM_SOURCES; i++) {
    if (strcmp(name, sourceNames[i]) == 0) return i;
  }
  return -1;
}

const char* fmodeName() {
  Rscw &d = rscw.core();
  if (d.fmin() == d.fmax()) return "fixed";
  return d.freqMode() == Rscw::TRACK ? "track" : "strongest";
}

void cmdHelp() {
  Serial.println("Commands:");
  Serial.println("  help                  - show this help");
  Serial.println("  status                - show current settings");
  Serial.println("  reset                 - reset the RSCW decoder (keeps settings)");
  Serial.println("  log on|off            - echo decoded characters on serial");
  Serial.println("  peak | peak reset     - ADC input peak");
  Serial.println("  scope                 - output next sweep as CSV (synced on pin 14)");
  Serial.println("  freq                  - estimated carrier frequency");
  Serial.println("  set out<N> <source>   - set output (0-1) to source");
  Serial.println("  set gain <value>      - set gain (0.1, 1.0, 10.0)");
  Serial.println("  set siggen <mode>     - set signal gen (0-6)");
  Serial.println("  set wpm <5..60>       - morse speed (RSCW needs it; resets the decoder)");
  Serial.println("  set fmode strongest|track - carrier search mode (as rscw -s / -t)");
  Serial.println("  set freq <Hz>         - fixed carrier frequency (as rscw -f Hz)");
  Serial.println("  set range <fmin> <fmax> - carrier search range (as rscw -f fmin-fmax)");
  Serial.print("Sources:");
  for (int i = 0; i < NUM_SOURCES; i++) {
    Serial.print(' ');
    Serial.print(sourceNames[i]);
  }
  Serial.println();
  Serial.print("SigGen modes:");
  for (int i = 0; i < NUM_SIGGEN_MODES; i++) {
    Serial.print(' ');
    Serial.print(i);
    Serial.print('=');
    Serial.print(AudioSignalGenerator_F32::getModeName(i));
  }
  Serial.println();
}

void cmdStatus() {
  Rscw &d = rscw.core();
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    Serial.print(menuRowNames[i]);
    Serial.print(": ");
    Serial.println(sourceNames[outputSourceSel[i]]);
  }
  Serial.print("Gain: ");
  Serial.println(gainNames[gainSelIdx]);
  Serial.print("Sig Gen: ");
  Serial.println(AudioSignalGenerator_F32::getModeName(sigGenSelIdx));
  Serial.print("WPM: ");
  Serial.println(d.wpm(), 1);
  Serial.print("Decoder: RSCW ");
  Serial.println(VERSION);
  Serial.printf("wpm=%.1f fmode=%s fmin=%.1f fmax=%.1f log=%d\n", d.wpm(), fmodeName(), d.fmin(), d.fmax(), logOn ? 1 : 0);
  Serial.printf("carrier=%.1f found=%d\n", d.carrier(), d.carrierFound() ? 1 : 0);
  Serial.printf("dot=%.1fms delay=%.0fms chars=%lu msgs=%d msgmax=%d msgovf=%lu\n",
                d.dotSamples() / 8.0f, d.scopeDelay() / 8.0f, (unsigned long)d.charCount(),
                d.msgCount(), d.msgHighWater(), (unsigned long)d.msgOverflows());
  Serial.printf("fs_i2s=%d decim=%d queue_max=%u queue_drop=%lu proc_max_us=%lu cpu=%.1f%% mem_f32_max=%d\n",
                AudioRSCW_F32::FS_IN, AudioRSCW_F32::DECIM, rscw.queueHighWater(),
                (unsigned long)rscw.queueDropped(), (unsigned long)rscw.procMaxUs(),
                AudioProcessorUsageMax(), AudioMemoryUsageMax_F32());
}

void executeCommand(char* cmd) {
  while (*cmd == ' ') cmd++;
  int len = strlen(cmd);
  while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\r' || cmd[len-1] == '\n')) {
    cmd[--len] = '\0';
  }
  if (len == 0) return;
  Rscw &d = rscw.core();
  if (strcmp(cmd, "help") == 0) {
    cmdHelp();
  } else if (strcmp(cmd, "status") == 0) {
    cmdStatus();
  } else if (strcmp(cmd, "reset") == 0) {
    resetDecoder();
    Serial.println("reset ok");
  } else if (strncmp(cmd, "log ", 4) == 0) {
    logOn = (strcmp(cmd + 4, "on") == 0);
    Serial.println(logOn ? "log on" : "log off");
  } else if (strcmp(cmd, "peak reset") == 0) {
    rscw.resetPeak();
    rscw.resetStats();
    Serial.println("peak reset");
  } else if (strcmp(cmd, "peak") == 0) {
    Serial.printf("ADC peak max=%.5f min=%.5f\n", rscw.peakMax(), -rscw.peakMin());
  } else if (strcmp(cmd, "scope") == 0) {
    scopeSerialRequested = true;
    Serial.println("Scope serial requested");
  } else if (strcmp(cmd, "freq") == 0) {
    Serial.printf("freq: %.1f found=%d\n", d.carrier(), d.carrierFound() ? 1 : 0);
  } else if (strncmp(cmd, "set ", 4) == 0) {
    char* arg = cmd + 4;
    while (*arg == ' ') arg++;
    if (strncmp(arg, "out", 3) == 0) {
      int outIdx = arg[3] - '0';
      if (outIdx < 0 || outIdx >= NUM_OUTPUTS) {
        Serial.println("Error: output index 0-1");
        return;
      }
      char* srcName = arg + 4;
      while (*srcName == ' ') srcName++;
      int srcIdx = findSourceIndex(srcName);
      if (srcIdx < 0) {
        Serial.print("Error: unknown source '");
        Serial.print(srcName);
        Serial.println("'");
        return;
      }
      outputSourceSel[outIdx] = srcIdx;
      selectOutputChannel(outIdx, srcIdx);
      drawMenu();
      Serial.print(menuRowNames[outIdx]);
      Serial.print(" -> ");
      Serial.println(sourceNames[srcIdx]);
    } else if (strncmp(arg, "gain ", 5) == 0) {
      float val = atof(arg + 5);
      bool found = false;
      for (int i = 0; i < NUM_GAIN_VALUES; i++) {
        if (fabs(val - gainValues[i]) < 0.01f) {
          gainSelIdx = i;
          I2s2.setGain(gainValues[gainSelIdx]);
          drawMenu();
          Serial.print("Gain = ");
          Serial.println(gainNames[gainSelIdx]);
          found = true;
          break;
        }
      }
      if (!found) Serial.println("Error: gain must be 0.1, 1.0, or 10.0");
    } else if (strncmp(arg, "siggen ", 7) == 0) {
      int mode = atoi(arg + 7);
      if (mode >= 0 && mode < NUM_SIGGEN_MODES) {
        sigGenSelIdx = mode;
        sigGen.setMode(sigGenSelIdx);
        drawMenu();
        Serial.print("Sig Gen = ");
        Serial.println(AudioSignalGenerator_F32::getModeName(sigGenSelIdx));
      } else {
        Serial.println("Error: siggen mode 0-6");
      }
    } else if (strncmp(arg, "wpm ", 4) == 0) {
      float w = atof(arg + 4);
      if (w >= Rscw::WPM_MIN && w <= Rscw::WPM_MAX) {
        applyWpm(w);
        drawMenu();
        Serial.printf("wpm = %.1f (decoder reset)\n", d.wpm());
      } else {
        Serial.println("Error: wpm must be 5..60");
      }
    } else if (strncmp(arg, "fmode ", 6) == 0) {
      const char* m = arg + 6;
      if (strcmp(m, "strongest") == 0 || strcmp(m, "track") == 0) {
        d.setFreqMode(strcmp(m, "track") == 0 ? Rscw::TRACK : Rscw::STRONGEST);
        if (d.fmin() == d.fmax()) d.setRange(1, Rscw::SAMP / 2 - 1);   // quitte le mode fixe
        resetDecoder();
        Serial.printf("fmode = %s\n", fmodeName());
      } else {
        Serial.println("Error: fmode strongest|track");
      }
    } else if (strncmp(arg, "freq ", 5) == 0) {
      float f = atof(arg + 5);
      if (f > 0 && f < Rscw::SAMP / 2) {
        d.setRange(f, f);
        resetDecoder();
        Serial.printf("freq = %.1f (fixed)\n", f);
      } else {
        Serial.println("Error: invalid value");
      }
    } else if (strncmp(arg, "range ", 6) == 0) {
      float a = 0, b = 0;
      if (sscanf(arg + 6, "%f %f", &a, &b) == 2 && a >= 0 && b > a && b < Rscw::SAMP / 2) {
        d.setRange(a, b);
        resetDecoder();
        Serial.printf("range = %.1f %.1f\n", a, b);
      } else {
        Serial.println("Error: invalid value");
      }
    } else {
      Serial.println("Error: unknown set parameter");
    }
  } else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
    Serial.println("Type 'help' for commands");
  }
}

void processSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdIndex > 0) {
        cmdBuffer[cmdIndex] = '\0';
        executeCommand(cmdBuffer);
        cmdIndex = 0;
      }
    } else {
      if (cmdIndex < CMD_BUF_SIZE - 1) {
        cmdBuffer[cmdIndex++] = c;
      } else {
        cmdIndex = 0;
      }
    }
  }
}

// ----------------------------
// loop()
// ----------------------------
void loop() {
  rscw.process();                     // décodage RSCW des échantillons reçus (caractères -> TFT/série)
  sampleDelayed();
  if (encoder.valueChanged()) {
    Pos_enc = encoder.getValue();
    int direction = (Pos_enc > old_Pos_enc) ? 1 : -1;
    old_Pos_enc = Pos_enc;
    handleEncoder(direction);
  }
  tunesw.update();
  if (tunesw.pressed()) {
    handleButton();
  }
  updateOscilloscope();
  processSerialInput();
}

/***********************************encodeur et menu*****************************************************/
void handleEncoder(int direction) {
  if (!menuEditMode) {
    menuSelectedRow += direction;
    if (menuSelectedRow < 0) menuSelectedRow = NUM_MENU_ROWS - 1;
    if (menuSelectedRow >= NUM_MENU_ROWS) menuSelectedRow = 0;
  } else {
    if (menuSelectedRow < NUM_OUTPUTS) {
      int src = outputSourceSel[menuSelectedRow] + direction;
      if (src < 0) src = NUM_SOURCES - 1;
      if (src >= NUM_SOURCES) src = 0;
      outputSourceSel[menuSelectedRow] = src;
      selectOutputChannel(menuSelectedRow, src);
    } else if (menuSelectedRow == 2) {
      gainSelIdx += direction;
      if (gainSelIdx < 0) gainSelIdx = NUM_GAIN_VALUES - 1;
      if (gainSelIdx >= NUM_GAIN_VALUES) gainSelIdx = 0;
      I2s2.setGain(gainValues[gainSelIdx]);
    } else if (menuSelectedRow == 3) {
      sigGenSelIdx += direction;
      if (sigGenSelIdx < 0) sigGenSelIdx = NUM_SIGGEN_MODES - 1;
      if (sigGenSelIdx >= NUM_SIGGEN_MODES) sigGenSelIdx = 0;
      sigGen.setMode(sigGenSelIdx);
    } else if (menuSelectedRow == 4) {
      // WPM : pas de 0,5, borné (pas de bouclage) ; chaque cran réinitialise le décodeur
      float w = rscw.core().wpm() + direction * WPM_STEP;
      if (w < Rscw::WPM_MIN) w = Rscw::WPM_MIN;
      if (w > Rscw::WPM_MAX) w = Rscw::WPM_MAX;
      applyWpm(w);
    }
  }
  drawMenu();
  delay(80);
}

void handleButton() {
  if (menuSelectedRow == 5) {
    scopeSerialRequested = true;
    scopeBarFlashTime = millis();
    drawScopeBar();
    return;
  }
  menuEditMode = !menuEditMode;
  drawMenu();
}

//------------------------------------- affichage
void drawMenuRow(int row) {
  int y = MENU_AREA_Y + row * MENU_LINE_HEIGHT;
  bool isSelected = (row == menuSelectedRow);
  tft.setFont(Arial_14);
  tft.fillRect(0, y, SCREEN_WIDTH, MENU_LINE_HEIGHT, ILI9341_BLACK);
  if (isSelected) {
    tft.fillRect(0, y, 100, MENU_LINE_HEIGHT, menuEditMode ? ILI9341_YELLOW : ILI9341_WHITE);
    tft.setTextColor(ILI9341_BLACK);
  } else {
    tft.setTextColor(ILI9341_ORANGE, ILI9341_BLACK);
  }
  tft.setCursor(4, y + 3);
  tft.print(menuRowNames[row]);
  if (isSelected && menuEditMode) {
    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  } else {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  }
  tft.setCursor(110, y + 3);
  if (row < NUM_OUTPUTS) {
    tft.print(sourceNames[outputSourceSel[row]]);
  } else if (row == 2) {
    tft.print(gainNames[gainSelIdx]);
  } else if (row == 3) {
    tft.print(AudioSignalGenerator_F32::getModeName(sigGenSelIdx));
  } else if (row == 4) {
    tft.print(rscw.core().wpm(), 1);
  }
}

void drawMenu() {
  for (int i = 0; i < 5; i++) drawMenuRow(i);
  drawScopeBar();
}

void drawScopeBar() {
  if (menuSelectedRow == 5) {
    uint16_t color = (millis() - scopeBarFlashTime < 200) ? ILI9341_YELLOW : ILI9341_WHITE;
    tft.fillRect(0, SCOPE_AREA_Y, SCOPE_X_OFFSET, SCOPE_AREA_HEIGHT, color);
  } else {
    tft.fillRect(0, SCOPE_AREA_Y, SCOPE_X_OFFSET, SCOPE_AREA_HEIGHT, ILI9341_BLACK);
  }
}

void displayVersion() {
  tft.setFont(Arial_14);
  tft.fillRect(0, VERSION_LINE_Y, SCREEN_WIDTH, 18, ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(4, VERSION_LINE_Y);
  tft.print("CW decod. ");
  tft.print(VERSION);
}

void sendToTFT1(char character) {
  #define MAX_DECODE_H (SCREEN_WIDTH - 20)
  #define MAX_DECODE_V (DECODE_AREA_Y + DECODE_AREA_HEIGHT - 20)
  posH = posH + 15;
  if (posH > MAX_DECODE_H) {
    posH = 10;
    posV = posV + 20;
  }
  if (posV > MAX_DECODE_V) {
    posH = 10;
    posV = DECODE_AREA_Y + 2;
    tft.fillRect(0, DECODE_AREA_Y, SCREEN_WIDTH, DECODE_AREA_HEIGHT, ILI9341_BLACK);
  }
  tft.setFont(Arial_14);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(posH, posV);
  tft.print(character);
}

// Teensy 4.0 : fréquence I2S (repris de Morse_decode_coherent)
int setI2SFreq(int freq) {
  int n1;
  int n2;
  int c0;
  int c2;
  int c1;
  double C;
  // PLL between 27*24 = 648MHz und 54*24=1296MHz
  if (freq > 8000) {
    n1 = 4;
  } else {
    n1 = 8;
  }
  n2 = 1 + (24000000 * 27) / (freq * 256 * n1);
  if (n2 > 63) {
    return 0;
  }
  C = ((double)freq * 256 * n1 * n2) / 24000000;
  c0 = C;
  c2 = 10000;
  c1 = C * c2 - (c0 * c2);
  set_audioClock(c0, c1, c2, true);
  CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI1_CLK_PRED_MASK | CCM_CS1CDR_SAI1_CLK_PODF_MASK))
               | CCM_CS1CDR_SAI1_CLK_PRED(n1 - 1)
               | CCM_CS1CDR_SAI1_CLK_PODF(n2 - 1);
  CCM_CS2CDR = (CCM_CS2CDR & ~(CCM_CS2CDR_SAI2_CLK_PRED_MASK | CCM_CS2CDR_SAI2_CLK_PODF_MASK))
               | CCM_CS2CDR_SAI2_CLK_PRED(n1 - 1)
               | CCM_CS2CDR_SAI2_CLK_PODF(n2 - 1);
  return freq;
}
