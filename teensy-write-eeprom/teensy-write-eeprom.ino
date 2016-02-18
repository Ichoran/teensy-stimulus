/*
  Writes to the EEPROM of a Teensy 3.1+ board in response to serial commands.
 */
#include <EEPROM.h>

// Default layout of pins.  Can be altered with EEPROM setings.
int eepsigN = 8;
byte* eepsig = (byte*)"stim1.0 ";

int idN = 12;

#define eN (eepsigN + idN)
#define eD (eepsigN + idN + 2)
#define NONE 255

// Make sure you MANUALLY keep digiN the same as this value AND keep the digi[] array this length!
#define DIG 24

byte lo = LOW;
byte hi = HIGH;

int waveN = 1;
int digiN = 24;
byte digi[] = { 14, 15, 16, 17, 18, 19, 20, 21,
                22, 23,  0,  1,  2,  3,  4,  5,
                 6,  7,  8,  9, 10, 11, 12,  13 };
char buf[64];

#define CLIP(i, lo, hi) ((i < lo) ? lo : ((i > hi) ? hi : i))

#define HEXTRI(i) (i + ((i < 10) ? '0' : '7'))

#define DEHEXT(i) (i - ((i > '9') ? '7' : '0'))

void EEset(int i, byte b) {
  if (EEPROM.read(i) != b) EEPROM.write(i, b);
}

void write_eeprom_sig() {
  for (int i = 0; i < eepsigN; i++) EEset(i, eepsig[i]);
}

void write_default_id() {
  for (int i = eepsigN; i < eepsigN + idN; i++) EEset(i, 0);
}

void write_default_wave() {
  EEset(eN, NONE);   // Do not set wave number, will default
}

void write_default_digiN() {
  EEset(eN + 1, NONE);  // Do not set digiN number, will default
}

void write_default_digis() {
  for (int i = 0; i < DIG; i++) EEset(eN + 2 + i, digi[i]);
}

void write_my_id(byte* id, int n) {
  for (int i = 0; i < n && i < idN; i++) EEset(eepsigN + i, id[i]);
  if (n >= 0 && n < idN) EEset(eepsigN + n, 0);
}

void write_my_wave(int n) {
  EEset(eN, (n == 0) ? 0 : 1);
}

void write_my_digiN(int n) {
  EEset(eN+1, (n < 0) ? 0 : ((n > DIG) ? DIG : n));
}

void write_my_digis(byte *digis, int n) {
  if (n < 0) n = 0;
  if (n > DIG) n = DIG;
  for (int j = 0; j < n; j++) EEPROM.write(eD + j, CLIP(digis[j], 0, 33));
}

void read_and_report_everything() {
  int i = 0;
  buf[0] = '~';
  for (i = 0; i < eN; i++) {
    byte c = EEPROM.read(i);
    buf[i+1] = c;
    if (c == 0) break;
  }
  buf[i+1] = '$';
  buf[i+2] = 0;
  Serial.println(buf);

  if (EEPROM.read(eN) == NONE) Serial.println("default");
  else if (EEPROM.read(eN) == 0) Serial.println("no analog");
  else Serial.println("analog present");

  if (EEPROM.read(eN+1) == NONE) Serial.println("default");
  else {
    buf[0] = '~';
    buf[1] = HEXTRI(EEPROM.read(eN+1));
    buf[2] = 0;
    Serial.println(buf);
  }

  buf[0] = '~';
  int n = CLIP(EEPROM.read(eN+1), 0, DIG);
  for (i = 1; i <= n; i++) buf[i] = HEXTRI(EEPROM.read(eD+i-1));
  if (EEPROM.read(eN) != 0) buf[i++] = '@';
  buf[i++] = '$';
  buf[i] = 0;
  Serial.println(buf);

  Serial.send_now();
}

// the setup routine runs once when you press reset:
void setup() {
  Serial.begin(115200);
  write_eeprom_sig();
  write_default_id();
  write_default_wave();
  write_default_digiN();
  write_default_digis();
}

bool already_got_it = false;

unsigned int lastmillis = 0;

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
            write_default_digis();
          }
          else if (c == '.') write_my_wave(0);
          else if (c == '@') write_my_wave(1);
          else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'X')) write_my_digiN(DEHEXT(c));
          else if (c == '=') {
            int i = 0;
            while (millis() - t < 1000 && i < 12 && c != '$' && c != '~') {
              if (Serial.available()) {
                c = Serial.read();
                buf[i] = c;
                i += 1;
              }
            }
            if (i > 0) write_my_id((byte*)buf, i - ((c == '$' || c == '~') ? 1 : 0));
            if (c == '~') already_got_it = true;
          }
          else if (c == '#') {
            int i = 0;
            while (millis() - t < 1000 && i < 50 && c != '$' && c != '~') {
              if (Serial.available()) {
                c = Serial.read();
                if ((c >= '0' && c <='9') || (c >= 'A' && c <= 'X')) buf[i++] = DEHEXT(c);
              }
            }
            if (c == '$' || c == '~') i--;
            if (i > 1) write_my_digis((byte*)buf, i);
            if (c == '~') already_got_it = true;
          }
        }
      }
    }
  }
}


