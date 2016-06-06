/*
  Run a digital and/or analog stimulus protocol from a Teensy 3.1+ board.
 */

#include <EEPROM.h>
#include <math.h>

#define MHZ 72
#define HTZ 72000000

int lo = LOW;
int hi = HIGH;

#define DIG 24
int digi[DIG] = { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
int led_is = 23;

#define ANA 1
int16_t wave[ANA][256];


/*******************************
 * Duration class, effectively *
 *******************************/

struct Dura {
  int s;  // Seconds
  int k;  // Clock ticks
};

bool valid_dura(Dura d) {
  return d.s >= 0 && d.k >= 0 && d.k < HTZ;
}

int compare_dura(Dura a, Dura b) {
  if (a.s < b.s) return -1;
  else if (a.s > b.s) return 1;
  else {
    if (a.k < b.k) return -1;
    else if (a.k > b.k) return 1;
    else return 0;
  }
}

bool lt_dura(Dura a, Dura b) {
  return (a.s < b.s) || ((a.s == b.s) && (a.k < b.k));
}

void normalize_dura(Dura *d) {
  if (d->k >= HTZ) {
    int xs = d->k / (HTZ);
    d->s += xs;
    d->k -= xs*HTZ;
  }
  else if (d->k < 0) {
    int xs = ((HTZ - 1) - d->k)/HTZ;
    d->s += xs;
    d->u += HTZ*xs;
  }  
}

void add_dura_us(Dura *d, int us) {
  d->k += us*MHZ;
  normalize_dura(d);
}

void add_dura_s(Dura *d, int s) {
  d->s += s;
}

void add_duras(Dura *d, Dura x) {
  d->s += x.s;
  d->k += x.k;
  while (d->k >= HTZ) { d->k -= HTZ; d->s += 1; }
}

void sub_duras(Dura *d, Dura x) {
  d->s -= x.s;
  d->k -= x.k;
  while (d->k < 0) { d->k += HTZ; d->s -= 1; }
}

int64_t dura_as_us(Dura d) {
  return (((int64_t)d.s)*HTZ + d.k)/MHZ;
}

Dura parse_dura(byte* input, int n) {
  int s = 0;
  int u = 0;
  int nu = 0;
  int i = 0;
  // Seconds before decimal point
  for (; i<n && input[i] != '.'; i++) {
    byte b = input[i] - '0';
    if (b < 10) s = s*10 + b;
    else return (Dura){-1, -1};
  }
  if (i+1 < n) {
    // Found decimal point with space afterwards.  Fractions of a second.
    i++;
    for (; i<n && nu < 6; i++, nu++) {
      byte b = input[i] - '0';
      if (b < 10) u = u*10 + b;
      else return (Dura){-1, -1};
    }
    for (; nu < 6; nu++) u = u*10;   // Pad out to microseconds
  }
  return (Dura){s, u*MHZ};
}

void write_dura_8(Dura d, byte* target) {
  int es = 10000000
  int x = 0
  int eu = MHZ*100000
  int i = 0
  for (; es > 0 && (x = (d.s/es)%10) == 0; es /= 10) {}
  for (; es > 0; es /= 10, i++) { x = (d.s/es)%10; target[i] = (byte)(x + '0'); }
  if (i == 0) { target[0] = '0'; target[1] = '.'; i = 2; }
  else if (i < 8) { target[i++] = '.'; }
  for (; i < 8 && eu >= MHZ; eu /= 10, i++) { x = (d.k/eu)%10; target[i] = (byte)(x + '0'); }
}

void write_dura_15(Dura d, byte* target) {
  int i = 0;
  for (int es =   10000000; es >    0; es /= 10, i++) target[i] = (byte)('0' + ((d.s/es)%10));
  target[i++] = '.';
  for (int eu = MHZ*100000; eu >= MHZ; eu /= 10, i++) target[i] = (byte)('0' + ((d.k/eu)%10));
}


/************************
 * Protocol information *
 ************************/

struct Protocol {
  Dura t;    // Total time
  Dura d;    // Delay
  Dura s;    // Stimulus on time
  Dura z;    // Stimulus off time
  Dura p;    // Pulse time (or period, for analog)
  Dura q;    // Pulse off time (analog: amplitude 0-2047 stored as microseconds)
  byte i;    // Invert? 'i' == yes, otherwise no
  byte j;    // Shape: 'l' = sinusoidal, 'r' = triangular, other = digital
  byte next; // Number of next protocol, 255 = none
  byte chan; // Channel number, 255 = none
};

#define PROT 254;
Protocol protocols[PROT];
int proti = DIG+ANA;

void init_protocol(Protocol *p) {
  *p = {{0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, 'u', ' ', 255, 255 };
}

void init_all_protocols(Protocol *ps) {
  for (int i = 0; i < DIG+ANA; i++) init_protocol(ps + i);
  for (int i = DIG; i < DIG+ANA; i++) ps[i].j = 'l';
  proti = DIG+ANA;
}

bool parse_labeled_dura_to_protocol(byte *input, Protocol *p) {
  Dura d = parse_dura(input + 1, 8);
  if (d.s < 0 || d.k < 0) return false;
  switch (input*) {
    case 't': p->t = d; break;
    case 'd': p->d = d; break;
    case 's': p->s = d; break;
    case 'z': p->z = d; break;
    case 'p': p->p = d; break;
    case 'q': p->q = d; break;
    case 'w': p->p = d; break;
    default: return false;
  }
  return true;
}

bool parse_all_duras_to_protocol(byte *input, Protocol *p) {
  for (int i = 1; i < 5; i++) if (input[i*8] != ';') return false;
  Dura d;
  // Manually unrolled loop
  d = parse_dura(input, 0*9); if (!valid_dura(d)) return false; p->t = d;
  d = parse_dura(input, 1*9); if (!valid_dura(d)) return false; p->d = d;
  d = parse_dura(input, 2*9); if (!valid_dura(d)) return false; p->s = d;
  d = parse_dura(input, 3*9); if (!valid_dura(d)) return false; p->z = d;
  d = parse_dura(input, 4*9); if (!valid_dura(d)) return false; p->p = d;
  d = parse_dura(input, 5*9); if (!valid_dura(d)) return false; p->q = d;
  return true;
}


/*********************************
 * Channel information & running *
 *********************************/

struct ChannelError {
  int nstim;     // Number of stimuli scheduled to start
  int smiss;     // Number of stimuli missed entirely
  int npuls;     // Number of pulses scheduled to start
  int pmiss;     // Number of pulses missed entirely
  int emax0;     // Biggest absolute error in pulse start timing (clock ticks)
  int emax1;     // Biggest absolute error in pulse end timing (clock ticks)
  Dura toff0;    // Total error in pulse start timing
  Dura toff1;    // Total error in pulse end timing  
}

void write_channel_error(ChannelError *ce, byte *target) {
  int ns = (ce->nstim > 999999999 || ce->nstim < 0) ? 999999999 : ce->nstim;
  int sm = (ce->smiss > 999999 || ce->smiss < 0) ? 999999 : ce->smiss;
  int np = (ce->npuls > 999999999 || ce->npuls < 0) ? 999999999 : ce->npuls;
  int pm = (ce->pmiss > 999999 || ce->pmiss < 0) ? 999999 : ce->pmiss;
  int e0 = ce->emax0 / MHZ; e0 = (e0 > 99999 || e0 < 0) ? 99999 : e0;
  int e1 = ce->emax1 / MHZ; e1 = (e1 > 99999 || e1 < 0) ? 99999 : e1;
  int64_t t0 = dura_as_us(ce->toff0); t0 = (t0 > 9999999999ll || t0 < 0) ? 0 : t0;
  int64_t t1 = dura_as_us(ce->toff1); t1 = (t1 > 9999999999ll || t1 < 0) ? 0 : t1;
  snprintf(target, 60, "%09d%06d%09d%06d%05d%05d%10lld%10lld", ns, sm, np, pm, e0, e1, t0, t1);
}

#define C_ZZZ 0
#define C_OFF 1
#define C_STM 2
#define C_PLS 3

struct Channel {
  Dura t;         // Time remaining
  Dura yn;        // Time until next stimulus status switch
  Dura pq;        // Time until next pulse status switch
  byte runlevel;  // Runlevel 0 = off, 1 = stim off, 2 = stim on pulse off, 3 = pulse on
  byte pin;       // The digital pin number for this output, or 255 = analog out
  byte disabled;  // Channel will not run unless this has value 0
  byte who;       // Which protocol we're running now, 255 = none
  byte zero;      // Initial protocol to start at (used only for resetting), 255 = none
  byte unused0;
  byte unused1;
  byte unused2;
  ChannelError e; // Error statistics
};

#define CHAN (DIG+ALA)
Channel channels[CHAN];

void init_channel(int index, Channel *c) {
  c = (Channel){ {0, 0}, {0, 0}, {0, 0}, 0, 0, 1, 255, 255, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0} };
  c->pin = (index < DIG) ? digi[index] : 255;
  c->zero = (index < DIG) ? index : 255;
}

void init_all_channels(Channel *cs) {
  for (int i = 0; i < CHAN; i++) init_channel(i, cs+i);
}

void refresh_channel(Channel *c) {
  c->e = (ChannelError){0, 0, 0, 0, 0, 0, 0, 0};
  c->t = c->yn = c->pq = (Dura){0, 0};
  c->runlevel = 0;
  c->who = zero;
}

void refresh_all_channels(Channel *cs) {
  for (int i = 0; i < CHAN; i++) refresh_channel(cs+i);
}

void channel_pin_low(Channel *c) {
  if (c->who != 255) digitalWrite(pin, LOW);
}

void channel_pin_high(Channel *c) {
  if (c->who != 255) digitalWrite(pin, HIGH);
}

void channel_pin_off(Channel *c, Protocol *ps) {
  byte w = c->who;
  if (w != 255) {
    if (ps[w].i == 'i') digitalWrite(pin, HIGH);
    else                digitalWrite(pin, LOW);
  }
}

void channel_pin_on(Channel *c, Protocol *ps) {
  byte w = c->who;
  if (w != 255) {
    if (ps[w].i == 'i') digitalWrite(pin, LOW);
    else                digitalWrite(pin, HIGH);
  }
}

bool channel_run_next_protocol(Channel *c, Protocol *ps) {
  Protocol *p = ps + c->who;
  channel_pin_low(c);
  if (p->next < 255) {
    p = ps + p->next;
    channel_pin_off(c, ps);
    c->runlevel = 1;
    c->t = p->t;
    c->yn = p->d;
  }
  else {
    c->who = 255;
    c->runlevel = 0;
    c->disabled = 1;
    return false;
  }
}

Dura channel_advance(Channel *c, Dura d, Protocol *ps) {
  bool started_yn = false;
  bool started_pq = false;
tail_recurse:
  if (c->disabled || c->runlevel == 0) return (Dura){0,0};
  if (c->runlevel == 1) {
    // Only relevant times are c->t and c->yn
    bool use_yn = lt_dura(c->yn, c->d);
    Dura *x = use_yn ? &(c->yn) : &(c->t);
    if (lt_dura(d, *x)) {
      sub_duras(c->yn, d);
      sub_duras(c->t, d);
      return *x;
    }
    else {
      sub_duras(&d, *x);
      if (use_yn) {
        sub_duras(&(c->t), c->yn);
        channel_pin_on(c, ps);
        started_yn = true;
        started_pq = true;
        c->runlevel = 3;
        c->pq = ps[c->who].q;
        c->yn = ps[c->who].s;
        c->e.nstim++;
        c->e.npuls++;
        // TODO--timing errors here!
      }
      else {
        channel_pin_low(c);
        if (started_yn) { started_yn = false; c->e.nmiss++; }
        if (started_pq) { started_pq = false; c->e.pmiss++; }
        channel_run_next_protocol(c, ps);
      }
      goto tail_recurse;   // Functionally: return channel_advance(c, d, ps, started_yn, started_pq);      
    }
  }
  else {
    // Relevant times are c->t, c->yn, and c->pq
    bool pq_first = lt_dura(c->pq, c->yn) && lt_dura(c->pq, c->t);
    bool yn_first = !pq_first && lt_dura(c->yn, c->t);
    Dura *x = pq_first ? &(c->pq) : (yn_first ? &(c->yn) : &(c->t));
    if (lt_dura(d, *x)) {
      sub_duras(&(c->pq), d);
      sub_duras(&(c->yn), d);
      sub_duras(&(c->t), d);
      return *x;
    }
    else {
      sub_duras(&d, *x);
      if (pq_first) {
        sub_duras(&(c->yn), c->pq);
        sub_duras(&(c->t), c->pq);
        if (c->runlevel == 2) {
          channel_pin_on(c, ps);
          started_pq = true;
          c->runlevel = 3;
          c->pq = ps[c->who].q;
          c->e.npuls++;
          // TODO--timing errors here!
        }
        else {
          channel_pin_off(c, ps);
          c->runlevel = 2;
          c->pq = ps[c->who].q;
          if (started_pq) { started_pq = false; c->e.pmiss++; }
        }
      }
      else if (yn_first) {
        sub_duras(&(c->t), c->pq);
        if (c->runlevel == 3) channel_pin_off(c, ps);
        c->runlevel = 1;
        c->yn = ps[c->who].z;
        if (started_pq) { started_pq = false; c->e.pmiss++; }
        if (started_yn) { started_yn = false; c->e.nmiss++; }
      }
      else {
        channel_pin_low(c, ps);
        if (started_yn) { started_yn = false; c->e.nmiss++; }
        if (started_pq) { started_pq = false; c->e.pmiss++; }
        channel_run_next_protocol(c, ps);
      }
      goto tail_recurse;
    }
  }
}


/*************************
 * Error and I/O buffers *
 *************************/

#define MSGN 62
#define WHON 62
#define BUFN 128

byte msg[MSGN];
int errn = 0;

byte whoami[WHON];
byte buf[BUFN];
int bufi = 0;

int shift_buffer(byte* buffer, int iN, int shift) {
  if (shift > iN) return 0;
  if (shift < 1) return iN;
  for (int j = shift; j < iN; j++) buffer[j-shift] = buffer[shift];
  return iN - shift;
}

int advance_command(byte* buffer, int i0, int iN) {
  int i = 0;
  while (i < iN) {
    byte b = buffer[i];
    if (b == '~' || b == '^') break;
    i++;
  }
  return shift_buffer(buffer, iN, i);
}

void drain_to_buf() {
  int av = Serial.available();
  while ((av--) > 0 && bufi < BUFN) buf[bufi++] = Serial.read();
}

void write_from_msg() {
  if (errn > 0) Serial.write(msg, errn);
  else {
    int n = 0;
    while (n < MSGN && msg[n] != 0) n++;
    Serial.write(msg, n);
  }
  Serial.send_now();
}

void write_who_am_i() {
  int n = 0;
  for (; n < WHON && whoami[n] != 0; n++) msg[n] = whoami[n];
  Serial.write(msg, n);
  Serial.send_now();
}


/***********
 * Runtime *
 ***********/

volatile int tick;       // Last CPU clock count
Dura global_clock;       // Time since start of running.
Dura next_event;         // Time of next event.  Just busywait until then.

#define ERRDONE -2
#define ERRSTOP -1
#define RUNSTOP  0
#define RUNSET   1
#define GOGOGO   2
volatile int runlevel;   // -2 error; -1 stopping to error; 0 ran; 1 accepting commands; 2 running

Dura next_blink;
volatile bool led_is_on;





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

void init_pins() {
  if (waveN > 0) analogWriteResolution(12);
  pinMode(LED_PIN, OUTPUT);
  blink_is_on = false;
  for (int i = 0; i < digiN; i++) pinMode(digi[i], OUTPUT);
}

/*********************************************
 * Initialization -- run once only at start! *
 *********************************************/

void read_my_eeprom() {
  // Read identifying tag (22 chars max)
  whoami[0] = '^';
  int i = 0;
  bool zeroed = false;
  for (; i < WHON-2; i++) {
    byte c = EEPROM.read(i);
    if (zeroed) whoami[i+1] = 0;
    else if (c == 0) { whoami[i+1] = '$'; zeroed = true; }
    else whoami[i+1] = c;
    i++;
  }
  whoami[i+1] = (zeroed) ? 0 : '$';
}

void init_wave() {
  for (int i = 0; i < 256; i++) {
    wave[i] = 2047 + (short)round(2047*sin(i * 2 * M_PI));
  }
}

// Setup runs once at reset / power on
void setup() {
  runlevel = RUNSTOP;
  read_my_eeprom();
  init_all_protocols(protocols);
  init_all_channels(channels);
  Serial.begin(115200);
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  errn = 0;
  bufi = 0;
  global_clock = (Dura){ 0, 0 };
  next_event = (Dura){ 0, 0 };
  next_blink = (Dura){ 0, MHZ*1000 }; // 1 ms delay before doing anything
  led_is_on = false;
  tick = ARM_DWT_CYCCNT;
  runlevel = RUNPROG;
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
        Serial.println(bufi);
        Serial.send_now();
      }
      count_me = 0;
    }
  }

  // Read anything waiting on the serial port

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

