/*
  Writes to the EEPROM of a Teensy 3.1+ board in response to serial commands.
 */
#include <EEPROM.h>

// Default layout of pins.  Can be altered with EEPROM setings.
int eepsigN = 8;
byte* eepsig = (byte*)"stim-1.0";

int idN = 12;

#define eN (eepsigN + idN)

byte lo = LOW;
byte hi = HIGH;

int digiN = 25;
byte digi[] = { 14, 15, 16, 17, 18,
               19, 20, 21, 22,  2,
                3,  4,  5,  6,  7,
                8,  9, 10, 11, 12,
               28, 27, 26, 25, 13 };
byte pole[] = { lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo,
               lo, lo, lo, lo, lo };
char obuf[53];

// Whether we have one analog output or none.
int waveN = 1;

void EEset(int i, byte b) {
  if (EEPROM.read(i) != b) EEPROM.write(i, b);
}

void write_eeprom_sig() {
  for (int i = 0; i < eepsigN; i ++) EEset(i, eepsig[i]);
}

void write_default_id() {
  for (int i = eepsigN; i < eepsigN + idN; i++) EEset(i, 0);
}

void write_default_wave() {
  EEset(eN, 0);   // Do not set wave number, will default
}

void write_default_digiN() {
  EEset(eN + 2, 0);  // Do not set digiN number, will default
}

void write_default_digipole() {
  EEset(eN + 4, 0);  // Do not set digi array, will default
}

void write_my_id(byte* id, int n) {
  for (int i = 0; i < n && i < idN; i++) EEset(eepsigN + i, id[i]);
  if (n >= 0 && n < idN) EEset(eepsigN + n, 0);
}

void write_my_wave(int n) {
  EEset(eN, 1);
  EEset(eN+1, (n == 0) ? 0 : 1);
}

void write_my_digiN(int n) {
  EEset(eN+2, 1);
  EEset(eN+3, (n < 0) ? 0 : ((n > 25) ? 25 : n));
}

void write_my_arrays(byte *poles, byte *digis, int n) {
  if (n < 0) n = 0;
  if (n > 25) n = 25;
  EEset(eN+4, 1);
  for (int i = eN+5, j = 0; j < n; i += 2, j++) {
    EEPROM.write(i, (poles[j] == 255) ? 255 : 0);
    EEPROM.write(i+1, (digis[j] < 0) ? 0 : ((digis[j] > 33) ? 33 : digis[j]));
  }
}

void read_and_report_everything() {
  int i = 0;
  obuf[0] = '~';
  for (i = 0; i < eN; i++) {
    byte c = EEPROM.read(i);
    obuf[i+1] = c;
    if (c == 0) break;
  }
  obuf[i+1] = '$';
  obuf[i+2] = 0;
  Serial.println(obuf);
  if (EEPROM.read(eN) == 0) Serial.println("default");
  else {
    obuf[0] = '~';
    obuf[1] = (EEPROM.read(eN+1) == 0) ? '0' : '1';
    obuf[2] = '$';
    obuf[3] = 0;
    Serial.println(obuf);
  }
  if (EEPROM.read(eN+2) == 0) Serial.println("default");
  else {
    obuf[0] = '~';
    obuf[1] = (EEPROM.read(eN+3) < 0) ? 'A' : (((EEPROM.read(eN+3) > 25) ? 25 : EEPROM.read(eN+3))+'A');
    obuf[2] = '$';
    obuf[3] = 0;
    Serial.println(obuf);
  }
  if (EEPROM.read(eN+4) == 0) Serial.println("default");
  else {
    obuf[0] = '~';
    int n = (EEPROM.read(eN+2) == 0) ? 25 : EEPROM.read(eN+3);
    if (n < 0) n = 0;
    if (n > 25) n = 25;
    for (i = 0; i < n; i++) {
      obuf[2*i+1] = (EEPROM.read(eN+2*i+5) == 255) ? '^' : '_';
      obuf[2*i+2] = ((EEPROM.read(eN+2*i+6) < 0) ? 0 : ((EEPROM.read(eN+2*i+6) > 33) ? 33 : EEPROM.read(eN+2*i+6))) + ';';
    }
    obuf[2*i+1] = '$';
    obuf[2*i+2] = 0;
    Serial.println(obuf);
  }
  Serial.send_now();
}

// the setup routine runs once when you press reset:
void setup() {
  Serial.begin(115200);
  write_eeprom_sig();
  write_default_id();
  write_default_wave();
  write_default_digiN();
  write_default_digipole();
}

bool already_got_it = false;

// the loop routine runs over and over again forever:
void loop() {
  if (Serial.available() || already_got_it) {
    if (already_got_it || Serial.read() == '~') {
      already_got_it = false;
      int t = millis();
      while (millis() - t < 1000 && !Serial.available()) {}
      if (Serial.available() && Serial.read() == '~') {
        while(millis() - t < 1000 && !Serial.available()) {}
        if(Serial.available()) {
          byte c = Serial.read();
          if (c == '?') read_and_report_everything();
          else if (c == '_') {
            write_default_id();
            write_default_wave();
            write_default_digiN();
            write_my_arrays(pole, digi,25);
            write_default_digipole();
          }
          else if (c == '0') write_my_wave(0);
          else if (c == '1') write_my_wave(1);
          else if (c >= 'A' && c <= 'Z') write_my_digiN(c - 'A');
          else if (c == '=') {
            int i = 0;
            while (millis() - t < 1000 && i < 12 && c != '$' && c != '~') {
              if (Serial.available()) {
                c = Serial.read();
                obuf[i] = c;
                i += 1;
              }
            }
            if (i > 0) write_my_id((byte*)obuf, i - ((c == '$' || c == '~') ? 1 : 0));
            if (c == '~') already_got_it = true;
          }
          else if (c == '#') {
            int i = 0;
            while (millis() - t < 1000 && i < 50 && c != '$' && c != '~') {
              if (Serial.available()) {
                c = Serial.read();
                if ((i&1) == 0) obuf[i >> 1] = (c == '^') ? 255 : 0;
                else obuf[26 + (i >> 1)] = (c < ';') ? 0 : ((c > '\\') ? 33 : (c - ';'));
                i += 1;
              }
            }
            if (c == '$' || c == '~') i--;
            Serial.println(i >> 1, DEC);
            if (i > 1) write_my_arrays((byte*)obuf, (byte*)(&obuf[26]), i >> 1);
            if (c == '~') already_got_it = true;
          }
        }
      }
    }
  }
}

