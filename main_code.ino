#include <Adafruit_NeoPixel.h>

// ==========================================
// KONFIGURASJON
// ==========================================
#define LED_PIN 6
#define LED_COUNT 60
#define LED2_PIN 3
#define LED2_COUNT 50
#define LED3_PIN 5
#define LED3_COUNT 30

#define HJERTE_PIN 2
#define BALLONG_PIN 3
#define BUZZER_PIN 10
#define TRIG_PIN 7
#define ECHO_PIN 8

// D6 segmenter
#define TOLV_ROD_START 0
#define TOLV_ROD_SLUTT 28
#define TOLV_BLAA_START 29
#define TOLV_BLAA_SLUTT 55

// D5 segmenter
#define LITEN_ROD_START 0
#define LITEN_ROD_SLUTT 21
#define LITEN_BLAA_START 22
#define LITEN_BLAA_SLUTT 43

// D4 HLR-veileder segmenter
#define LUFT_START 0
#define LUFT_SLUTT 4
#define LUFT_ANTALL 5
#define HJERTE_START 5
#define HJERTE_SLUTT 9
#define HJERTE_ANTALL 5

#define IDEAL_MS_MIN 500
#define IDEAL_MS_MAX 600
#define KVALITET_FORFALL 0.003
#define OKSYGEN_FORFALL_SAKTE 0.0003
#define OKSYGEN_FORFALL_RASK 0.002
#define OKSYGEN_FORFALL 0.001
#define KOMPRESJONER_NEDGANG 20
#define DEBOUNCE_MS 50
#define ANIMASJON_MS 40

#define OPPSTART_INNBLASNINGER 5
#define KOMPRESJONER_MAL 30
#define INNBLASNINGER_PR_RUNDE 2
#define RUNDER_FOR_SEIER 3

#define LUFT_FADE_INN 0.006
#define LUFT_FADE_UT 0.025

#define SEIER_VARIGHET_MS 30000UL

#define INNPUST_TERSKEL_CM 7

unsigned long ballongDebounce = 0;

// ==========================================
// BUZZER / STAYIN' ALIVE (passiv piezo)
// ==========================================
#define BPM 103
#define SLAG_MS (60000UL / BPM)

unsigned long nesteSlagh = 0;

// ==========================================
// TILSTANDER
// ==========================================
enum Tilstand { OPPSTART,
                KOMPRESJONER,
                VENTER_INNPUST,
                SEIER };
Tilstand tilstand = OPPSTART;

// ==========================================
// GLOBALE VARIABLER
// ==========================================
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ400);
Adafruit_NeoPixel strip2(LED2_COUNT, LED2_PIN, NEO_RGB + NEO_KHZ400);
Adafruit_NeoPixel strip3(LED3_COUNT, LED3_PIN, NEO_RGB + NEO_KHZ400);

float compressionQuality = 0.0;
float oxygenLevel = 0.5;

unsigned long lastCompressionTime = 0;
unsigned long lastAnimUpdate = 0;

bool hjerteForrige = HIGH;
unsigned long hjerteDebounce = 0;

bool hjertetrykk = false;
bool ballongtrykk = false;

float tolvRodPos = 0.0;
float tolvBlaaPos = 0.0;
float litenRodPos = 0.0;
float litenBlaaPos = 0.0;

int oppstartInnblasninger = 0;
int kompresjonTeller = 0;
int innblasningTeller = 0;
int rundeTeller = 0;

unsigned long blinkTimer = 0;
bool blinkState = false;

float luftFade[15] = { 0 };

// Seier-animasjon
unsigned long seierStartTid = 0;
float seierOrm = 0.0;
float seierPust = 0.0;
float seierPustFase = 0.0;

// Ultralyd
bool ballongForrigeNaer = false;

// ==========================================
// HJELPEFUNKSJONER
// ==========================================
float klemm(float v, float mn, float mx) {
  return v < mn ? mn : (v > mx ? mx : v);
}

void nullstill() {
  tilstand = OPPSTART;
  oppstartInnblasninger = 0;
  kompresjonTeller = 0;
  innblasningTeller = 0;
  rundeTeller = 0;
  compressionQuality = 0.0;
  oxygenLevel = 0.5;
  for (int i = 0; i < LUFT_ANTALL; i++) luftFade[i] = 0.0;
  nesteSlagh = millis();
  noTone(BUZZER_PIN);
  ballongForrigeNaer = false;
}

uint32_t lagOksygenFarge(Adafruit_NeoPixel &s, float oksygen, float styrke) {
  int blaa = (int)((1.0 - oksygen) * 255 * styrke);
  int rod = (int)(oksygen * 255 * styrke);
  return s.Color(blaa, rod, 0);
}

void settLED(Adafruit_NeoPixel &s, int i, uint32_t farge, float styrke) {
  uint8_t r = (farge >> 16) & 0xFF;
  uint8_t g = (farge >> 8) & 0xFF;
  uint8_t b = farge & 0xFF;
  s.setPixelColor(i, s.Color(
                       (uint8_t)(r * styrke),
                       (uint8_t)(g * styrke),
                       (uint8_t)(b * styrke)));
}

uint32_t hjerteFarge(float fremgang) {
  uint8_t rod = (uint8_t)((1.0 - fremgang) * 255);
  uint8_t gron = (uint8_t)(fremgang * 255);
  return strip3.Color(0, rod, gron);
}

// ==========================================
// ULTRALYD
// ==========================================
float lesAvstand() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long varighet = pulseIn(ECHO_PIN, HIGH, 30000);
  if (varighet == 0) return 999.0;
  return varighet * 0.0343 / 2.0;
}

// ==========================================
// LES KNAPPER
// ==========================================
void lesKnapper() {
  unsigned long na = millis();

  // Hjerteknapp (D2)
  bool hNaa = digitalRead(HJERTE_PIN);
  if (hNaa == LOW && hjerteForrige == HIGH && (na - hjerteDebounce > DEBOUNCE_MS)) {
    hjertetrykk = true;
    hjerteDebounce = na;
  } else {
    hjertetrykk = false;
  }
  hjerteForrige = hNaa;

  // Innpust via ultralyd
  float avstand = lesAvstand();
  bool ballongNaer = (avstand < INNPUST_TERSKEL_CM);
  if (ballongNaer && !ballongForrigeNaer && (na - ballongDebounce > 2000)) {
    ballongtrykk = true;
    ballongDebounce = na;
  } else {
    ballongtrykk = false;
  }
  ballongForrigeNaer = ballongNaer;
}

// ==========================================
// SEIER-LYD
// ==========================================
void spillSeierLyd() {
  tone(BUZZER_PIN, 784, 120);  // G5
  delay(130);
  tone(BUZZER_PIN, 784, 120);  // G5
  delay(130);
  tone(BUZZER_PIN, 784, 120);  // G5
  delay(130);
  tone(BUZZER_PIN, 1047, 400);  // C6
  delay(410);
  noTone(BUZZER_PIN);
}

// ==========================================
// BUZZER – STAYIN' ALIVE TAKT
// ==========================================
void oppdaterBuzzer() {
  unsigned long na = millis();

  if (hjertetrykk && tilstand == KOMPRESJONER) {
    tone(BUZZER_PIN, 1200, 60);
    nesteSlagh = na + SLAG_MS;
    return;
  }

  if (tilstand == KOMPRESJONER) {
    if (na >= nesteSlagh) {
      tone(BUZZER_PIN, 1000, 80);
      nesteSlagh = na + SLAG_MS;
    }
  } else {
    noTone(BUZZER_PIN);
    nesteSlagh = na;
  }
}

// ==========================================
// OPPDATER KVALITET OG OKSYGEN
// ==========================================
void oppdaterKvalitet() {
  if (tilstand == SEIER) {
    seierPustFase += 0.02;
    if (seierPustFase > TWO_PI) seierPustFase -= TWO_PI;
    oxygenLevel = 0.5 + 0.4 * sin(seierPustFase);
    compressionQuality = klemm(compressionQuality + 0.01, 0.0, 1.0);
    return;
  }

  if (hjertetrykk) {
    unsigned long na = millis();
    unsigned long intervall = na - lastCompressionTime;
    lastCompressionTime = na;

    float kvalitet;
    if (intervall >= IDEAL_MS_MIN && intervall <= IDEAL_MS_MAX) {
      kvalitet = 1.0;
    } else if (intervall < IDEAL_MS_MIN) {
      kvalitet = (float)intervall / IDEAL_MS_MIN;
    } else {
      kvalitet = klemm(1.0 - ((float)(intervall - IDEAL_MS_MAX) / IDEAL_MS_MAX), 0.0, 1.0);
    }
    compressionQuality = klemm(compressionQuality * 0.6 + kvalitet * 0.4, 0.0, 1.0);
  }
  compressionQuality -= KVALITET_FORFALL;
  compressionQuality = klemm(compressionQuality, 0.0, 1.0);

  if (ballongtrykk) {
    oxygenLevel = klemm(oxygenLevel + 0.4, 0.0, 1.0);
  }

  if (tilstand == KOMPRESJONER && kompresjonTeller >= KOMPRESJONER_NEDGANG) {
    float fremgang = (float)(kompresjonTeller - KOMPRESJONER_NEDGANG)
                     / (KOMPRESJONER_MAL - KOMPRESJONER_NEDGANG);
    float forfall = OKSYGEN_FORFALL_SAKTE
                    + (OKSYGEN_FORFALL_RASK - OKSYGEN_FORFALL_SAKTE) * fremgang;
    oxygenLevel -= forfall;
  } else {
    oxygenLevel -= OKSYGEN_FORFALL_SAKTE;
  }
  oxygenLevel = klemm(oxygenLevel, 0.0, 1.0);
}

// ==========================================
// HLR TILSTANDSMASKIN
// ==========================================
void oppdaterHLR() {
  if (tilstand == SEIER) {
    if (millis() - seierStartTid >= SEIER_VARIGHET_MS) {
      nullstill();
    }
    return;
  }

  switch (tilstand) {
    case OPPSTART:
      if (ballongtrykk) {
        oppstartInnblasninger++;
        if (oppstartInnblasninger >= OPPSTART_INNBLASNINGER) {
          tilstand = KOMPRESJONER;
          kompresjonTeller = 0;
          nesteSlagh = millis();
        }
      }
      break;

    case KOMPRESJONER:
      if (hjertetrykk) {
        kompresjonTeller++;
        if (kompresjonTeller >= KOMPRESJONER_MAL) {
          rundeTeller++;
          if (rundeTeller >= RUNDER_FOR_SEIER) {
            tilstand = SEIER;
            seierStartTid = millis();
            seierOrm = 0.0;
            seierPustFase = 0.0;
            compressionQuality = 1.0;
            nesteSlagh = millis();
            spillSeierLyd();
          } else {
            tilstand = VENTER_INNPUST;
            innblasningTeller = 0;
            blinkTimer = millis();
          }
        }
      }
      break;

    case VENTER_INNPUST:
      if (ballongtrykk) {
        innblasningTeller++;
        if (innblasningTeller >= INNBLASNINGER_PR_RUNDE) {
          tilstand = KOMPRESJONER;
          kompresjonTeller = 0;
          nesteSlagh = millis();
        }
      }
      break;

    default:
      break;
  }
}

// ==========================================
// OPPDATER LUFT-FADE
// ==========================================
void oppdaterLuftFade() {
  float maalFyll = 0.0;

  if (tilstand == SEIER) {
    maalFyll = 0.5 + 0.5 * sin(seierPustFase);
  } else if (tilstand == OPPSTART) {
    maalFyll = (float)oppstartInnblasninger / OPPSTART_INNBLASNINGER;
  } else if (tilstand == KOMPRESJONER) {
    maalFyll = 1.0 - ((float)kompresjonTeller / KOMPRESJONER_MAL);
  } else if (tilstand == VENTER_INNPUST) {
    maalFyll = (float)innblasningTeller / INNBLASNINGER_PR_RUNDE;
  }

  for (int i = 0; i < LUFT_ANTALL; i++) {
    float terskel = (float)(i + 1) / LUFT_ANTALL;
    float maal = (maalFyll >= terskel) ? 1.0 : 0.0;

    if (luftFade[i] < maal) {
      luftFade[i] += LUFT_FADE_INN;
    } else if (luftFade[i] > maal) {
      luftFade[i] -= LUFT_FADE_UT;
    }
    luftFade[i] = klemm(luftFade[i], 0.0, 1.0);
  }
}

// ==========================================
// BLODSTRØM (D6 og D5)
// ==========================================
void tegnSeksjon(Adafruit_NeoPixel &s,
                 int ledStart, int ledSlutt,
                 float &pos, float hastighet,
                 uint32_t farge) {
  int lengde = ledSlutt - ledStart + 1;
  pos += hastighet;
  if (pos >= lengde) pos -= lengde;

  for (int i = 0; i < lengde; i++) {
    float avstand = abs((float)i - pos);
    if (avstand > lengde / 2.0) avstand = lengde - avstand;
    float pulsStyrke = exp(-avstand * avstand * 0.4);
    float total = klemm(0.08 + pulsStyrke * compressionQuality, 0.0, 1.0);
    settLED(s, ledStart + i, farge, total);
  }
}

void tegnBlodstrips() {
  float hastighet = 0.05 + compressionQuality * 1.5;
  uint32_t oksFargeD6 = lagOksygenFarge(strip, oxygenLevel, 1.0);
  uint32_t oksFargeD5 = lagOksygenFarge(strip2, oxygenLevel, 1.0);

  tegnSeksjon(strip, TOLV_ROD_START, TOLV_ROD_SLUTT,
              tolvRodPos, hastighet, oksFargeD6);
  tegnSeksjon(strip, TOLV_BLAA_START, TOLV_BLAA_SLUTT,
              tolvBlaaPos, hastighet, strip.Color(255, 0, 0));

  tegnSeksjon(strip2, LITEN_ROD_START, LITEN_ROD_SLUTT,
              litenRodPos, hastighet, oksFargeD5);
  tegnSeksjon(strip2, LITEN_BLAA_START, LITEN_BLAA_SLUTT,
              litenBlaaPos, hastighet, strip2.Color(255, 0, 0));
}

// ==========================================
// SEIER-ANIMASJON på HLR-strip (D4)
// ==========================================
void tegnSeierstrip() {
  strip3.clear();

  seierOrm += 0.3;
  if (seierOrm >= LED3_COUNT) seierOrm -= LED3_COUNT;

  for (int i = 0; i < LED3_COUNT; i++) {
    float avstand = abs((float)i - seierOrm);
    if (avstand > LED3_COUNT / 2.0) avstand = LED3_COUNT - avstand;

    float styrke = exp(-avstand * avstand * 0.15);
    styrke = klemm(styrke + 0.15, 0.0, 1.0);

    uint8_t gron = (uint8_t)(styrke * 255);
    uint8_t blaa = (uint8_t)((1.0 - styrke) * 120);
    strip3.setPixelColor(i, strip3.Color(blaa, 0, gron));
  }
}

// ==========================================
// HLR-VEILEDER (D4) – normal modus
// ==========================================
void tegnHLRstrip() {
  strip3.clear();

  for (int i = 0; i < LUFT_ANTALL; i++) {
    if (luftFade[i] > 0.01) {
      uint8_t styrke = (uint8_t)(luftFade[i] * 200);
      strip3.setPixelColor(LUFT_START + i, strip3.Color(styrke, 0, 0));
    }
  }

  if (tilstand == VENTER_INNPUST && luftFade[0] < 0.05) {
    if (millis() - blinkTimer > 400) {
      blinkState = !blinkState;
      blinkTimer = millis();
    }
    if (blinkState) {
      strip3.setPixelColor(LUFT_START, strip3.Color(0, 255, 0));
    }
  }

  int hjerteLEDs = 0;
  if (tilstand == KOMPRESJONER) {
    hjerteLEDs = map(kompresjonTeller, 0, KOMPRESJONER_MAL, 0, HJERTE_ANTALL);
  } else if (tilstand == VENTER_INNPUST) {
    hjerteLEDs = map(innblasningTeller, 0, INNBLASNINGER_PR_RUNDE, HJERTE_ANTALL, 0);
  }
  hjerteLEDs = constrain(hjerteLEDs, 0, HJERTE_ANTALL);

  for (int i = 0; i < hjerteLEDs; i++) {
    float fremgang = (float)i / (HJERTE_ANTALL - 1);
    strip3.setPixelColor(HJERTE_SLUTT - i, hjerteFarge(fremgang));
  }
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(9600);
  delay(500);
  pinMode(HJERTE_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  nesteSlagh = millis();

  strip.begin();
  strip.setBrightness(150);
  strip.clear();
  strip.show();

  strip2.begin();
  strip2.setBrightness(150);
  strip2.clear();
  strip2.show();

  strip3.begin();
  strip3.setBrightness(150);
  strip3.clear();
  strip3.show();
}

void loop() {
  lesKnapper();
  oppdaterKvalitet();
  oppdaterHLR();
  oppdaterLuftFade();
  oppdaterBuzzer();
  delay(10);

  if (millis() - lastAnimUpdate >= ANIMASJON_MS) {
    lastAnimUpdate = millis();
    tegnBlodstrips();

    if (tilstand == SEIER) {
      tegnSeierstrip();
    } else {
      tegnHLRstrip();
    }

    strip.show();
    strip2.show();
    strip3.show();
  }
}
