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
int digi[DIG] = { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
int led_is = 23;

#define ANA 1
int16_t wave[256];


/*******************************
 * Duration class, effectively *
 *******************************/

struct Dura {
  int s;  // Seconds
  int k;  // Clock ticks

  bool is_empty() { return s == 0 && k == 0; }

  bool is_valid() { return s >= 0 && k >= 0 && k < HTZ; }

  int compare(Dura d) { return (s < d.s) ? -1 : ((s > d.s) ? 1 : ((k < d.k) ? -1 : (k > d.k) ? 1 : 0)); }

  bool operator< (Dura d) { return s < d.s || ((s == d.s) && k < d.k); }

  void normalize() {
    if (k >= HTZ) {
      int xs = k / (HTZ);
      s += xs;
      k -= xs*HTZ;
    }
    else if (k < 0) {
      int xs = ((HTZ - 1) - k)/HTZ;
      s -= xs;
      k += HTZ*xs;
    }      
  }

  void operator+= (Dura d) {
    s += d.s;
    k += d.k;
    while (k >= HTZ) { k -= HTZ; s += 1; }
  }

  void operator+= (int ticks) {
    k += ticks;
    while (k >= HTZ) { k -= HTZ; s += 1; }
  }

  Dura or_smaller(Dura d) { return (s < d.s) ? *this : ((s > d.s) ? d : ((k > d.k) ? d : *this)); }

  int64_t as_us() { return (((int64_t)s)*HTZ + k)/MHZ; }

  void write_8(byte* target) {
    int es = 10000000;
    int x = 0;
    int eu = MHZ*100000;
    int i = 0;
    for (; es > 0 && (x = (s/es)%10) == 0; es /= 10) {}
    for (; es > 0; es /= 10, i++) { x = (s/es)%10; target[i] = (byte)(x + '0'); }
    if (i == 0) { target[0] = '0'; target[1] = '.'; i = 2; }
    else if (i < 8) { target[i++] = '.'; }
    for (; i < 8 && eu >= MHZ; eu /= 10, i++) { x = (k/eu)%10; target[i] = (byte)(x + '0'); }
  }

  void write_15(byte* target) {
    int i = 0;
    for (int es =   10000000; es >    0; es /= 10, i++) target[i] = (byte)('0' + ((s/es)%10));
    target[i++] = '.';
    for (int eu = MHZ*100000; eu >= MHZ; eu /= 10, i++) target[i] = (byte)('0' + ((k/eu)%10));
  }

  void parse(byte* input, int n) {
    int s = 0;
    int u = 0;
    int nu = 0;
    int i = 0;
    // Seconds before decimal point
    for (; i<n && input[i] != '.'; i++) {
      byte b = input[i] - '0';
      if (b < 10) s = s*10 + b;
      else {
        this->s = this->k = -1;
        return;
      }
    }
    if (i+1 < n) {
      // Found decimal point with space afterwards.  Fractions of a second.
      i++;
      for (; i<n && nu < 6; i++, nu++) {
        byte b = input[i] - '0';
        if (b < 10) u = u*10 + b;
        else {
          this->s = this->k = -1;
          return;
        }
      }
      for (; nu < 6; nu++) u = u*10;   // Pad out to microseconds
    }
    this->s = s;
    this->k = u*MHZ;
  }
};



/************************
 * Protocol information *
 ************************/

#define PROT 254

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

  void init() { *this = {{0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, 'u', ' ', 255, 255}; }

  bool parse_labeled(byte label, byte *input) {
    Dura x; x = {0, 0}; x.parse(input, 8);
    if (x.s < 0 || x.k < 0) return false;
    switch (label) {
      case 't': t = x; break;
      case 'd': d = x; break;
      case 's': s = x; break;
      case 'z': z = x; break;
      case 'p': p = x; break;
      case 'q': q = x; break;
      case 'w': p = x; break;
      default: return false;
    }
    return true;
  }

  bool parse_all(byte *input) {
    for (int i = 1; i < 5; i++) if (input[i*8] != ';') return false;
    Dura x; x = {0, 0};
    // Manually unrolled loop
    x.parse(input, 0*9); if (!x.is_valid()) return false; t = x;
    x.parse(input, 1*9); if (!x.is_valid()) return false; d = x;
    x.parse(input, 2*9); if (!x.is_valid()) return false; s = x;
    x.parse(input, 3*9); if (!x.is_valid()) return false; z = x;
    x.parse(input, 4*9); if (!x.is_valid()) return false; p = x;
    x.parse(input, 5*9); if (!x.is_valid()) return false; q = x;
    return true;
  }

  static void init(Protocol *ps, int &pi) {
    for (int i = 0; i < DIG+ANA; i++) ps[i].init();
    for (int i = DIG; i < DIG+ANA; i++) ps[i].j = 'l';
    pi = DIG+ANA;
  }

  byte append(int &pi) {
    if (pi < PROT) {
      next = (byte)pi;
      pi++;
      return next;
    }
    else return 255;
  }
};

Protocol protocols[PROT];
int proti = DIG+ANA;
Protocol not_a_protocol = (Protocol){{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, ' ', ' ', 255, 255};



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

  void write(byte *target) {
    int ns = (nstim > 999999999 || nstim < 0) ? 999999999 : nstim;
    int sm = (smiss > 999999 || smiss < 0) ? 999999 : smiss;
    int np = (npuls > 999999999 || npuls < 0) ? 999999999 : npuls;
    int pm = (pmiss > 999999 || pmiss < 0) ? 999999 : pmiss;
    int e0 = emax0 / MHZ; e0 = (e0 > 99999 || e0 < 0) ? 99999 : e0;
    int e1 = emax1 / MHZ; e1 = (e1 > 99999 || e1 < 0) ? 99999 : e1;
    int64_t t0 = toff0.as_us(); t0 = (t0 > 9999999999ll || t0 < 0) ? 9999999999ll : t0;
    int64_t t1 = toff1.as_us(); t1 = (t1 > 9999999999ll || t1 < 0) ? 9999999999ll : t1;
    snprintf((char*)target, 60, "%09d%06d%09d%06d%05d%05d%10lld%10lld", ns, sm, np, pm, e0, e1, t0, t1);
  }
};


#define C_ZZZ 0
#define C_WAIT 1
#define C_LO 2
#define C_HI 3

#define CHAN (DIG+ANA)

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

  void init(int index) {
    *this = (Channel){ {0, 0}, {0, 0}, {0, 0}, 0, 0, 1, 255, 255, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0} };
    pin = (index < DIG) ? digi[index] : 255;
    zero = (index < CHAN) ? index : 255;
  }

  void refresh(Protocol *ps) {
    e = (ChannelError){0, 0, 0, 0, 0, 0, 0, 0};
    t = yn = pq = (Dura){0, 0};
    runlevel = C_ZZZ;
    disabled = 0;
    who = zero;
    while (who < PROT && ps[who].next < PROT) who = ps[who].next;
  }

  void pin_low() { if (who != 255) digitalWrite(pin, LOW); }
  void pin_high() { if (who != 255) digitalWrite(pin, HIGH); }

  void pin_off(Protocol *ps) {
    if (who != 255) {
      if (ps[who].i == 'i') digitalWrite(pin, HIGH);
      else                  digitalWrite(pin, LOW);
    }
  }
  void pin_on(Protocol *ps) {
    if (who != 255) {
      if (ps[who].i == 'i') digitalWrite(pin, LOW);
      else                  digitalWrite(pin, HIGH);
    }
  }

  bool run_next_protocol(Protocol *ps, Dura d) {
    Protocol *p = ps + who;
    pin_low();
    if (p->next < 255) {
      p = ps + p->next;
      pin_off(ps);
      runlevel = C_WAIT;
      t = p->t; t += d;
      yn = p->d; yn += d;
    }
    else {
      who = 255;
      runlevel = C_ZZZ;
      disabled = 1;
      return false;
    }
  }

  Dura advance(Dura d, Protocol *ps) {
    bool started_yn = false;
    bool started_pq = false;
tail_recurse:
    if (disabled || runlevel == C_ZZZ) return (Dura){0,0};
    if (runlevel == C_WAIT) {
      // Only relevant times are c->t and c->yn
      Dura *x = (yn < t) ? &yn : &t;
      if (d < *x) return *x;
      else {
        if (yn < t) {
          pin_on(ps);
          started_yn = true;
          started_pq = true;
          runlevel = C_HI;
          pq = yn; pq += ps[who].p;
          yn += ps[who].s;
          e.nstim++;
          e.npuls++;
          // TODO--timing errors here!
        }
        else {
          pin_low();
          if (started_yn) { started_yn = false; e.smiss++; }
          if (started_pq) { started_pq = false; e.pmiss++; }
          run_next_protocol(ps, t);
        }
        goto tail_recurse;   // Functionally: return advance(d, ps, started_yn, started_pq);      
      }
    }
    else {
      // Relevant times are c->t, c->yn, and c->pq
      bool pq_first = (pq < yn) && (pq < t);
      bool yn_first = !pq_first && (yn < t);
      Dura *x = pq_first ? &pq : (yn_first ? &yn : &t);
      if (d < *x) return *x;
      else {
        if (pq_first) {
          if (runlevel == C_LO) {
            pin_on(ps);
            started_pq = true;
            runlevel = C_HI;
            pq += ps[who].p;
            e.npuls++;
            // TODO--timing errors here!
          }
          else {
            pin_off(ps);
            runlevel = C_LO;
            pq += ps[who].q;
            if (started_pq) { started_pq = false; e.pmiss++; }
          }
        }
        else if (yn_first) {
          if (runlevel == C_HI) pin_off(ps);
          runlevel = C_WAIT;
          yn += ps[who].z;
          if (started_pq) { started_pq = false; e.pmiss++; }
          if (started_yn) { started_yn = false; e.smiss++; }
        }
        else {
          // t exhausted
          pin_low();
          if (started_yn) { started_yn = false; e.smiss++; }
          if (started_pq) { started_pq = false; e.pmiss++; }
          run_next_protocol(ps, t);
        }
        goto tail_recurse;
      }
    }
  }

  static void init(Channel *cs){ for (int i = 0; i < CHAN; i++) cs[i].init(i); }

  static void refresh(Channel *cs, Protocol *ps) { for (int i = 0; i < CHAN; i++) cs[i].refresh(ps); }

  static Dura advance(Channel *cs, Dura d, Protocol *ps) {
    Dura x = (Dura){0, 0};
    for (int i = 0; i < DIG; i++) if (!cs[i].disabled) {
      Dura y = cs[i].advance(d, ps);
      if (!y.is_empty()) {
        if (x.is_empty() || y < x) x = y;
      }
    }
    return x;
  }
};

Channel channels[CHAN];
Channel not_a_channel = (Channel){ {0, 0}, {0, 0}, {0, 0}, 0, 128, 1, 255, 255, 0, 0, 0, {0, 0, 0, 0, 0, 0, {0, 0}, {0, 0}}};





/*****************************************
**END C++ish SECTION, BEGIN Cish SECTION**
*****************************************/


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
  int i = i0;
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

int yelln = 0;

void yell(const char* msg) {
  Serial.write(msg, strlen(msg));
  yelln += 1;
  if (yelln > 78) {
    yelln = 0;
    Serial.write("\n", 1);
  }
  Serial.send_now();
}



/*********************************************
 * Initialization -- run once only at start! *
 *********************************************/

void ensure_eeprom_is(const byte* msg, int offset, int n, int zeroIfBefore) {
  int i = offset;
  for (; i < offset+n; i++) {
    byte c = EEPROM.read(i);
    byte m = msg[i-offset];
    if (c != m) {
      EEPROM.write(i, m);
      if (!m) break;
    }
  }
  if (i < zeroIfBefore) {
    byte c = EEPROM.read(i);
    if (c) EEPROM.write(i, 0);
  }
}

void read_my_eeprom() {
  // Read identifying tag (up to max allowed size)
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

void write_my_eeprom(const byte *msg) {
  ensure_eeprom_is((byte*)"stim1.0 ", 0, (msg) ? 8 : 9, WHON);
  if (msg) ensure_eeprom_is(msg, 8, WHON-2-8, WHON);
  read_my_eeprom();
}

void init_wave() {
  for (int i = 0; i < 256; i++) {
    wave[i] = (int16_t)(2047 + (short)round(2047*sin(i * 2 * M_PI)));
  }
}


/***********
 * Runtime *
 ***********/

volatile int tick;       // Last CPU clock count
Dura global_clock;       // Time since start of running.
Dura next_event;         // Time of next event.  Just busywait until then.

#define MIN_BUSY_US 1000
#define MAX_BUSY_US 10000
Dura io_anyway;          // Time at which you need to read I/O even if it may interfere with events

#define LOCKED  -3
#define ERRDONE -2
#define ERRSTOP -1
#define RUNSTOP  0
#define RUNSET   1
#define GOGOGO   2
volatile int runlevel;   // -2 error; -1 stopping to error; 0 ran; 1 accepting commands; 2 running

volatile bool led_is_on;

void error_with_message(const char* what, int n, const char* detail, int m) {
  if (errn == 0) {
    for (int i = 0; i < n && errn < MSGN; i++) { char c = what[i]; msg[errn] = c; if (c == 0) break; errn += 1; }
    for (int i = 0; i < m && errn < MSGN; i++) { char c = detail[i]; msg[errn] = c; if (c == 0) break; errn += 1; }
  }
  if (runlevel != ERRDONE) runlevel = ERRSTOP;
}

void error_with_message(const char* what, const char* detail, int m) { error_with_message(what, MSGN, detail, m); }

void error_with_message(const char* what, int n, const char* detail) { error_with_message(what, n, detail, MSGN); }

void error_with_message(const char* what, const char* detail) { error_with_message(what, MSGN, detail, MSGN); }

void go_go_go() {
  if (runlevel >= RUNSTOP && runlevel <= RUNSET) {
    runlevel = LOCKED;
    if (led_is_on) {
      led_is_on = false;
      channels[led_is].pin_low();
    }
    int found = 0;
    for (int i = 0; i < DIG; i++) {
      Channel *c = channels + i;
      if (!c->disabled) {
        c->who = c->zero;
        Protocol *p = protocols + c->who;
        c->pin_off(p);
        c->t = p->t;
        c->yn = p->d;
        c->runlevel = C_WAIT;
        if (found) next_event = next_event.or_smaller(p->d);
        else next_event = p->d;
        found += 1;
      }
    }
    global_clock = (Dura){0, 0};
    io_anyway = global_clock;
    io_anyway += MHZ * MAX_BUSY_US;
    tick = ARM_DWT_CYCCNT;
    runlevel = GOGOGO;
  }
}



/*******************
 * Command Parsing *
 *******************/

Channel* process_get_channel(byte ch) {
  if (ch >= 'A' && ch <= 'Z' && ch != 'Y') {
    return channels + ((ch == 'Z') ? 25 : (ch - 'A'));
  } 
  else {
    char tiny[2];
    tiny[0] = ch;
    tiny[1] = 0;
    error_with_message("Unknown channel ", -1, tiny, 1);
    return &not_a_channel; 
  }
}

Protocol* process_get_protocol(byte ch) {
  Channel *c = process_get_channel(ch);
  if (c->who != 255) {
    return protocols + c->who;
  }
  else {
    char tiny[2];
    tiny[0] = ch;
    tiny[1] = 0;
    error_with_message("No protocol for channel ", -1, tiny, 1);
    return &not_a_protocol;
  }
}

void process_reset() {
  Protocol::init(protocols, proti);
  Channel::init(channels);
  errn = 0;
  runlevel = RUNSET;
}

void process_refresh() {
  Channel::refresh(channels, protocols);
}

void process_start_running(byte who) {
  // TODO - write me
}

void process_stop_running(byte who) {
  // TODO - write me
}

void process_say_the_time() {
  msg[0] = '^';
  ((runlevel == GOGOGO) ? global_clock : (Dura){0, 0}).write_15(msg+1);
  msg[16] = '$';
  msg[17] = 0;
  write_from_msg();
}

void process_error_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') { 
    bufi = advance_command(buf, (buf[0] == '^') ? 1 : 0, bufi);
    return;
  }
  // Was a fixed-length command.  Pick out the meaningful ones.
  switch(buf[1]) {
    case '@': Serial.write("~!", 2); Serial.send_now(); break;
    case '.': process_reset(); break;
    case '#': write_from_msg(); break;
    case '?': write_who_am_i(); break;
    default: break;
  }
  bufi = shift_buffer(buf, bufi, 2);
}

void process_complete_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') {
    error_with_message("unknown command starting: ", (char*)buf, 1);
    bufi = advance_command(buf, (buf[0] == '^') ? 1 : 0, bufi);
    return;
  }
  switch(buf[1]) {
    case '@': Serial.write("~/"); Serial.send_now(); break;
    case '.': process_reset(); break;
    case '"': process_refresh(); break;
    case '#': process_say_the_time(); break;
    case '?': write_who_am_i(); break;
    case '/': break;
    default:
      if (buf[1] >= 'A' && buf[1] <= 'Z' && buf[1] != 'Y') {
        if (bufi < 3) return;
        if (buf[2] == '/') { shift_buffer(buf, bufi, 3); return; }
      }
      error_with_message("Command not valid (run complete): ", (char*)buf, 2);
  }
  bufi = shift_buffer(buf, bufi, 2);
}

void process_init_command() {
  if (bufi < 2) return;
  if (buf[0] == '^') {
    int i = 1;
    for (; i < bufi && buf[i] != '$'; i++) {}
    if (i < bufi) {
      if (memcmp("IDENTITY", buf+1, 8) != 0) {
        error_with_message("Expected ^IDENTITY found:", (char*)buf, bufi);
        return;
      }
      ensure_eeprom_is(buf + 9, 8, i-1, WHON - 2);
      bufi = shift_buffer(buf, bufi, i+1);
    }
    else if (bufi >= WHON) {
      error_with_message("^ without $ in: ", (char*)buf, bufi);
    }
    return;
  }
  byte b = buf[1];
  if (b >= 'A' && b <= 'Z' && b != 'Y') {
    if (bufi < 3) return;
    byte ch = b;
    b = buf[2];
    switch(b) {
      case '@': Serial.write("~."); Serial.send_now(); break;
      case '*': process_start_running(ch); break;
      case '/': break;
      case '&': /* TODO */ break;
      case '=':
      case ':':
        if (bufi < 56) return;
        if (ch == 'Z') {
          error_with_message("Cannot set all on channel Z: ", (char*)buf, 12);
        }
        else {
          if (!process_get_protocol(ch)->parse_all(buf+3)) {
            error_with_message("Bad =: ", (char*)buf, 53);
          }         
        }
        bufi = shift_buffer(buf, bufi, 56);
        if (b == ':') process_start_running(ch);
        return;
        break;
      case 'u': process_get_protocol(ch)->i = 'u'; break;
      case 'i': process_get_protocol(ch)->i = 'i'; break;
      case 'l': if (ch != 'Z') error_with_message("Analog required: ", (char*)buf, 3); else process_get_protocol(ch)->j = 'l'; break;
      case 'r': if (ch != 'Z') error_with_message("Analog required: ", (char*)buf, 3); else process_get_protocol(ch)->j = 'r'; break;
      case 't':
      case 'd':
      case 's':
      case 'z':
      case 'p':
      case 'q':
      case 'w': 
        if (bufi < 11) return;
        if ((b == 'w' && ch != 'Z') || (ch == 'Z' && (b == 'p' || b == 'q'))) {
          error_with_message("Bad command for channel: ", (char*)buf, 11);
        }
        else {
          if (!process_get_protocol(ch)->parse_labeled(b, buf+3)) {
            error_with_message("Bad duration format: ", (char*)buf, 11);
          }
        }
        bufi = shift_buffer(buf, bufi, 11);
        return;
      case 'a': /* TODO */ return;
      default:
        error_with_message("Channel command not valid (setting): ", (char*)buf, 3);
    }
    bufi = shift_buffer(buf, bufi, 3);
  }
  else {
    switch(b) {
      case '@': Serial.write("~."); Serial.send_now(); break;
      case '.': process_reset(); break;
      case '#': process_say_the_time(); break;
      case '?': write_who_am_i(); break;
      case '/': break;
      case '*': process_start_running(0); break;
      default:
        error_with_message("Command not valid (setting): ", (char*)buf, 2);
    }
    bufi = shift_buffer(buf, bufi, 2);
  }
}

void process_runtime_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') {
    error_with_message("Channel command not valid(running): ", (char*)buf, 2);
    bufi = advance_command(buf, (buf[0] == '^') ? 1 : 0, bufi);
    return;
  }
  byte b = buf[1];
  if (b >= 'A' && b <= 'Z' && b != 'Y') {
    if (bufi < 3) return;
    byte ch = b;
    b = buf[2];
    switch(b) {
      case '@': /* TODO */ break;
      case '#':
        msg[0] = '^';
        process_get_channel(ch)->e.write(msg+1);
        msg[61] = '$';
        msg[62] = 0;
        Serial.write((char*)msg);
        Serial.send_now();
        break;
      case '/': process_stop_running(ch); break;
      default:
        error_with_message("Channel command not valid (running): ", (char*)buf, 3);
    }
    bufi = shift_buffer(buf, bufi, 3);
  }
  else {
    switch(b) {
      case '@': Serial.write("~*"); Serial.send_now(); break;
      case '.': process_reset(); break;
      case '#': process_say_the_time(); break;
      case '?': write_who_am_i(); break;
      case '/': process_stop_running(0); break;
      default:
        error_with_message("Command not valid (running): ", (char*)buf, 2);
    }
    bufi = shift_buffer(buf, bufi, 2);    
  }
}



/*************
 * Main Loop *
 *************/

// Setup runs once at reset / power on
void setup() {
  runlevel = RUNSTOP;
  read_my_eeprom();
  process_reset();
  Serial.begin(115200);
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  errn = 0;
  bufi = 0;
  global_clock = (Dura){ 0, 0 };
  next_event = (Dura){ 0, 0 };
  led_is_on = false;
  tick = ARM_DWT_CYCCNT;
  runlevel = RUNSET;
  yell("S");
}

// Runs over and over forever (after setup() finishes)
void loop() {
  int now = ARM_DWT_CYCCNT;
  int delta = now - tick;
  tick = now;
  global_clock += delta;
  bool urgent = false;
  if (next_event < global_clock) {
    urgent = true;
    if (runlevel == GOGOGO) { 
      /* TODO */
      yell("go");
    }
    else {
      if (led_is_on) channels[led_is].pin_low();
      else channels[led_is].pin_high();
      led_is_on = !led_is_on;
      delta = 0;
      switch(runlevel) {
        case RUNSET: delta = (led_is_on) ? MHZ*100000 : MHZ*400000;
        case RUNSTOP: delta = MHZ*1000000;
        case ERRSTOP: delta = MHZ*100000;
        case ERRDONE: delta = MHZ*100000;
        default: delta = MHZ*1000;
      }
      next_event += delta;
      yell("L");
    }
  }
  if (!urgent || io_anyway < global_clock) {
    Dura soon = global_clock;
    soon += MHZ * MIN_BUSY_US;
    if (next_event < soon || io_anyway < next_event) {
      // Do not need to busywait for next event
      drain_to_buf();
      switch(runlevel) {
        case ERRDONE: process_error_command(); break;
        case RUNSTOP: process_complete_command(); break;
        case RUNSET: process_init_command(); break;
        case GOGOGO: process_runtime_command(); break;
        default: break;
      }
      now = ARM_DWT_CYCCNT;
      delta = now - tick;
      tick = now;
      global_clock += delta;
      io_anyway = global_clock;
      io_anyway += MHZ * MAX_BUSY_US;
      yell("p");
    }
    else yell("_");
  }
  delay(100);
}
