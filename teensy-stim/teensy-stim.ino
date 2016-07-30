/*
  Run a digital and/or analog stimulus protocol from a Teensy 3.1+ board.
 */

#include <EEPROM.h>
#include <math.h>

#define MHZ 72
#define HTZ 72000000

#define LED_PIN 13

#define DIG 24
int digi[DIG] = { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

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
  Dura q;    // Pulse off time (analog: amplitude 0-2047)
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

#define ANALOG_ZERO 2047
#define ANALOG_AMPL 2047

#define CHAN (DIG+ANA)

struct Channel {
  Dura t;         // Time remaining
  Dura yn;        // Time until next stimulus status switch
  Dura pq;        // Time until next pulse status switch
  byte runlevel;  // Runlevel 0 = off, 1 = stim off, 2 = stim on pulse off, 3 = pulse on
  byte pin;       // The digital pin number for this output, or 255 = analog out
  byte who;       // Which protocol we're running now, 255 = none
  byte zero;      // Initial protocol to start at (used only for resetting), 255 = none
  ChannelError e; // Error statistics

  void init(int index) {
    *this = (Channel){ {0, 0}, {0, 0}, {0, 0}, C_ZZZ, 0, 255, 255, {0, 0, 0, 0, 0, 0, 0, 0} };
    pin = (index < DIG) ? digi[index] : 255;
    zero = (index < CHAN) ? index : 255;
  }

  void refresh(Protocol *ps) {
    e = (ChannelError){0, 0, 0, 0, 0, 0, 0, 0};
    t = yn = pq = (Dura){0, 0};
    runlevel = C_ZZZ;
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
    pin_off(p);
    if (p->next < 255) {
      p = ps + p->next;
      pin_off(p);
      runlevel = C_WAIT;
      t = p->t; t += d;
      yn = p->d; yn += d;
    }
    else {
      who = 255;
      digitalWrite(pin, LOW);
      runlevel = C_ZZZ;
      return false;
    }
  }

  bool alive() { return runlevel != C_ZZZ && who != 255; }

  Dura advance(Dura d, Protocol *ps) {
    bool started_yn = false;
    bool started_pq = false;
tail_recurse:
    if (!alive()) return (Dura){0,0};
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
    for (int i = 0; i < DIG; i++) if (cs[i].alive()) {
      Dura y = cs[i].advance(d, ps);
      if (!y.is_empty()) {
        if (x.is_empty() || y < x) x = y;
      }
    }
    return x;
  }
};

Channel channels[CHAN];
Channel not_a_channel = (Channel){ {0, 0}, {0, 0}, {0, 0}, C_ZZZ, 128, 255, 255, {0, 0, 0, 0, 0, 0, {0, 0}, {0, 0}}};





/*****************************************
**END C++ish SECTION, BEGIN Cish SECTION**
*****************************************/


/*************************
 * Error and I/O buffers *
 *************************/

#ifdef YELL_DEBUG
#define YELLN 39
int yelln = 0;

void yell(const char* msg) {
  int k = strlen(msg);
  Serial.write(msg, k);
  yelln += k;
  if (yelln > YELLN) {
    yelln = 0;
    Serial.write("\n", 1);
  }
  Serial.send_now();
}

void yell2ib(int i, int j, const byte* text) {
  char buffer[23+128+1];
  snprintf(buffer, 23, "%d %d ", i, j);
  int k = 0, l = 0;
  while (l < 23 && buffer[l]) l++;
  for (; k < j && k < 128 && text[k]; k++) buffer[l+k] = text[k];
  buffer[l+k] = '\n';
  buffer[l+k+1] = 0;
  Serial.write(buffer, l+k+1);
  Serial.send_now();
}

void yellr3d(int rl, Dura g, Dura n, Dura io) {
  char buffer[54];
  buffer[0] = '^';
  snprintf(buffer+1, 5, "%d  ", rl);
  g.write_15((byte*)buffer+4);
  buffer[19] = ' ';
  n.write_15((byte*)buffer+20);
  buffer[35] = ' ';
  n.write_15((byte*)buffer+36);
  buffer[51] = '$';
  buffer[52] = '\n';
  buffer[53] = 0;
  Serial.write(buffer,53);
  Serial.send_now();
}
#endif

#define MSGN 62
#define BUFN 128

byte msg[MSGN];
int erri = 0;

byte buf[BUFN];
int bufi = 0;

void discard(byte* buffer, int& iN, int shift) {
  if (shift > iN) iN = 0;
  else if (shift >= 1) {
    for (int j = shift; j < iN; j++) buffer[j-shift] = buffer[j];
    int result = iN - shift;
#ifdef YELL_DEBUG
    yell2ib(iN, result, buffer);
#endif
    iN = result;    
  }
}

void discard_buf(int shift) { discard(buf, bufi, shift); }

void discard_command() {
  int i = 1;
  while (i < bufi) {
    byte b = buf[i];
    if (b == '~' || b == '^') break;
    i++;
  }
  discard(buf, bufi, i);
}

void drain_to_buf() {
  int av = Serial.available();
  if (av > 0) {
#ifdef YELL_DEBUG
    int old = bufi;
#endif
    while ((av--) > 0 && bufi < BUFN) buf[bufi++] = Serial.read();
#ifdef YELL_DEBUG
    yell2ib(old, bufi, buf);
#endif
  }
}

void tell_msg() {
  if (erri > 0) Serial.write(msg, erri);
  else {
    int n = 0;
    while (n < MSGN && msg[n] != 0) n++;
    Serial.write(msg, n);
  }
  Serial.send_now();
}



/*********************************************
 * Initialization -- run once only at start! *
 *********************************************/

#define WHON 62
byte whoami[WHON];
int whoi = 0;

bool eeprom_set(const byte* msg, int eepi, int max, bool zero) {
  int i = 0;
  bool changed = false;
  for (; i < max; i++) {
    byte mi = msg[i];
    if (!mi) break;
    byte ei = EEPROM.read(i+eepi);
    if (mi != ei) {
      changed = true;
      EEPROM.write(i+eepi, mi);
    }
  }
  if (zero) {
    byte ei = EEPROM.read(i+eepi);
    if (ei != 0) {
      changed = true;
      EEPROM.write(i+eepi, 0);
    }
  }
  return changed;
}

void eeprom_read_who() {
  whoami[0] = '^';
  int i = 1;
  for (; i < WHON-1; i++) {
    byte ei = EEPROM.read(i-1);
    if (!ei) break;
    whoami[i] = ei;
  }
  whoami[i] = '$';
  whoi = i+1;
  if (i+1 < WHON) whoami[i+1] = 0;
}

void tell_who() { Serial.write(whoami, whoi); Serial.send_now(); }

void init_eeprom() {
  bool first = eeprom_set((byte*)"stim1.0 ", 0, WHON, false);
  if (first) eeprom_set((byte*)"", 8, WHON, true);
  eeprom_read_who();
}

void init_analog() {
  for (int i = 0; i < 256; i++) wave[i] = (int16_t)(ANALOG_ZERO + (short)round(ANALOG_AMPL*sin(i * 2 * M_PI)));
  analogWriteResolution(12);
  analogWrite(A14, ANALOG_ZERO);
}

void init_digital() {
  for (int i = 0; i<DIG; i++) { pinMode(i, OUTPUT); digitalWrite(i, LOW); }
}



/***********
 * Runtime *
 ***********/

volatile int tick;       // Last CPU clock count
Dura global_clock;       // Time since start of running.
Dura next_event;         // Time of next event.  Just busywait until then.

#define MIN_BUSY_US 1000
#define MAX_BUSY_US 20000
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
  if (erri == 0) {
    for (int i = 0; i < n && erri < MSGN; i++) { char c = what[i]; msg[erri] = c; if (c == 0) break; erri += 1; }
    for (int i = 0; i < m && erri < MSGN; i++) { char c = detail[i]; msg[erri] = c; if (c == 0) break; erri += 1; }
  }
  if (runlevel != ERRDONE) runlevel = ERRSTOP;
}

void error_with_message(const char* what, const char* detail, int m) { error_with_message(what, MSGN, detail, m); }

void error_with_message(const char* what, int n, const char* detail) { error_with_message(what, n, detail, MSGN); }

void error_with_message(const char* what, const char* detail) { error_with_message(what, MSGN, detail, MSGN); }

void analog_cooldown() {
  if (runlevel == ERRSTOP) {
    if (channels[DIG].who == 255) {
      if (channels[DIG].zero != 255) analogWrite(A14, ANALOG_ZERO);
      runlevel = ERRDONE;
    }
    else {
      // TODO--nice cooldown!
      analogWrite(A14, ANALOG_ZERO);
      runlevel = ERRDONE;
    }
  }
}

void go_go_go() {
  if (runlevel == RUNSET) {
    runlevel = LOCKED;
    if (led_is_on) {
      led_is_on = false;
      digitalWrite(LED_PIN, LOW);
    }
    int found = 0;
    for (int i = 0; i < DIG; i++) {
      Channel *c = channels + i;
      c->who = c->zero;
      if (c->who == 255) continue;
      Protocol *p = protocols + c->who;
      c->pin_off(p);
      c->t = p->t;
      c->yn = p->d;
      c->runlevel = C_WAIT;
      if (found) next_event = next_event.or_smaller(p->d);
      else next_event = p->d;
      found += 1;
    }
    global_clock = (Dura){0, 0};
    io_anyway = global_clock;
    io_anyway += MHZ * MAX_BUSY_US;
    tick = ARM_DWT_CYCCNT;
    runlevel = GOGOGO;
  }
  else if (runlevel != ERRDONE && runlevel != ERRSTOP) {
    error_with_message("Attempt to start running from invalid state.", "");
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
    error_with_message("Unknown channel ", tiny, 1);
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
    error_with_message("No protocol for channel ", tiny, 1);
    return &not_a_protocol;
  }
}

void process_reset() {
  Protocol::init(protocols, proti);
  Channel::init(channels);
  erri = 0;
  runlevel = RUNSET;
}

void process_refresh() {
  Channel::refresh(channels, protocols);
}

void process_start_running() {
  // TODO - write me
}

void process_start_running(byte who) {
  // TODO - write me
}

void process_stop_running(byte who) {
  // TODO - write me
}

void process_stop_running() {
  // TODO - write me
}

void process_say_the_time() {
  msg[0] = '^';
  ((runlevel == GOGOGO) ? global_clock : (Dura){0, 0}).write_15(msg+1);
  msg[16] = '$';
  msg[17] = 0;
  tell_msg();
}

void process_error_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') {
    discard_command();
    return;
  }
  // Was a fixed-length command.  Pick out the meaningful ones.
  switch(buf[1]) {
    case '@': Serial.write("~!", 2); Serial.send_now(); break;
    case '.': process_reset(); break;
    case '#': tell_msg(); break;
    case '?': tell_who(); break;
    default: break;
  }
  discard_buf(2);
}

void process_complete_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') {
    error_with_message("unknown command starting: ", (char*)buf, 1);
    discard_command();
    return;
  }
  switch(buf[1]) {
    case '@': Serial.write("~/"); Serial.send_now(); break;
    case '.': process_reset(); break;
    case '"': process_refresh(); break;
    case '#': process_say_the_time(); break;
    case '?': tell_who(); break;
    case '/': break;
    default:
      if (buf[1] >= 'A' && buf[1] <= 'Z' && buf[1] != 'Y') {
        if (bufi < 3) return;
        if (buf[2] == '/') { discard_buf(3); return; }
      }
      error_with_message("Command not valid (run complete): ", (char*)buf, 2);
  }
  discard_buf(2);
}

void process_init_command() {
  if (bufi < 2) return;
  if (buf[0] == '^') {
    int i = 1;
    for (; i < bufi && buf[i] != '$'; i++) {}
    if (i < bufi) {
      if (memcmp("IDENTITY", buf+1, 8) != 0) {
        error_with_message("Expected ^IDENTITY found:", (char*)buf, bufi);
        discard_command();
        return;
      }
      eeprom_set(buf + 9, 8, i - 9, true);
      eeprom_read_who();
      discard_buf(i+1);
    }
    else if (bufi >= WHON) {
      error_with_message("^ without $ in: ", (char*)buf, bufi);
      discard_command();
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
        discard_buf(56);
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
        discard_buf(11);
        return;
      case 'a': /* TODO */ return;
      default:
        error_with_message("Channel command not valid (setting): ", (char*)buf, 3);
    }
    discard_buf(3);
  }
  else {
    switch(b) {
      case '@': Serial.write("~."); Serial.send_now(); break;
      case '.': process_reset(); break;
      case '#': process_say_the_time(); break;
      case '?': tell_who(); break;
      case '/': break;
      case '*': process_start_running(); break;
      default:
        error_with_message("Command not valid (setting): ", (char*)buf, 2);
    }
    discard_buf(2);
  }
}

void process_runtime_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') {
    error_with_message("Channel command not valid(running): ", (char*)buf, 2);
    discard_command();
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
    discard_buf(3);
  }
  else {
    switch(b) {
      case '@': Serial.write("~*"); Serial.send_now(); break;
      case '.': process_reset(); break;
      case '#': process_say_the_time(); break;
      case '?': tell_who(); break;
      case '/': process_stop_running(); break;
      default:
        error_with_message("Command not valid (running): ", (char*)buf, 2);
    }
    discard_buf(2);   
  }
}



/*************
 * Main Loop *
 *************/

// Setup runs once at reset / power on
void setup() {
  runlevel = RUNSTOP;
  init_eeprom();
  init_digital();
  init_analog();
  process_reset();
  Serial.begin(115200);
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  erri = 0;
  bufi = 0;
  global_clock = (Dura){ 0, 0 };
  next_event = (Dura){ 0, 0 };
  led_is_on = false;
  tick = ARM_DWT_CYCCNT;
  runlevel = RUNSET;
}

#ifdef YELL_DEBUG
int lastrun = -10;
#endif

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
    }
    else {
      if (led_is_on) digitalWrite(LED_PIN, LOW);
      else digitalWrite(LED_PIN, HIGH);
      led_is_on = !led_is_on;
      delta = 0;
      switch(runlevel) {
        case RUNSET:  delta = (led_is_on) ?  2000 :  998000; break;
        case RUNSTOP: delta = (led_is_on) ?  2000 : 2998000; break;
        case ERRSTOP: delta = (led_is_on) ? 10000 :   90000; break;
        case ERRDONE: delta = (led_is_on) ? 10000 :  190000; break;
        default:      delta = (led_is_on) ? 10000 :   40000; break;
      }
      next_event += MHZ * delta;
    }
  }
  if (runlevel == ERRSTOP) analog_cooldown();
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
#ifdef YELL_DEBUG
      if (io_anyway < global_clock) yell("io");
#endif
      now = ARM_DWT_CYCCNT;
      delta = now - tick;
      tick = now;
      global_clock += delta;
      io_anyway = global_clock;
      io_anyway += MHZ * MAX_BUSY_US;
    }
  }
#ifdef YELL_DEBUG
  if (lastrun != runlevel) yellr3d(runlevel, global_clock, next_event, io_anyway);
  lastrun = runlevel;
#endif
}
