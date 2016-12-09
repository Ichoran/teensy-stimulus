/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#ifndef KERRR_TICKLISH
#define KERRR_TICKLISH

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>

#include <libserialport.h>

#include "ticklish_util.h"

#define TKH_MAX_TIME_MICROS 99999999000000ll

typedef struct TkhTimed {
    struct timeval zero;       // Earliest possible value of start time, assuming time runs the same speed on this machine and board
    struct timeval window;     // How much later our zero could be
    struct timeval timestamp;  // Our timestamp when we tickled
    struct timeval board_at;   // Board's idea of the elapsed time
} TkhTimed;

void tkh_timed_init(TkhTimed *tkt);

bool tkh_timed_is_valid(TkhTimed *tkt);

char* tkh_timed_to_string(TkhTimed *tkt);



typedef struct TkhDigital {
    char channel;   // Unlike Scala interface, we store the channel in here!
    long long duration;
    long long delay;
    long long block_high;
    long long block_low;
    long long pulse_high;
    long long pulse_low;
    bool upright;
} TkhDigital;

bool tkh_digital_is_valid(TkhDigital *tkh);

TkhDigital tkh_zero_digital(char channel);

TkhDigital tkh_simple_digital(char channel, double delay, double interval, double high, unsigned int count);

TkhDigital tkh_pulsed_digital(char channel, double delay, double interval, unsigned int count, double pulse_interval, double pulse_high, unsigned int pulse_count);

char* tkh_digital_to_string(TkhDigital *tdg, bool command);



#define TICKLISH_PATIENCE 500
#define TICKLISH_BUFFER_N 256
#define TICKLISH_MAX_OUT 64

typedef struct Ticklish {
    // This stuff should be immutable (set once at creation)
    char* portname;
    struct sp_port *my_port;

    pthread_mutex_t my_mutex;  // Want to set = PTHREAD_MUTEX_INITIALIZER;

    // This stuff is mutable and the above mutex should be locked before any of this is altered
    volatile char* my_id;

    volatile char* buffer;
    volatile int buffer_start;
    volatile int buffer_end;

    volatile int error_value;
} Ticklish;

Ticklish* tkh_construct(struct sp_port* port);

void tkh_destruct(Ticklish *tkh);

bool tkh_is_connected(Ticklish *tkh);

void tkh_connect(Ticklish *tkh);

void tkh_disconnect(Ticklish *tkh);

int tkh_wait_for_next_buffer(Ticklish *tkh);

char* tkh_fixed_read(Ticklish *tkh, int n, bool twiddled);

char* tkh_flex_read(Ticklish *tkh, bool dollared);

void tkh_write(Ticklish *tkh, const char *s);

char* tkh_query(Ticklish *tkh, const char* ask, int n);

char* tkh_flex_query(Ticklish *tkh, const char* ask);

bool tkh_is_ticklish(Ticklish *tkh);

char* tkh_id(Ticklish *tkh);

enum TkhState tkh_state(Ticklish *tkh);

bool tkh_ping(Ticklish *tkh);

void tkh_clear(Ticklish *tkh);

bool tkh_is_error(Ticklish *tkh);
bool tkh_is_prog(Ticklish *tkh);
bool tkh_is_run(Ticklish *tkh);
bool tkh_is_done(Ticklish *tkh);

TkhTimed tkh_timesync(Ticklish *tkh);

void tkh_set(Ticklish *tkh, TkhDigital *protocols, int n);

TkhTimed tkh_run(Ticklish *tkh);



/** Pass a reference to a pointer for an array of descriptions.
  * Function returns the number of things actually passed back.
  * Free each one, then free the array (with `free`).
  */
int tkh_get_all_port_descriptions(char ***descsp);

/** Gets a Ticklish if available.  Destroy with tkh_destruct.
  * Port is OPEN when the routine returns!
  */
Ticklish* tkh_find_first_ticklish();

/** Pass a reference to a pointer for an array of Ticklish pointers.
  * Function returns the number of things actually passed back.
  * Call tkh_destruct on each item in the array, then `free` the array.
  * Every port is OPEN when the routine returns!
  */
int tkh_find_all_ticklish(Ticklish ***tkhsp);


#ifdef __cplusplus
}
#endif

#endif
