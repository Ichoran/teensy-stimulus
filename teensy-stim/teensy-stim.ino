/*
  Run a digital and/or analog stimulus protocol from a Teensy 3.1+ board.
 */

#include <EEPROM.h>

int eepsigN = 8;
int idN = 12;
int eN = eepsigN + idN;
byte* eepsig = (byte*)"stim1.0 ";

int lo = LOW;
int hi = HIGH;

#define PROT 254
#define CHAN 25
#define DIG 24

int waveN = 1;
int digiN = DIG;
int digi[DIG];

int16_t wave[256];

struct Dura {
  int s;
  int u;
};

struct State {
  byte protocol;
  byte running;
  byte stimulated;
  byte pulsed;
  Dura d;
  Dura i;
  Dura y;
  Dura n;
  Dura p;
  Dura q;
};

State protocols[PROT];
State channels[CHAN];

void read_my_eeprom() {
  // Make sure the EEPROM corresponds to the expected device
  int i = 0;
  for (; i < eepsigN; i++) if (EEPROM.read(i) != eepsig[i]) return;

  // Read wave number if it's there
  if (EEPROM.read(i++) != 0) {
    waveN = EEPROM.read(i++);
    if (waveN < 0) waveN = 0;
    if (waveN > 1) waveN = 1;  // Default case falls into here anyway
  }
  else i += 1;

  // Read number of digital channels if it's there
  if (EEPROM.read(i++) != 0) {
    digiN = EEPROM.read(i++);
    if (digiN < 0) digiN = 0;
    if (digiN > 25) digiN = 25;  // Default case falls into here anyway
  }
  else i += 1;

  // Read array of pins (it should always be there)
  for (int j = 0; j < digiN; j++) {
    digi[j] = EEPROM.read(i++);
    if (digi[j] < 0) digi[j] = 0;
    if (digi[j] > 33) digi[j] = 33;
  }
}

void set_digi() {
}

void init_pins() {
  if (waveN > 0) analogWriteResolution(12);
  for (int i = 0; i < digiN; i++) pinMode(digi[i], OUTPUT);
}

int fix;

const int led = 13;

// Setup runs once at reset / power on
void setup() {
  read_my_eeprom();
  Serial.begin(115200);
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  pinMode(led, OUTPUT);
  fix = micros();
  for (int i = 0; i < PROT; i++) protocols[i].protocol = 255;
  for (int j = 0; j < CHAN; j++) channels[j].protocol = 255;
}

// Loop runs forever
void loop() {
  int r0 = ARM_DWT_CYCCNT;
  digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
  int m = micros();
  int n = 100000;
  while (micros() - m < n) {};
  digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
  m = micros();
  n = 900000;
  while (micros() - m < n) {};
  int r = ARM_DWT_CYCCNT;
  Serial.println(r - r0);
  Serial.println(micros() - fix);
  Serial.send_now();
}

