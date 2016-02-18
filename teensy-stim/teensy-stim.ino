/*
  Run a digital and/or analog stimulus protocol from a Teensy 3.1+ board.
 */

#include <EEPROM.h>
#include <math.h>

int eepsigN = 8;
int idN = 12;
int eN = eepsigN + idN;
byte* eepsig = (byte*)"stim1.0 ";

int lo = LOW;
int hi = HIGH;

#define LED_PIN 13
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

struct Protocol {
  Dura t;    // Total time
  Dura d;    // Delay
  Dura y;    // Stimulus on time
  Dura n;    // Stimulus off time
  Dura p;    // Pulse time
  Dura q;    // Pulse off time
  byte i;    // Invert? 0 = no
  byte s;    // What is this for?
  byte next; // Number of next protocol, 255 = none
  byte unused0;
};

struct Channel {
  Dura t;        // Time remaining
  Dura yn;       // Time until next stimulus status switch
  Dura pq;       // Time until next pulse status switch
  byte protocol; // Which protocol we're running, 255 = none
  byte runlevel; // Runlevel 0 = off, 1 = stim off, 2 = stim on pulse off, 3 = pulse on
  byte pin;      // The digital pin number for this output, or 255 = analog out
  byte unused0;
};

int proto_i = 0;
Protocol protocols[PROT];

Channel channels[CHAN];

int errormsgN = 12;
byte errormsg[12];

char buf[63];

int tick;               // Last clock tick (microseconds), used to update time
Dura global_clock;      // Counts up from the start of protocols
volatile int running;   // Number of running channels.  0 = setup, values < 0 = error state

void read_my_eeprom() {
  // Make sure the EEPROM corresponds to the expected device
  int i = 0;
  for (; i < eepsigN; i++) if (EEPROM.read(i) != eepsig[i]) {
    char msg[] = "wrong-ver";
    running = -1;
    int j = 0;
    for (; j < 9; j++) errormsg[j] = msg[j];
    for (; j < errormsgN; j++) errormsg[j] = ' ';
    return;
  }

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

void init_wave() {
  for (int i = 0; i < 256; i++) {
    wave[i] = 2047 + (short)round(2047*sin(i * 2 * M_PI));
  }
}

void init_pins() {
  if (waveN > 0) analogWriteResolution(12);
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < digiN; i++) pinMode(digi[i], OUTPUT);
}

void advance(Dura *x, int us) {
  x->u += us;
  while (x->u >= 1000000) { x->u -= 1000000; x->s += 1; }
}

void advance_from(Dura *x, Dura v) {
  x->u += v.u;
  x->s += v.s;
  while (x->u >= 1000000) { x->u -= 1000000; x->s += 1; }
}

void diminish(Dura *x, int us) {
  x->u -= us;
  while (x->u < 0) { x->u += 1000000; x->s -= 1; }
}

void diminish_by(Dura *x, Dura v) {
  x->u -= v.u;
  x->s -= v.s;
  while (x->u < 0) { x->u += 1000000; x->s -= 1; }
}

// Setup runs once at reset / power on
void setup() {
  read_my_eeprom();
  init_pins();
  Serial.begin(115200);
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  for (int i = 0; i < PROT; i++) protocols[i].next = 255;
  for (int j = 0; j < CHAN; j++) channels[j].protocol = 255;
  running = 0;
}

void turn_off(Channel *ci) {
  Protocol *p = (protocols + ci->protocol);
  if (p->i == 0) digitalWrite(ci->pin, LOW);
  else digitalWrite(ci->pin, HIGH);
}

void turn_on(Channel *ci) {
  Protocol *p = (protocols + ci->protocol);
  if (p->i == 0) digitalWrite(ci->pin, HIGH);
  else digitalWrite(ci->pin, LOW);
}

// Loop runs forever
void loop() {
  if (running > 0) {
    int now = micros();
    int delta = now - tick;
    tick = now;
    advance(&global_clock, delta);
    for (int i = 0; i < digiN; i++) {
      if (channels[i].runlevel > 0) {
        Channel *ci = &channels[i];
        diminish(&(ci->t), delta);
        if (ci->t.s < 0) {
          if (ci->runlevel == 3) turn_off(ci);
          ci->runlevel = 0;
          running -= 1;
          continue;   // CONTINUE WITH NEXT CHANNEL, THIS ONE IS DONE
        }
        else {
          diminish(&(ci->yn), delta);
          if (ci->yn.s < 0) {
            bool flipped = false;
            bool on = (ci->runlevel > 1);
            while (ci->yn.s < 0) {
              advance_from(&(ci->yn), on ? protocols[ci->protocol].n : protocols[ci->protocol].y);
              flipped = !flipped;
              on = !on;
            }
            if (!on) {
              if (flipped) { 
                if (ci->runlevel == 3) turn_off(ci);
                ci->runlevel = 1;
              }
            }
            else {
              // TODO
            }
          }
          else {
            // TODO
          }
        }
      }
    }
  }


  int r0 = ARM_DWT_CYCCNT;
  digitalWrite(LED_PIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  int m = micros();
  int n = 100000;
  while ((int)micros() - m < n) {};
  digitalWrite(LED_PIN, LOW);    // turn the LED off by making the voltage LOW
  m = micros();
  n = 900000;
  while ((int)micros() - m < n) {};
  int r = ARM_DWT_CYCCNT;
  Serial.println(r - r0);
  Serial.send_now();
}
