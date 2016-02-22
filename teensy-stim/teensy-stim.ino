/*
  Run a digital and/or analog stimulus protocol from a Teensy 3.1+ board.
 */

#include <EEPROM.h>
#include <math.h>

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
  byte s;    // Shape: 0 = digital, 1 = sinusoidal, 2 = triangular
  byte next; // Number of next protocol, 255 = none
  byte used; // 0 = unused, otherwise (channel # + 1)
};

struct Channel {
  Dura t;        // Time remaining
  Dura yn;       // Time until next stimulus status switch
  Dura pq;       // Time until next pulse status switch
  byte runlevel; // Runlevel 0 = off, 1 = stim off, 2 = stim on pulse off, 3 = pulse on
  byte pin;      // The digital pin number for this output, or 255 = analog out
  byte disabled; // Channel may not be used unless it is 0
  byte protocol; // Which protocol we're running now, 255 = none
  byte protzero; // Initial protocol to start at (used only for resetting), 255 = none
  byte protidx;  // Counts up as we walk through protocols
  byte unused0;
  byte unused1;
};

int protoi = 0;            // Index of next empty protocol slot
int proton = 0;            // Total number of protocols
Protocol protocols[PROT];  // Slots for protocols (not necessarily contiguous)

Channel channels[CHAN];

int errormsgN = 12;
byte errormsg[12];

int eepsigN = 8;                    // Stimulator signature size
byte* eepsig = (byte*)"stim1.0 ";   // Expected string
int idN = 12;                       // Size of identifying string that follows
int eN = eepsigN + idN;             // Whole identifier (stim + name)
int whoamiN = 0;                    // Size of string returned to client
byte whoami[24];                    // ~ + eepsig + id + $ + zero terminator

int bufi = 0;    // Input buffer index
int bufN = 63;   // Input buffer size
byte buf[63];    // Input buffer
void shiftbuf(int n) {
  if (n > 0) {
    for (int i = n, j = 0; i < bufi; i++, j++) buf[j] = buf[i];
    bufi = (n >= bufi) ? 0 : bufi - n;
  }
}


int tick;               // Last clock tick (microseconds), used to update time
Dura global_clock;      // Counts up from the start of protocols
Dura pending_shift;     // Requested shifts in timing.  TODO--actually make the shifts.
volatile int running;   // Number of running channels.  0 = setup, values < 0 = error state
void run_by(int r) {
  cli();
  running = running + r;   // Super-explicit so you see it isn't an atomic operation!
  sei();
}

Dura blinking;              // Countdown timer for blinking
volatile bool blink_is_on;  // State reflecting whether blink is on or off


/***************************************************
** Initialization code -- run once only at start! **
***************************************************/

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


/********************************
** Number manipulation and I/O **
*********************************/

#define CLIP(i, lo, hi) ((i < lo) ? lo : ((i > hi) ? hi : i))

#define HEXTRI(i) (i + ((i < 10) ? '0' : '7'))

#define DEHEXT(i) (i - ((i > '9') ? '7' : '0'))

void six_write(int value, byte *target) {
  for (int i = 5; i >= 0; i--, value /= 10) target[i] = '0'+(value%10);
}

int six_read(byte *target) {
  int s = 0;
  for (int i = 0; i < 6; i++) {
    byte c = target[i];
    if (c < '0' || c > '9') return -1;
    s = 10*s + (c-'0');
  }
  return s;
}


/************************************
** Manipulation and I/O of timings **
************************************/

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

void seven_write(Dura t, byte *target) {
  if (t.s >= 1000) {
    six_write(t.s, target);
    target[6] = 's';
  }
  else if (t.s >= 1) {
    six_write(t.s*1000 + t.u/1000, target);
    target[6] = 'm';
  }
  else {
    if (t.s < 0 || t.u < 0) six_write(0, target);
    else six_write(t.u, target);
    target[6] = 'u';
  }
}

Dura seven_read(byte *target) {
  Dura dura;
  int i = six_read(target);
  if (i < 0) { dura.s = dura.u = -1; }
  else {
    if      (target[6] == 's') { dura.s = i; dura.u = 0; }
    else if (target[6] == 'u') { dura.s = 0; dura.u = i; }
    else if (target[6] == 'm') { dura.s = i/1000; dura.u = 1000*(i%1000); }
    else                       { dura.s = -1; dura.u = -1; }
  }
  return dura;
}


/*************************************
** Control of channel state machine **
*************************************/

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

void halt_a_channel(Channel *ch) {
  if (ch->pin != 255 && ch->runlevel == 3) turn_off(ch);
  ch->runlevel = 0;
  run_by(-1);
}

void halt_channels() {
  protoi = -protoi;
  for (int i = 0; i < CHAN; i++) halt_a_channel(channels + i);
  if (running > 0 && channels[DIG].runlevel > 0) {
    unsigned int m = micros();
    while (running > 0 && micros() - m < 100000) {}
  }
  if (running > 0) running = 0;
}


/******************************
** Generic parsing utilities **
******************************/

void die_with_message(const char *msg) {
  int n = strnlen(msg, 63);
  halt_channels();
  Serial.write(msg, n);
  Serial.send_now();
}

void parse_time_error() {
  halt_channels();
  memcpy(errormsg, "bad time    ", 12);
  running = -1;
}

Dura parse_time(byte *target) {
  Dura result = seven_read(target);
  if (result.s < 0) parse_time_error();
  return result;
}

void write_now(byte *msg, int n) {
  Serial.write(msg, n);
  Serial.send_now();
}

void raise_unknown_command_error(byte cmd, byte chan) {
  shiftbuf(2);
  halt_channels();
  running = -1;
  memcpy(errormsg, "unknown", 7);
  memset(errormsg + 7, ' ', errormsgN - 7);
  errormsg[8] = cmd;
  errormsg[10] = chan;
}

Protocol not_a_protocol;

Protocol* ensure_protocol(Channel *ch) {
  // TODO--make something that works
  return &not_a_protocol;
}

bool assert_not_running(Channel *ch) {
  // TODO--make this check something
  return false;
}

bool assert_is_wave(Channel *ch) {
  // TODO--make this check something
  return false;
}

bool assert_is_digital(Channel *ch) {
  // TODO--make this check something
  return false;
}

void set_channel_shape(Channel *ch, byte shape) {
  Protocol *p = ensure_protocol(ch);
  if ((shape == 0 || assert_is_wave(ch)) && assert_not_running(ch)) p->s = shape;
}


/********************************************************
** Interpretation of global commands with no arguments **
********************************************************/

void start_running() {
  shiftbuf(2);  // TODO--actually do something
}

void report_errors() {
  shiftbuf(2);
  byte two[2];
  two[0] = '~';
  int r = running;
  two[1] = (r < 0) ? '$' : HEXTRI(r);
  write_now(two, 2);
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
    six_write(global_clock.s, msg + i);
    i += 6;
    msg[i++] = 's';
    six_write(global_clock.u, msg + i);
    i += 6;
    msg[i++] = 'u';
  }
  write_now(msg, i);
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


/*****************************************************
** Interpretation of global commands with arguments **
*****************************************************/

void shift_timing(int direction) {
  if (bufi >= 9) {
    Dura my_shift = parse_time(buf + 2);
    shiftbuf(9);
    if (my_shift.s >= 0) {
      if (direction < 0) diminish_by(&pending_shift, my_shift);
      else advance_from(&pending_shift, my_shift);
    }
  }
}


/******************************************************************
** Interpretation of channel-specific commands with no arguments **
******************************************************************/

void abort_channel(Channel *ch) {
  shiftbuf(3);
  halt_a_channel(ch);
}

void report_channel_state(Channel *ch) {
  shiftbuf(3);
  byte msg[4];
  msg[0] = '~';
  msg[1] = '0' + ch->runlevel;
  msg[2] = HEXTRI((ch->protzero >> 5));
  msg[3] = HEXTRI((ch->protzero & 0x1F));
  Serial.write(msg, 4);
  Serial.send_now();
}

void run_only_channel(Channel *ch) {
  shiftbuf(3);
  // TODO--actually run the channel
}

void report_channel_protocol(Channel *ch) {
  shiftbuf(3);
  byte msg[45];
  int i = 0;
  msg[i++] = '~';
  Protocol *p = ensure_protocol(ch);
  seven_write(p->t, msg+i); i += 7;
  seven_write(p->d, msg+i); i += 7;
  seven_write(p->y, msg+i); i += 7;
  seven_write(p->n, msg+i); i += 7;
  seven_write(p->p, msg+i); i += 7;
  seven_write(p->q, msg+i); i += 7;
  msg[i++] = (p->i == 0) ? ';' : 'i';
  msg[i++] = (p->s == 0) ? ';' : ((p->s == 1) ? 's' : 'r');
  Serial.write(msg, i);
  Serial.send_now();
}

void report_channel_stats(Channel *ch) {
  shiftbuf(3);
  // TODO--actually report the stats.  Need to collect them first.
}

void invert_polarity(Channel *ch) {
  shiftbuf(3);
  Protocol *p = ensure_protocol(ch);
  if (assert_not_running(ch)) p->i = (p->i == 0) ? 255 : 0;
}

void make_sinusoidal(Channel *ch) { 
  shiftbuf(3);
  set_channel_shape(ch, 1);
}

void make_triangular(Channel *ch) {
  shiftbuf(3);
  set_channel_shape(ch, 2);
}

/******************************************************************
** Interpretation of channel-specific commands with no arguments **
******************************************************************/

void set_a_duration(Channel *ch, size_t offset, bool isdig, bool iswave) {
  if (bufi >= 10) {
    Protocol *p = ensure_protocol(ch);
    Dura *d = (Dura*)((size_t)(p) + offset);
    if (assert_not_running(ch) && (!isdig || assert_is_digital(ch)) && (!iswave || assert_is_wave(ch))) *d = parse_time(buf+3);
    shiftbuf(10);
  }
}

void set_channel_time(Channel *ch)        { set_a_duration(ch, offsetof(Protocol,t), false, false); }

void set_channel_delay(Channel *ch)       { set_a_duration(ch, offsetof(Protocol,d), false, false); }

void set_channel_stim_on(Channel *ch)     { set_a_duration(ch, offsetof(Protocol,y), false, false); }

void set_channel_stim_off(Channel *ch)    { set_a_duration(ch, offsetof(Protocol,n), false, false); }

void set_channel_pulse_on(Channel *ch)    { set_a_duration(ch, offsetof(Protocol,p), true, false); }

void set_channel_pulse_off(Channel *ch)   { set_a_duration(ch, offsetof(Protocol,q), true, false); }

void set_channel_wave_period(Channel *ch) { set_a_duration(ch, offsetof(Protocol,p), false, true); }

void set_channel_wave_amplitude(Channel *ch) {
  if (bufi >= 10) {
    Protocol *p = ensure_protocol(ch);
    if (assert_not_running(ch) && assert_is_wave(ch)) {
      p->q.s = 0;
      p->q.u = six_read(buf + 3);
    }
    // TODO -- error message if u > 2047.
    shiftbuf(10);
  }
}

void set_channel(Channel *ch) {
  if (bufi >= 47) {
    Protocol *p = ensure_protocol(ch);
    int i = 3;
    p->t = parse_time(buf + i); i += 7;
    p->d = parse_time(buf + i); i += 7;
    p->y = parse_time(buf + i); i += 7;
    p->n = parse_time(buf + i); i += 7;
    p->p = parse_time(buf + i); i += 7;
    if (ch->pin != 255) p->q = parse_time(buf + i);
    else { p->q.u = six_read(buf + i); p->q.s = 0; }
    i += 7;
    byte c = buf[i++];
    if (c == ';') p->i = 0; else if (c == 'i') p->i = 255; else die_with_message("bad invert");
    c = buf[i++];
    if (c == ';') p->s = 0;
    else if (assert_is_wave(ch)) {
      if (c == 's') p->s = 1;
      else if (c == 'r') p->s = 2;
      else die_with_message("bad shape");
    }
    shiftbuf(47);
  }
}

void set_and_run_only_channel(Channel *ch) {
  // TODO--actually do this
}

void append_protocol(Channel *ch) {
  // TODO--actually do it
}


/**************************************
** Select which command to interpret **
**************************************/

void parse_command_on(Channel *ch, char who) {
  if (ch->disabled) {
    halt_channels();
    running = -1;
    memcpy(errormsg,"inactive    ", 12);
    errormsg[9] = who;
  }
  if (bufi > 2) {
    byte c = buf[2];  // Command
    if      (c == '.') abort_channel(ch);
    else if (c == '$') report_channel_state(ch);
    else if (c == '!') run_only_channel(ch);
    else if (c == '/') set_and_run_only_channel(ch);
    else if (c == '*') report_channel_protocol(ch);
    else if (c == '#') report_channel_stats(ch);
    else if (c == 'i') invert_polarity(ch);
    else if (c == 's') make_sinusoidal(ch);
    else if (c == 'r') make_triangular(ch);
    else if (c == 't') set_channel_time(ch);
    else if (c == 'd') set_channel_delay(ch);
    else if (c == 'y') set_channel_stim_on(ch);
    else if (c == 'n') set_channel_stim_off(ch);
    else if (c == 'p') set_channel_pulse_on(ch);
    else if (c == 'q') set_channel_pulse_off(ch);
    else if (c == 'w') set_channel_wave_period(ch);
    else if (c == 'a') set_channel_wave_amplitude(ch);
    else if (c == '=') set_channel(ch);
    else if (c == '+') append_protocol(ch);
    else               raise_unknown_command_error(c, who);
  }
}

void interpret(char cmd) {
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
  else                 raise_unknown_command_error(cmd, ' ');
}


/*****************************************
** State machine for stimuli and/or I/O **
*****************************************/

int count_me = 0;

// Loop runs forever (after setup)
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
          run_by(-1);
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
    Serial.println(buf[1],DEC);
    Serial.send_now();
    interpret(buf[1]);
  }

  // int r = ARM_DWT_CYCCNT;   // Read cycle count (effectively a 72 MHz clock)
  // Serial.println(r - r0);
  // Serial.send_now();
}

