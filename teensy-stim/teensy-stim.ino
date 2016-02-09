/*
  Run a digital and/or analog stimulus protocol from a Teensy 3.1+ board.
 */

// Default layout of pins.  Can be altered with EEPROM setings.
int eepsigN = 8;
byte* eepsig = (byte*)"stim-1.0";

int lo = LOW;
int hi = HIGH;

int digiN = 25;
int digi[] = { 14, 15, 16, 17, 18,
               19, 20, 21, 22,  2,
                3,  4,  5,  6,  7,
                8,  9, 10, 11, 12,
               28, 27, 26, 25, 13 };
int pole[] = { lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo };

// Whether we have one analog output or none.
int waveN = 1;

void read_my_eeprom() {
  // Make sure the EEPROM corresponds to the expected device
  int i = 0;
  for (; i < eepsigN; i++) if (EEPROM.read(i) != eepsig[i]) return;

  // Read wave number if it's there
  if (EEPROM.read(i++) != 0) {
    waveN = EEPROM.read(i++);
    if (waveN < 0) waveN = 0;
    if (waveN > 1) waveN = 1;
  }
  else i += 1;

  // Read number of digital channels if it's there
  if (EEPROM.read(i++) != 0) {
    digiN = EEPROM.read(i++);
    if (digiN < 0) digiN = 0;
    if (digiN > 25) digiN = 25;
  }
  else i += 1;

  // Read array of pins and polarities if they are there
  if (EEPROM.read(i++) != 0) {
    for (int j = 0; j < digiN; j++) {
      pole[j] = (EEPROM.read(i++) == 255) ? hi : low;
      digi[j] = EEPROM.read(i++);
      if (digi[j] < 0) digi[j] = 0;
      if (digi[j] > 33) digi[j] = 33;
    }
  }
}

void set_digi() {
}

void init_pins() {
  if (waveN > 0) analogWriteResolution(12);
  for (int i = 0; i < digiN; i++) pinMode(digi[i], OUTPUT);
}

// the setup routine runs once when you press reset:
void setup() {
  read_my_eeprom();
  Serial.begin(115200);
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  fix = micros();
}

// the loop routine runs over and over again forever:
void loop() {
  int r0 = ARM_DWT_CYCCNT;
  digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
  int m = micros();
  int n = delays[delayi];
  while (micros() - m < n) {};
  delayi = (delayi >= 9) ? 0 : (delayi+1);
  digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
  m = micros();
  n = delays[delayi];
  while (micros() - m < n) {};
  delayi = (delayi >= 9) ? 0 : (delayi+1);
  m = ARM_DWT_CYCCNT;
  n = ARM_DWT_CYCCNT;
  int k = 0;
  for (int i = 0; i < 10; i++) k += delays[i];
  int r = ARM_DWT_CYCCNT;
  Serial.println(n - m);
  Serial.println(r - n);
  Serial.println(r - r0);
  Serial.println(micros() - fix);
  Serial.send_now();
}

