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

#define CLIP(i, lo, hi) ((i < lo) ? lo : ((i > hi) ? hi : i))
#define HEXTRI(i) (i + ((i < 10) ? '0' : '7'))
#define DEHEXT(i) (i - ((i > '9') ? '7' : '0'))

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
  byte disabled; // Channel may not be used unless it is 0
};

int proto_i = 0;
Protocol protocols[PROT];

Channel channels[CHAN];

int errormsgN = 12;
byte errormsg[12];

int whoamiN = 0;
byte whoami[24];

int bufi;
int bufN = 63;
byte buf[63];

int tick;               // Last clock tick (microseconds), used to update time
Dura global_clock;      // Counts up from the start of protocols
Dura pending_shift;     // Requested shifts in timing.  TODO--actually make the shifts.
volatile int running;   // Number of running channels.  0 = setup, values < 0 = error state

Dura blinking;
volatile bool blink_is_on;

void read_my_eeprom() {
  // Make sure the EEPROM corresponds to the expected device
  whoami[0] = '~';
  int i = 0;
  for (; i < eepsigN; i++) {
    byte c = EEPROM.read(i);
    if (c != eepsig[i]) {
      char msg[] = "wrong-ver";
      running = -2;
      int j = 0;
      for (; j < 9; j++) errormsg[j] = msg[j];
      for (; j < errormsgN; j++) errormsg[j] = ' ';
      return;
    }
    else whoami[i+1] = c;
  }
  for (; i < eepsigN + 12; i++) {
    byte c = EEPROM.read(i);
    if (c == 0) break;
    whoami[i+1] = c;
  }
  whoami[++i] = '$';
  whoami[++i] = 0;
  whoamiN = i;
  i = eN;

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
  blink_is_on = false;
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
  running = 0;
  read_my_eeprom();
  init_pins();
  Serial.begin(115200);
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  for (int i = 0; i < PROT; i++) protocols[i].next = 255;
  for (int j = 0; j < CHAN; j++) {
    channels[j].protocol = 255;
    if (j < DIG) channels[j].disabled = (j >= digiN) ? 1 : 0;
    else channels[j].disabled = (waveN < 1) ? 1 : 0;
  }
  running = 0;
  blinking.s = 0;
  blinking.u = 1000;   // 1 ms delay before doing anything
  pending_shift.s = 0;
  pending_shift.u = 0;
  tick = micros();
  bufi = 0;
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

void halt_channels() {
  // TODO--do something
}

void six_in(int value, byte *target) {
  for (int i = 5; i >= 0; i--, value /= 10) target[i] = '0'+(value%10);
}

int six_out(byte *target) {
  int s = 0;
  for (int i = 0; i < 6; i++) {
    byte c = target[i];
    if (c < '0' || c > '9') return -1;
    s = 10*s + (c-'0');
  }
  return s;
}

void parse_time_error() {
  halt_channels();
  memcpy(errormsg, "bad time    ", 12);
  running = -1;
}

Dura parse_time(byte *target) {
  Dura result;
  result.s = 0;
  result.u = 0;
  int s = six_out(target);
  if (s < 0) parse_time_error();
  else {
    byte c = target[6];
    if (c == 's') result.s = s;
    else if (c == 'u') result.u = s;
    else if (c == 'm') { result.s = s/1000; result.u = s%1000; }
    else parse_time_error();
  }
  return result;
}

volatile int count_me;

void shiftbuf(int n) {
  if (n > 0) {
    for (int i = n, j = 0; i < bufi; i++, j++) buf[j] = buf[i];
    bufi = (n >= bufi) ? 0 : bufi - n;
  }
}

void start_running() {
  shiftbuf(2);  // TODO--actually do something
}

void report_errors() {
  shiftbuf(2);
  byte two[2];
  two[0] = '~';
  two[1] = (running < 0) ? '$' : HEXTRI(running);
  Serial.write(two, 2);
  Serial.send_now();
}

void report_timing() {
  shiftbuf(2);
  byte msg[15];
  int i = 0;
  msg[i++] = '~';
  if (running < 0) {
    msg[i++] = '!';
    for (int j = 0; j < errormsgN; j++, i++) msg[i] = errormsg[j];
    msg[i++] = '$';
  }
  else if (running == 0) {
    memcpy(msg + i, "000000s000000u", 14);
    i += 14;
  }
  else {
    six_in(global_clock.s, msg + i);
    i += 6;
    msg[i++] = 's';
    six_in(global_clock.u, msg + i);
    i += 6;
    msg[i++] = 'u';
  }
  Serial.write(msg, i);
  Serial.send_now();
}

void shift_timing(int direction) {
  if (bufi >= 9) {
    Dura my_shift = parse_time(buf + 2);
    shiftbuf(9);
    if (direction < 0) diminish_by(&pending_shift, my_shift);
    else advance_from(&pending_shift, my_shift);
  }
}

void parse_command_on(Channel *ch, char who) {
  if (ch->disabled) {
    halt_channels();
    running = -1;
    memcpy(errormsg,"inactive    ", 12);
    errormsg[9] = who;
  }
  for (int i = 2; i < bufi; i++) {
    if (buf[i] == '$') { shiftbuf(i+1); return; }
    if (buf[i] == '~') { shiftbuf(i); return; }
  }
}

void abort_and_reset() {
  shiftbuf(2);
  if (running > 0) halt_channels();
  running = 0;
}

void reset_parameters() {
  shiftbuf(2);  // TODO--actually do something
}

void report_identity() {
  shiftbuf(2);
  Serial.println(whoamiN, DEC);
  Serial.write(whoami, whoamiN);
  Serial.send_now();
}

void report_pins() {
  shiftbuf(2);
  byte pinout[27];
  int i = 0;
  pinout[i++] = '~';
  for (; i <= digiN; i++) pinout[i] = HEXTRI(digi[i-1]);
  if (waveN > 0) pinout[i++] = '@';
  pinout[i++] = '$';
  Serial.write(pinout, i);
  Serial.send_now();
}

void raise_unknown_command_error(byte cmd) {
  shiftbuf(2);
  halt_channels();
  running = -1;
  memcpy(errormsg, "unknown ", 8);
  memset(errormsg + 8, ' ', errormsgN - 8);
}

// Loop runs forever
void loop() {
  // Update timing at the beginning of this loop
  int now = micros();
  int delta = now - tick;
  tick = now;
  count_me++;

  if (running > 0) {
    // Run protocol state machines
    advance(&global_clock, delta);
    for (int i = 0; i < digiN; i++) {
      if (channels[i].runlevel > 0) {
        Channel *ci = &channels[i];
        Protocol pc = protocols[ci->protocol];
        diminish(&(ci->t), delta);
        if (ci->t.s < 0) {
          if (ci->runlevel == 3) turn_off(ci);
          ci->runlevel = 0;
          running -= 1;
          continue;   // CONTINUE WITH NEXT CHANNEL, THIS ONE IS DONE
          // TODO--handle switching protocols!
        }
        else {
          diminish(&(ci->yn), delta);
          if (ci->yn.s < 0) {
            bool flipped = false;
            bool on = (ci->runlevel > 1);
            while (ci->yn.s < 0) {
              advance_from(&(ci->yn), on ? pc.n : pc.y);
              flipped = !flipped;
              on = !on;
            }
            if (on && flipped) {
              if (ci->runlevel == 3) turn_off(ci);
              ci->runlevel = 1;
            }
            else if (on != flipped) {
              // We were off and went on, or were on and went off and on.  Either way, we need to figure out what pulse we're in.
              Dura excess = on ? pc.n : pc.y;
              diminish_by(&excess, ci->yn);
              bool pup = true;
              ci->pq = protocols[ci->protocol].p;
              diminish_by(&(ci->pq), excess);
              while (ci->pq.s < 0) {
                advance_from(&(ci->pq), pup ? pc.q : pc.p);
                pup = !pup;
              }
              if (pup) {
                if (ci->runlevel < 3) turn_on(ci);
                ci->runlevel = 3;
              }
              else {
                if (ci->runlevel == 3) turn_off(ci);
                ci -> runlevel = 2;
              }
            }
            else {}   // We were off, and now are off, so it's all good.
          }
          else if (ci->runlevel > 1) {
            diminish(&(ci->pq), delta);
            if (ci->pq.s < 0) {
              bool flipped = false;
              bool pup = (ci->runlevel == 3);
              while (ci->pq.s < 0) {
                advance_from(&(ci->pq), pup ? pc.q : pc.p);
                flipped = !flipped;
                pup = !pup;
              }
              if (flipped) {
                if (pup) {
                  ci->runlevel = 2;
                  turn_off(ci);
                }
                else {
                  ci->runlevel = 3;
                  turn_on(ci);
                }
              }
            }
          }
          else {}  // We were in a stimulus-off state the whole time, so nothing to do
        }
      }
    }
  }
  else {
    // Run state machine for LED status blinking
    diminish(&blinking, delta);
    if (blinking.s < 0) {
      if (blink_is_on) {
        digitalWrite(LED_PIN, LOW);
        advance(&blinking, (running == 0) ? 1980000 : ((running == -1) ? 470000 : 90000));
        blink_is_on = false;
      }
      else {
        digitalWrite(LED_PIN, HIGH);
        advance(&blinking, (running == 0) ? 20000 : ((running == -1) ? 30000 : 10000));
        blink_is_on = true;
      }
      Serial.println(bufi);
      Serial.send_now();
      count_me = 0;
    }
  }

  // Read anything waiting on the serial port
  int av = Serial.available();
  if (av > 0) {
    byte c = Serial.read();
    while (bufi == 0 && c != '~') {
      av--;
      if (av > 0) c = Serial.read();
      else break;
    }
    if (av > 0) {
      buf[bufi++] = c;
      av--;
      while (bufi < bufN && av > 0) {
        c = Serial.read();
        buf[bufi++] = c;
        av--;
      }
    }
  }

  // Interpret any fully buffered commands
  if (bufi > 1) {
    // NOTE--subcommands are responsible for checking for completeness and consuming input if it is complete!
    char cmd = buf[1];
    Serial.println(cmd,DEC);
    Serial.send_now();
    if      (cmd == '!') start_running();
    else if (cmd == '$') report_errors();
    else if (cmd == '?') report_timing();
    else if (cmd == '>') shift_timing(1);
    else if (cmd == '<') shift_timing(-1);
    else if ((cmd >= '0' && cmd <= '9') || (cmd >= 'A' && cmd <= 'N'))
                         parse_command_on(channels + DEHEXT(cmd), cmd);
    else if (cmd == '@') parse_command_on(channels + DIG, '@');
    else if (cmd == '.') abort_and_reset();
    else if (cmd == '"') reset_parameters();
    else if (cmd == ':') report_identity();
    else if (cmd == '*') report_pins();
    else                 raise_unknown_command_error(cmd);
  }

  // int r = ARM_DWT_CYCCNT;   // Read cycle count (effectively a 72 MHz clock)
  // Serial.println(r - r0);
  // Serial.send_now();
}

