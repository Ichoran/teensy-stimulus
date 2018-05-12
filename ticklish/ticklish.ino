/*
  Run a digital and/or analog stimulus protocol from a Teensy 3.1+ board.
 */

/*
Ticklish is essentially a set of simple state machines.

The _main loop_ has four persistent states and two transient states; these
are present in the variable `runlevel`.
Persistent states:
  RUN_ERROR - Indicates an error state.
    State entered whenever an invalid input is received.
    State exited only when explicitly reset with `~.` command
  RUN_PROGRAM - State for setting stimuli.
    Ticklish starts in this state.  Reset `~.` moves to this state.  Refresh `~"` will also from RUN_COMPLETED.
    State exited when a run starts: `~*` or `~A*` or `~A:100.0000;...` (or error)
  RUN_COMPLETED - Indicates that a protocol is done running.
    State entered when running state is complete.
    State exited when reset `~.` or refreshed `~"` (to run same program again).
  RUN_GO - Protocol is running.
    State entered from RUN_PROGRAM with a run command `~*` `~A*` `~A:100.0000;...`
    State exited when stimulus is complete (goes to RUN_COMPLETED state)
Transient states:
  RUN_TO_ERROR - Was running, cooling down stimuli, will turn to error within a second or so.
  RUN_LOCKED - Setting up for something (probably a run).  Transient, very brief (~1 ms).

There is also a state machine that runs every digital channel.
These are stored in the Channel struct.  In addition to a runlevel (state),
there are also three separate timers.
  Global timer `t`: when this is exhausted, the channel is done running.
  Block transition timer yn: when this is exhausted, switch to the next on or off block (as appropriate)
  Pulse transition timer pq: when this is exhausted, turn the stimulus on or off (as appropriate)
There are four runlevels:
  C_ZZZ - Channel is sleeping.  Timers are irrelevant.
  C_WAIT - In an off block.  When yn is exhausted, turn on to C_HI.
  C_LO - In an on block, but stimulus is off.  Turn it on when pq is exhausted, or go back to C_WAIT if pq is exhausted.
  C_HI - Stimulus is on!  If pq exahausted, turn off and go to C_LO.  If yn exhuasted, turn off and go to C_WAIT.
  If t is ever exhausted, turn off stimulus and go to C_ZZZ.

There is supposed to be a similar state machine for analog channels, but there isn't yet.
*/


#include <EEPROM.h>
#include <math.h>

// Note: the Teensy 3.1 and 3.2 can be "overclocked" to 96 MHz, but 72 MHz is their base operating speed.
// 72 MHz is plenty for our purposes.
// Needs to be altered for 3.5 or 3.6 if they run at full speed (120 or 180 MHz respectively).
#define MHZ 72
#define HTZ 72000000

// This is the hardware pin attached to the on-chip LED.
#define LED_PIN 13

// The digital channels supported; these correspond to letters 'A' through 'X', with the last being the LED pin.
#define DIG 24
int digi[DIG] = { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
bool assume_in[DIG] = {
  false, false, false, false, true, true, true, true, true,
  false, false, false, false, false, false, false, false, false, false, false, false, false,
  false
};

// The analog channels supported; support is a fiction currently.
#define ANA 2
#define ANALOG_DIVS 1024
int16_t wave[ANALOG_DIVS];

#define DPI 10
bool can_dpi[DIG] = {
  false, false, false, false, false, false, false, false, false, false,
  true, true, true, true, true, true, true, true, true, true, true, true, true,
  false
};
uint64_t dpi_buffer[DIG];


/******************************
 * Primary timekeeping struct *
 ******************************
 * 
 * Time is stored as seconds plus ticks, so it's dependent on the clock rate.
 * It's really intended that times will be used going forward, and that you mostly
 * want to check when one duration has passed another.
 *
 * All comparison operators could be defined, but I've only bothered with < right now.
 *
 * Only positive times are supported.
**/

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

Dura dpi_timing[DIG];


/*************************
 * Digital pulse reading *
 *************************/

struct DigitalPulses {
// Right now customized to read the DHT22 pulse signal as described in the
// Aosong Electronics manual
  byte slot;
  byte pin;
  short pulse;
  bool active;
  bool errored;
  bool v_was_hi;
  bool v_goal_hi;
  Dura next_t;
  Dura max_t;
  uint64_t bits;
  short who;

  void clear() {
    bits = 0;
    active = false;
    errored = false;
    for (int i = 0; i < DIG; i++) {
      dpi_buffer[i] = 0;
      dpi_timing[i] = (Dura){0, 0};
    }
  }

  void init(byte my_slot, byte my_pin, Dura &now) {
    slot = my_slot;
    pin = my_pin;
    pulse = -3;
    next_t = now;
    v_was_hi = false;
    active = true;
    v_was_hi = false;
    errored = false;
    bits = 0;
    who = 3;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    dpi_buffer[slot] = 0;
    next_t += MHZ*1200;
    max_t = next_t;
    max_t += MHZ*6800;  // Whole protocol should complete within 8 ms.  Actually less: readout is < 5 ms.
  }

  void run(Dura &now) {
    if (!active) return;
    if (errored || max_t < now) {
      active = false;
      return;
    }
    if (pulse < 0) {
      if (pulse == -3) {
        if (next_t < now) {
          pinMode(pin, INPUT_PULLUP);
          pulse = -2;
          dpi_timing[slot] = now;
          v_goal_hi = false;
          max_t = now;
          next_t = now;
          next_t += MHZ*60;
          max_t += MHZ*(70+50)*60;
        }
      }
      else if (pulse == -2) {
        if (digitalRead(pin) == HIGH) {
          if (next_t < now) {
            pulse = -1;
            next_t = now;
            next_t += MHZ*40;
          }
          else {
            next_t = now;
            next_t += MHZ*20;
          }
        }
      }
      else {
        if (digitalRead(pin) == LOW) {
          if (next_t < now) {
            pulse = 0;
            next_t = now;
            next_t += MHZ*25;
            v_goal_hi = true;
          }
          else {
            next_t = now;
            next_t += MHZ*40;
          }
        }
      }
    }
    else {
      if (v_goal_hi) {
        if (digitalRead(pin) == HIGH) {
          if (next_t < now) {
            v_goal_hi = false;
            next_t = now;
            next_t += MHZ*45;
          }
          else {
            next_t = now;
            next_t += MHZ*20;
          }
        }
      }
      else {
        if (digitalRead(pin) == LOW) {
          if (next_t < now) {
            bits |= (1ull << pulse);
            dpi_buffer[slot] = bits;
          }
          next_t = now;
          next_t += MHZ*20;
          pulse += 1;
          if (pulse >= 40) {
            dpi_timing[slot] = now;
            active = false;
          }
          else v_goal_hi = true;
        }
      }
    }
  }
};

/************************
 * Protocol information *
 ************************
 *
 * Struct that defines a stimulus protocol.  Fields describe how they are used.
**/

#define PROT 254

struct Protocol {
  Dura t;    // Total time
  Dura d;    // Delay
  Dura s;    // Stimulus on time
  Dura z;    // Stimulus off time
  Dura p;    // Pulse time (or period, for analog)
  Dura q;    // Pulse off time (analog: amplitude 0-2047)
  byte i;    // Invert? 'i' == yes, otherwise no
  byte j;    // Shape: 'l' = sinusoidal, 'r' = triangular, `|` = digital pulse read, other = standard digital out
  byte next; // Number of next protocol, 255 = none
  byte chan; // Channel number, 255 = none

  void init() { *this = {{0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, 'u', ' ', 255, 255}; }

  void write(byte* target) {
    t.write_8(target); target[8] = ';'; target += 9;
    d.write_8(target); target[8] = ';'; target += 9;
    s.write_8(target); target[8] = ';'; target += 9;
    z.write_8(target); target[8] = ';'; target += 9;
    p.write_8(target); target[8] = ';'; target += 9;
    q.write_8(target);                  target += 8;
    target[0] = i;
    target[1] = j;
    target[2] = '\n';
  }

  // Parse one duration given by a label
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

  // Parse all durations (digital only)
  int parse_all(byte *input) {
    for (int i = 1; i < 6; i++) if (input[i*9 - 1] != ';') return 10+i;
    Dura x; x = {0, 0};
    // Manually unrolled loop
    x.parse(input + 0*9, 8); if (!x.is_valid()) return 1; t = x;
    x.parse(input + 1*9, 8); if (!x.is_valid()) return 2; d = x;
    x.parse(input + 2*9, 8); if (!x.is_valid()) return 3; s = x;
    x.parse(input + 3*9, 8); if (!x.is_valid()) return 4; z = x;
    x.parse(input + 4*9, 8); if (!x.is_valid()) return 5; p = x;
    x.parse(input + 5*9, 8); if (!x.is_valid()) return 6; q = x;
    if (input[6*9-1] == 'i') i = 'i'; else if (input[6*9-1] == 'u') i = 'u'; else return 7;
    return 0;
  }

  // Reshuffles all protocols so only this one is ready to run.
  // We are placed first within the supplied buffer.  Length
  // is placed in &pi.
  void solo(Protocol *ps, int &pi) {
    int i = 0;
    ps[i] = *this;
    while (i+1 < PROT && ps[i].next < PROT) {
      ps[i+1] = ps[ps[i].next];  // We are copying all the data, not just pointers!
      ps[i].next = (byte)(i+1);  // Point existing one at new (probably lower) index.
      i++;
    }
    // Last one will point at 255 (terminator), and that doesn't change!
    pi = i;
  }

  // Set all protcols to empty
  static void init(Protocol *ps, int &pi) {
    for (int i = 0; i < DIG+ANA; i++) ps[i].init();
    for (int i = DIG; i < DIG+ANA; i++) ps[i].j = 'l';
    pi = 0;
  }
};

Protocol protocols[PROT]; // Slots for protocol inforation
int proti = 0;            // Next available protocol index
Protocol not_a_protocol = (Protocol){{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, ' ', ' ', 255, 255};



/*********************************
 * Channel information & running *
 *********************************
**/

#define C_ZZZ 0
#define C_WAIT 1
#define C_LO 2
#define C_HI 3

#define ANALOG_ZERO 2047
#define ANALOG_AMPL 2047
#define ANALOG_BITS 12

#define CHAN (DIG+ANA)

struct Channel {
  Dura t;         // Time remaining
  Dura yn;        // Time until next stimulus status switch
  Dura pq;        // Time until next pulse status switch
  byte runlevel;  // Runlevel 0 = off, 1 = stim off, 2 = stim on pulse off, 3 = pulse on
  byte pin;       // The digital pin number for this output, or 255 = analog out
  byte who;       // Which protocol we're running now, 255 = none
  byte zero;      // Initial protocol to start at (used only for resetting), 255 = none

  void init(int index) {
    *this = (Channel){ {0, 0}, {0, 0}, {0, 0}, C_ZZZ, 0, 255, 255 };
    pin = (index < DIG) ? digi[index] : 255;
    zero = 255;
    if (index < DIG && assume_in[index]) pinMode(index, INPUT);
  }

  void write(byte* target) {
    if (runlevel == 0) {
      if (who == 255) strncpy((char*)target, "unused\n", 8);
      else protocols[who].write(target);
    }
    else {
      target[0] = '*'; target += 1;
      t.write_8(target);  target[8] = ';'; target += 9;
      yn.write_8(target); target[8] = ';'; target += 9;
      pq.write_8(target); target[8] = ';'; target += 9;
      snprintf((char*)target, 10, ";%03d,%03d\n", pin, who);
    }
  }

  void refresh(Protocol *ps) {
    t = yn = pq = (Dura){0, 0};
    runlevel = C_ZZZ; // Debug::shout(__LINE__, pin, runlevel);
    who = zero;
    while (who < PROT && ps[who].next < PROT) who = ps[who].next;
  }

  void pin_low() { if (who != 255) digitalWrite(pin, LOW); }
  void pin_high() { if (who != 255) digitalWrite(pin, HIGH); }

  void pin_off(Protocol *ps) {
    if (who != 255 && ps[who].j != '|') {
      if (ps[who].i == 'i') digitalWrite(pin, HIGH);
      else                  digitalWrite(pin, LOW);
    }
  }
  bool pin_on(Protocol *ps, DigitalPulses *pulsar, Dura &now, byte slot) {
    if (who != 255) {
      if (ps[who].j == '|') {
        if (pulsar->active) return false;
        pulsar->init(slot, pin, now);
      }
      else if (ps[who].i == 'i') digitalWrite(pin, LOW);
      else                       digitalWrite(pin, HIGH);
    }
    return true;
  }

  bool run_next_protocol(Protocol *ps, Dura d) {
    Protocol *p = ps + who;
    if (p->j != '|') pin_off(p);
    who = p->next;
    if (who < 255) {
      p = ps + who;
      if (p->j == '|') pinMode(pin, INPUT);
      else {
        pinMode(pin, OUTPUT);
        pin_off(p);
      }
      runlevel = C_WAIT; // Debug::shout(__LINE__, pin, runlevel, d);
      t = p->t; t += d;
      yn = p->d; yn += d;
      return true;
    }
    else if (zero < 255) {
      digitalWrite(pin, LOW);
      runlevel = C_ZZZ;
      return false;
    }
    else return false;
  }

  bool alive() { return runlevel != C_ZZZ && who != 255; }

  Dura advance(Dura d, Protocol *ps, DigitalPulses *pulsar, byte slot) {
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
          if (!pin_on(ps, pulsar, d, slot)) return (Dura){-1, -1};
          started_yn = true;
          started_pq = true;
          runlevel = C_HI; // Debug::shout(__LINE__, pin, runlevel, d);
          pq = yn; pq += ps[who].p;
          yn += ps[who].s;
        }
        else {
          pin_low();
          started_yn = false;
          started_pq = false;
          run_next_protocol(ps, t);
        }
        goto tail_recurse;   // Functionally: return advance(d, ps, started_yn, started_pq);      
      }
    }
    else {
      // Relevant times are c->t, c->yn, and c->pq
      pulsar->run(d);
      bool pq_first = (pq < yn) && (pq < t);
      bool yn_first = !pq_first && (yn < t);
      Dura *x = pq_first ? &pq : (yn_first ? &yn : &t);
      if (d < *x) return *x;
      else {
        if (pq_first) {
          if (runlevel == C_LO) {
            if (!pin_on(ps, pulsar, d, slot)) return (Dura){-1, -1};
            started_pq = true;
            runlevel = C_HI; // Debug::shout(__LINE__, pin, runlevel, d);
            pq += ps[who].p;
          }
          else {
            pin_off(ps);
            runlevel = C_LO;
            pq += ps[who].q;
            if (started_pq) started_pq = false;
          }
        }
        else if (yn_first) {
          if (runlevel == C_HI) pin_off(ps);
          runlevel = C_WAIT; // Debug::shout(__LINE__, pin, runlevel, d);
          yn += ps[who].z;
          if (started_pq) started_pq = false;
          if (started_yn) started_yn = false;
        }
        else {
          // t exhausted
          pin_low();
          if (started_yn) started_yn = false;
          if (started_pq) started_pq = false;
          run_next_protocol(ps, t);
        }
        goto tail_recurse;
      }
    }
  }

  static void init(Channel *cs, DigitalPulses *pulsar){ 
    pulsar->clear();
    for (int i = 0; i < CHAN; i++) cs[i].init(i);
  }

  static void refresh(Channel *cs, Protocol *ps, DigitalPulses *pulsar) { 
    pulsar->clear();
    for (int i = 0; i < CHAN; i++) cs[i].refresh(ps);
  }

  static void solo(Channel *cs, int it, Protocol *ps, int &pi) {
    for (int i = 0; i < CHAN; i++) {
      if (i == it) {
        if (cs[i].who < PROT) ps[cs[i].who].solo(ps, pi);
        cs[i].refresh(ps);
      }
      else cs[i].init(i);
    }
  }

  static Dura advance(Channel *cs, Dura d, Protocol *ps, DigitalPulses *pulsar, int &living) {
    Dura x = (Dura){0, 0};
    int a = 0;
    for (int i = 0; i < DIG; i++) if (cs[i].alive()) {
      a += 1;
      Dura y = cs[i].advance(d, ps, pulsar, (byte)i);
      if (!y.is_valid()) return y;
      if (!y.is_empty()) {
        if (x.is_empty() || y < x) x = y;
      }
      else if (!cs[i].alive()) a -= 1;
    }
    living = a;
    return x;
  }
};

Channel channels[CHAN];
Channel not_a_channel = (Channel){ {0, 0}, {0, 0}, {0, 0}, C_ZZZ, 128, 255, 255 };


/****************************
 * Analog signal generation *
 ****************************/

// TODO


/*****************************************
**END C++ish SECTION, BEGIN Cish SECTION**
*****************************************/


/*************************
 * Error and I/O buffers *
 *************************/

#define MSGN 62
#define BUFN 128

byte msg[MSGN+1];
int erri = 0;

byte buf[BUFN];
int bufi = 0;

int drain_to_buf() {
  int av = Serial.available();
  if (av > 0) {
    while ((av--) > 0 && bufi < BUFN) {
      byte c = Serial.read();
      buf[bufi] = c;
      if (c == '\n') return bufi+1;
      else bufi++;
    }
  }
  return (bufi < BUFN) ? 0 : BUFN;
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

void write_voltage_5(int digital_value, char* buffer, bool digital) {
  int v = 0;
  if (digital) {
    if (digital_value > 0) v = 5000;
  }
  else {
    v = (3300 * digital_value + (1 << (ANALOG_BITS-1)))/(1 << ANALOG_BITS);
    if (v < 1) v = 1;
    if (v > 3300) v = 3300;    
  }
  buffer[0] = (char)('0' + (v/1000));
  buffer[1] = '.';
  buffer[2] = (char)('0' + ((v/100)%10));
  buffer[3] = (char)('0' + ((v/10)%10));
  buffer[4] = (char)('0' + (v % 10));
}



/*********************************************
 * Initialization -- run once only at start! *
 *********************************************/

#define DRIFT_OFFSET 128
int drift_rate;           // Correction for drift

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

bool eeprom_set_int(int value, int eepi) {
  int old = eeprom_get_int(eepi);
  if (old == value) return false;
  for (int i = 0; i < 4; i++) {
    byte b = (byte)(value & 0xFF);
    value >>= 8;
    EEPROM.write(eepi+i, b);
  }
  return true;
}

int eeprom_get_int(int eepi) {
  int value = 0;
  for (int i = 0; i < 4; i++) {
    value >>= 8;
    value = value | ((int)(EEPROM.read(eepi+i)) << 24);
  }
  return value;
}

void eeprom_read_who() {
  whoami[0] = '$';
  int i = 1;
  for (; i < WHON-1; i++) {
    byte ei = EEPROM.read(i-1);
    if (!ei) break;
    if (ei == '~' || ei == '$' || ei == '\n') whoami[i] = '?';
    else whoami[i] = ei;
  }
  whoami[i] = '\n';
  whoi = i+1;
  if (i+1 < WHON) whoami[i+1] = 0;
}

void tell_who() { Serial.write(whoami, whoi); Serial.send_now(); }

void init_eeprom() {
  bool first = eeprom_set((byte*)"Ticklish2.0 ", 0, WHON, false);
  if (first) {
    eeprom_set((byte*)"", 12, WHON, true);
    eeprom_set_int(0, DRIFT_OFFSET);
  }
  eeprom_read_who();
  drift_rate = eeprom_get_int(DRIFT_OFFSET);
}

void init_analog() {
  for (int i = 0; i < ANALOG_DIVS; i++) wave[i] = ANALOG_ZERO + (int16_t)round(ANALOG_AMPL*sin(i * 2 * (M_PI / ANALOG_DIVS)));
  analogReadResolution(ANALOG_BITS);
  analogWriteResolution(ANALOG_BITS);
  analogWrite(A14, ANALOG_ZERO);
}

void init_digital() {
  for (int i = 0; i<DIG; i++) { 
    int pi = digi[i];
    if (assume_in[i]) pinMode(pi, INPUT);
    else { pinMode(pi, OUTPUT); digitalWrite(pi, LOW); }
  }
  // Override whatever else happens to make LED work.
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}



/***********
 * Runtime *
 ***********/

volatile int tick;        // Last CPU clock count
volatile int tock;        // Clock counts since last correction
Dura global_clock;        // Time since start of running.
Dura next_event;          // Time of next event.  Just busywait until then.

#define MIN_BUSY_US 1000
#define MAX_BUSY_US 20000
Dura io_anyway;           // Time at which you need to read I/O even if it may interfere with events

#define RUN_LOCKED   -3
#define RUN_ERROR    -2
#define RUN_TO_ERROR -1
#define RUN_COMPLETED 0
#define RUN_PROGRAM   1
#define RUN_GO        2
volatile int runlevel;   // -2 error; -1 stopping to error; 0 ran; 1 accepting commands; 2 running
volatile int alive;      // Number of channels alive and running

volatile bool led_is_on;

struct DigitalPulses pulsar;

void error_with_message(const char* what, int n, const char* detail, int m) {
  if (erri == 0) {
    msg[0] = '$';
    erri = 1;
    for (int i = 0; i < n && erri < MSGN-1; i++) { char c = what[i]; msg[erri] = c; if (c == 0) break; erri += 1; }
    for (int i = 0; i < m && erri < MSGN-1; i++) { char c = detail[i]; msg[erri] = c; if (c == 0) break; erri += 1; }
    msg[erri] = '\n';
    erri += 1;
    if (erri < MSGN) msg[erri] = 0;
  }
  if (runlevel != RUN_ERROR) runlevel = RUN_TO_ERROR;
}

void error_with_message(const char* what, const char* detail, int m) { error_with_message(what, MSGN, detail, m); }

void error_with_message(const char* what, int n, const char* detail) { error_with_message(what, n, detail, MSGN); }

void error_with_message(const char* what, const char* detail) { error_with_message(what, MSGN, detail, MSGN); }

void error_with_message(const char* what, char letter) {
  char tiny[2];
  tiny[0] = letter;
  tiny[1] = 0;
  error_with_message(what, tiny, 1);
}

void error_with_message(const char* what) { error_with_message(what, MSGN, "", 0); }

void analog_cooldown() {
  if (runlevel == RUN_TO_ERROR) {
    if (channels[DIG].who == 255) {
      if (channels[DIG].zero != 255) analogWrite(A14, ANALOG_ZERO);
      runlevel = RUN_ERROR;
    }
    else {
      // TODO--nice cooldown!
      analogWrite(A14, ANALOG_ZERO);
      runlevel = RUN_ERROR;
    }
  }
}

void go_go_go() {
  if (runlevel == RUN_PROGRAM) {
    runlevel = RUN_LOCKED;
    alive = 0;
    if (led_is_on) {
      led_is_on = false;
      digitalWrite(LED_PIN, LOW);
    }
    int found = 0;
    for (int i = 0; i < DIG; i++) {
      Channel *c = channels + i;
      c->who = c->zero;
      if (c->who == 255) continue;
      alive += 1;
      Protocol *p = protocols + c->who;
      if (p->j == '|') pinMode(c->pin, INPUT);
      else {
        pinMode(c->pin, OUTPUT);
        c->pin_off(p);
      }
      c->t = p->t;
      c->yn = p->d;
      c->runlevel = C_WAIT; // Debug::shout(__LINE__, c->pin, c->runlevel);
      if (found) next_event = next_event.or_smaller(p->d);
      else next_event = p->d;
      found += 1;
    }
    global_clock = (Dura){0, 0};
    io_anyway = global_clock;
    io_anyway += MHZ * MAX_BUSY_US;
    tick = ARM_DWT_CYCCNT;
    tock = 0;
    runlevel = RUN_GO;
  }
  else if (runlevel != RUN_ERROR && runlevel != RUN_TO_ERROR) {
    error_with_message("Attempt to start running from invalid state.");
  }
}

bool run_iteration() {
  // Can't pass volatile as reference, so buffer it
  int living = alive;
  next_event = Channel::advance(channels, global_clock, protocols, &pulsar, living);
  if (!next_event.is_valid()) error_with_message("Attempt to read two or more digital pulse trains at once");
  alive = living;
  return alive != 0;
}



/*******************
 * Command Parsing *
 *******************/

Channel* process_get_channel(byte ch) {
  if (ch >= 'A' && (ch < ('A'+DIG) || ch == 'Z')) return channels + ((ch == 'Z') ? DIG : (ch - 'A'));
  else {
    error_with_message("Unknown channel ", (char)ch);
    return &not_a_channel; 
  }
}

Protocol* process_get_protocol(byte ch) {
  Channel *c = process_get_channel(ch);
  if (c->who != 255) return protocols + c->who;
  else {
    error_with_message("No protocol for channel ", (char)ch);
    return &not_a_protocol;
  }
}

Protocol* process_ensure_protocol(byte ch) {
  Channel *c = process_get_channel(ch);
  if (c->who != 255) return protocols + c->who;
  else if (c->zero != 255) {
    c->refresh(protocols);
    return protocols + c->who;
  }
  else if (proti < PROT) {
    c->zero = proti;
    c->who = c->zero;
    if (c->pin < 255) {
      if (c->pin != LED_PIN) pinMode(c->pin, OUTPUT);
      digitalWrite(c->pin, LOW);
    }
    protocols[proti].init();
    proti++;
    return protocols + c->who;
  }
  else return process_get_protocol(ch);   // Defer to get for error handling
}

Protocol* process_new_protocol(byte ch) {
  Channel *c = process_get_channel(ch);
  if (proti < PROT) {
    byte old = c->who;
    while (c->who != 255) { old = c->who; c->who = protocols[c->who].next; }
    protocols[old].next = (byte)proti;
    c->who = (byte)proti;
    protocols[proti].init();
    proti++;
    return protocols + c->who;
  }
  else {
    c->who = 255;
    return process_get_protocol(ch);    // Defer to get for error handling
  }
}

void process_reset() {
  Protocol::init(protocols, proti);
  Channel::init(channels, &pulsar);
  erri = 0;
  alive = 0;
  runlevel = RUN_PROGRAM;
}

void process_refresh() {
  if (runlevel == RUN_COMPLETED) {
    Channel::refresh(channels, protocols, &pulsar);
    runlevel = RUN_PROGRAM;  
  }
}

void process_start_running() {
  go_go_go();
}

void process_start_running(byte who) {
  if (who >= 'A' && who <= 'Z' && who != 'Y') {
    int i = who - 'A';
    if (who == 'Z') i -= 1;
    Channel::solo(channels, i, protocols, proti);
    process_start_running();
  }
  else {
    char tiny[3]; tiny[0] = who; tiny[1] = '\n'; tiny[2] = 0;
    error_with_message("Trying to run on unknown channel", tiny);
  }
}

void process_stop_running(byte who) {
  Channel *c = process_get_channel(who);
  if (runlevel == RUN_GO) {
    if (c->who != 255) {
      c->pin_low();
      c->runlevel = C_ZZZ; // Debug::shout(__LINE__, c->pin, c->runlevel);
      c->who = 255;
      if (alive > 0) alive -= 1;
      if (alive == 0) runlevel = RUN_COMPLETED;
    }
  }
}

void process_stop_running() {
  for (int i = 0; i < DIG; i++) {
    Channel *c = channels + i;
    if (c->who != 255) {
      c->pin_low();
      c->runlevel = C_ZZZ; // Debug::shout(__LINE__, c->pin, c->runlevel);
      c->who = 255;
    }
  }
  alive = 0;
  runlevel = RUN_COMPLETED;
}

void process_say_the_time() {
  msg[0] = '$';
  ((runlevel == RUN_GO) ? global_clock : (Dura){0, 0}).write_15(msg+1);
  msg[16] = '\n';
  msg[17] = 0;
  tell_msg();
}

void process_say_the_drift(int old_drift, int new_drift, bool changed, bool query) {
  msg[0] = '~';
  msg[1] = '^';
  int drift = old_drift;
  if (drift < 0) { drift = -drift; msg[2] = '-'; } else msg[2] = '+';
  if (drift >= 100000000) drift = 0;
  for (int i = 0; i < 8; i++) {
    msg[10-i] = (drift % 10) + '0';
    drift /= 10;
  }
  msg[11] = (changed) ? '!' : ((query && old_drift != new_drift) ? '?' : '.');
  msg[12] = 0;
  tell_msg();
}

void process_say_empty() {
  Serial.write("$\n");
  Serial.send_now();
}

int median_of_three(int a, int b, int c) {
  if (a < b) {
    if (b < c) return b;
    if (a < c) return c;
    return a;
  }
  if (c < b) return b;
  if (c < a) return c;
  return a;
}

int process_get_analog(int channel) {
  int mmm[3];
  int mm[3];
  int m[3];
  for (int i=0; i < 3; i++) {
    for (int j=0; j < 3; j++) {
      for (int k=0; k < 3; k++) m[k] = analogRead(channel);
      mm[j] = median_of_three(m[0], m[1], m[2]);
    }
    mmm[i] = median_of_three(mm[0], mm[1], mm[2]);
  }
  return median_of_three(mmm[0], mmm[1], mmm[2]);
}

void process_say_the_voltage(char ch) {
  if (ch == 'X' || ch == 'Y' || ch == 'Z') error_with_message("Cannot ever read input on this channel: ", ch);
  else {
    Channel *c = process_get_channel(ch);
    if (c->zero != 255) error_with_message("Channel voltage request not valid because running on: ", ch);
    else {
      pinMode(digi[ch - 'A'], INPUT);
      msg[0] = '~';
      write_voltage_5((ch <= 'J') ? process_get_analog(ch - 'A') : digitalRead(ch - 'K'), (char*)(msg+1), ch > 'J');
      msg[6] = 0;
      tell_msg();
    }          
  }
}

bool process_set_the_drift(int new_drift, int save) {
  drift_rate = (new_drift == 1) ? 2 : new_drift;
  if (save) return eeprom_set_int(drift_rate, DRIFT_OFFSET);
  else return false;
}

bool process_drift_command() {
  if (bufi < 12) return false;
  int sign = 0;
  if (buf[2] == '+') sign = 1;
  else if (buf[2] == '-') sign = -1;
  if (sign == 0) {
    error_with_message("Bad drift correction, missing +-: ", (char*)buf, 12);
  }
  else {
    int number = 0;
    for (int i = 3; i<11; i++) {
      int v = buf[i] - '0';
      if (v < 0 || v > 9) {
        error_with_message("Invalid drift correction number: ", (char*)buf, 12);
        number = 0x80000000;
        break;
      }
      else number = 10*number + (buf[i] - '0');
    }
    if (number > -1000000000 && number < 1000000000) {
      if (buf[12] == '^') {
        drift_rate = eeprom_get_int(DRIFT_OFFSET);
        buf[12] = '?';
      }
      int old_drift = drift_rate;
      bool changed = false;
      if (buf[11] != '?') {
        changed = process_set_the_drift(sign*number, buf[12] == '!');
      }
      process_say_the_drift(old_drift, sign*number, changed, buf[12] == '?');
    }
  }
  return true;
}

void process_channel_status_command(byte ch) {
  msg[0] = '$';
  process_get_channel(ch)->write(msg+1);
  msg[62] = 0;
  Serial.write((char*)msg);
  Serial.send_now();
}

void process_channel_read_command(byte ch) {
  msg[0] = '$';
  snprintf((char*)msg+1, 12, "%010llX", dpi_buffer[ch - 'A']);
  msg[11] = '\n';
  msg[12] = 0;
  Serial.write(msg, 12);
  Serial.send_now();
}

void process_channel_time_command(byte ch) {
  msg[0] = '$';
  dpi_timing[ch - 'A'].write_15(msg+1);
  msg[16] = '\n';
  msg[17] = 0;
  Serial.write(msg, 17);
  Serial.send_now();
}

void process_error_command() {
  if (bufi < 2 || buf[0] != '~') return;
  // Was a fixed-length command.  Pick out the meaningful ones.
  switch(buf[1]) {
    case '@': Serial.write("~!\n", 3); Serial.send_now(); break;
    case '.': process_reset(); break;
    case '\'': process_say_empty(); break;
    case '#': tell_msg(); break;
    case '?': tell_who(); break;
    default: break;
  }
}

void process_complete_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') {
    if (buf[0] == '$') error_with_message("Command not valid (run complete): ", (char*)buf, 1);
    else               error_with_message("unknown command starting with: ", (char*)buf, 1);
    return;
  }
  switch(buf[1]) {
    case '@': Serial.write("~/\n", 3); Serial.send_now(); break;
    case '.': process_reset(); break;
    case '"': process_refresh(); break;
    case '#': process_say_the_time(); break;
    case '?': tell_who(); break;
    case '/': break;
    case '\'': process_say_empty(); break;
    case '^': if (!process_drift_command()) return; break;
    default:
      if (buf[1] >= 'A' && buf[1] <= 'Z' && bufi > 2) {
        if (buf[2] == '/') return;
        else if (buf[2] == '#') { process_channel_status_command(buf[1]); return; }
        else if (buf[2] == '?') { process_say_the_voltage(buf[1]); return; }
        else if (buf[2] == '|') {
          if      (bufi == 4 && buf[3] == '?') process_channel_read_command(buf[1]);
          else if (bufi == 4 && buf[3] == '#') process_channel_time_command(buf[1]);
          else {
            error_with_message("Digital pulse command while complete (not a read): ", (char*)buf, 4);
          }
          return;
        }
      }
      error_with_message("Command not valid (run complete): ", (char*)buf, 2);
  }
}

void process_init_command() {
  if (bufi < 2) return;
  if (buf[0] == '$') {
    if (memcmp("IDENTITY", buf+1, 8) != 0) {
      error_with_message("Expected $IDENTITY, found ", (char*)buf, bufi);
      return;
    }
    eeprom_set(buf + 9, 12, bufi - 9, true);
    eeprom_read_who();
    return;
  }
  byte b = buf[1];
  if (b >= 'A' && b <= 'Z' && b != 'Y') {
    if (bufi < 3) return;
    byte ch = b;
    b = buf[2];
    switch(b) {
      case '@': Serial.write("~.\n", 3); Serial.send_now(); break;
      case '#': process_channel_status_command(ch); break;
      case '?': process_say_the_voltage(ch); break;
      case '*': process_start_running(ch); break;
      case '/': break;
      case '&': process_new_protocol(ch); break;
      case '|':
        if (ch < 'K' || ch > 'W') error_with_message("Digital pulse read on channel outside of K-W: ", (char*)buf+1, 1);
        else if (bufi < 4) error_with_message("| without > or < or ?");
        else if (buf[3] == '<') process_ensure_protocol(ch)->j = '|';
        else if (buf[3] == '>') {
          Protocol *p = process_ensure_protocol(ch);
          if (p->j == '|') p->j = ' ';
        }
        else if (buf[3] == '?') process_channel_read_command(b);
        else if (buf[3] == '#') process_channel_time_command(b);
        else error_with_message("Unknown digital pulse command: ", (char*)buf, 4);
      case '=':
      case ':':
        if (bufi < 57) return;
        if (ch == 'Z') {
          error_with_message("Cannot set all on channel Z: ", (char*)buf, 12);
        }
        else {
          int err = process_ensure_protocol(ch)->parse_all(buf+3);
          if (err) {
            char bad[6]; bad[0] = 'N'; bad[1] = 'o'; bad[2] = '0'+err; bad[3] = ' '; bad[4]='='; bad[5]=0;
            error_with_message(bad, (char*)(buf+3), 57);
          }         
        }
        if (b == ':') process_start_running(ch);
        return;
        break;
      case 'u': process_ensure_protocol(ch)->i = 'u'; break;
      case 'i': process_ensure_protocol(ch)->i = 'i'; break;
      case 'l': if (ch != 'Z') error_with_message("Analog required: ", (char*)buf, 3); else process_ensure_protocol(ch)->j = 'l'; break;
      case 'r': if (ch != 'Z') error_with_message("Analog required: ", (char*)buf, 3); else process_ensure_protocol(ch)->j = 'r'; break;
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
          if (!process_ensure_protocol(ch)->parse_labeled(b, buf+3)) {
            error_with_message("Bad duration format: ", (char*)buf, 11);
          }
        }
        return;
      case 'a': /* TODO */ return;
      default:
        error_with_message("Channel command not valid (setting): ", (char*)buf, 3);
    }
  }
  else {
    switch(b) {
      case '@': Serial.write("~.\n", 3); Serial.send_now(); break;
      case '.': process_reset(); break;
      case '#': process_say_the_time(); break;
      case '?': tell_who(); break;
      case '\'': process_say_empty(); break;
      case '/': break;
      case '*': process_start_running(); break;
      case '^': if (!process_drift_command()) return; break;
      default:
        error_with_message("Command not valid (setting): ", (char*)buf, 2);
    }
  }
}

void process_runtime_command() {
  if (bufi < 2) return;
  if (buf[0] != '~') {
    error_with_message("Channel command not valid (running): ", (char*)buf, 2);
    return;
  }
  byte b = buf[1];
  if (b >= 'A' && b <= 'Z') {
    if (bufi < 3) return;
    byte ch = b;
    b = buf[2];
    switch(b) {
      case '@': /* TODO */ break;
      case '#': process_channel_status_command(ch); break;
      case '/': process_stop_running(ch); break;
      case '?': process_say_the_voltage(ch); break;
      case '|':
        if      (bufi == 4 && buf[3] == '?') process_channel_read_command(b);
        else if (bufi == 4 && buf[3] == '#') process_channel_time_command(b);
        else {
          error_with_message("Digital pulse command while running that isn't read: ", (char*)buf, 4);
        }
        break;
      default:
        error_with_message("Channel command not valid (running): ", (char*)buf, 3);
    }
  }
  else {
    switch(b) {
      case '@': Serial.write("~*\n", 3); Serial.send_now(); break;
      case '.': process_reset(); break;
      case '#': process_say_the_time(); break;
      case '?': tell_who(); break;
      case '/': process_stop_running(); break;
      case '\'': process_say_empty(); break;
      case '^': if (!process_drift_command()) return; break;
      default:
        error_with_message("Command not valid (running): ", (char*)buf, 2);
    }   
  }
}



/*************
 * Main Loop *
 *************/

// Setup runs once at reset / power on
void setup() {
  runlevel = RUN_LOCKED;
  alive = 0;
  init_eeprom();
  init_digital();
  init_analog();
  pulsar.clear();
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
  tock = 0;
  runlevel = RUN_PROGRAM;
}

int time_passes() {
  int now = ARM_DWT_CYCCNT;
  int delta = now - tick;
  tick = now;
  int x, y;
  if (drift_rate != 0) {
    x = (drift_rate > 0) ? drift_rate : -drift_rate;
    tock += delta;
    if (tock > x) {
      if (tock > (x << 4)) {
        y = tock/x;
        tock = tock - y*x;
      }
      else {
        y = 1;
        tock -= x;
        while (tock > x) {
          y++;
          tock -= x;
        }
      }
      delta += (drift_rate > 0) ? y : -y;
    }
  }
  global_clock += delta;
  return delta;
}

// Runs over and over forever (after setup() finishes)
void loop() {
  int delta = time_passes();
  bool urgent = false;
  if (next_event < global_clock) {
    urgent = true;
    if (runlevel == RUN_GO) {
      bool alive = run_iteration();
      if (!alive) process_stop_running();
    }
    else {
      if (led_is_on) digitalWrite(LED_PIN, LOW);
      else digitalWrite(LED_PIN, HIGH);
      led_is_on = !led_is_on;
      delta = 0;
      switch(runlevel) {
        case RUN_PROGRAM:   delta = (led_is_on) ?  2000 :  998000; break;
        case RUN_COMPLETED: delta = (led_is_on) ?  2000 : 2998000; break;
        case RUN_TO_ERROR:  delta = (led_is_on) ? 10000 :   90000; break;
        case RUN_ERROR:     delta = (led_is_on) ? 10000 :  190000; break;
        default:            delta = (led_is_on) ? 10000 :   40000; break;
      }
      next_event += MHZ * delta;
    }
  }
  else if (runlevel == RUN_GO && pulsar.active) pulsar.run(global_clock);

  if (runlevel == RUN_TO_ERROR) analog_cooldown();
  if (!urgent || io_anyway < global_clock) {
    Dura soon = global_clock;
    soon += MHZ * MIN_BUSY_US;
    if (next_event < soon || io_anyway < next_event) {
      // Do not need to busywait for next event
      int n = drain_to_buf();
      if (n == BUFN) {
        error_with_message("Too much input without newline: ...", ((char*)buf + (BUFN-10)), 10);
        bufi = 0;
      }
      else if (n > 0) {
        switch(runlevel) {
          case RUN_ERROR:     process_error_command(); break;
          case RUN_COMPLETED: process_complete_command(); break;
          case RUN_PROGRAM:   process_init_command(); break;
          case RUN_GO:        process_runtime_command(); break;
          default: break;
        }
        bufi = 0;
      }
      delta = time_passes();
      io_anyway = global_clock;
      io_anyway += MHZ * MAX_BUSY_US;
    }
  }
}
