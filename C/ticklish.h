/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#ifndef KERRR_TICKLISH
#define KERRR_TICKLISH

#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>

#include <libserialport.h>

#define TKH_MAX_TIME_MICROS 99999999000000ll

typedef struct TkhTimed {
    struct timeval zero;       // Earliest possible value of start time, assuming time runs the same speed on this machine and board
    struct timeval window;     // How much later our zero could be
    struct timeval timestamp;  // Our timestamp when we tickled
    struct timeval board_at;   // Board's idea of the elapsed time
} TkhTimed;

bool tkh_timed_is_valid(TkhTimed *tkh);

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

TkhDigital tkh_simple_digital(char channel, double delay, double interval, double high, unsigned int count);

TkhDigital tkh_pulsed_digital(char channel, double delay, double interval, unsigned int count, double pulse_interval, double pulse_high, unsigned int pulse_count);

#define TICKLISH_PATIENCE 500

typedef struct Ticklish {
    char* portname;

    struct sp_port *my_port;
    char* my_id;

    pthread_mutex_t my_mutex;  // Want to set = PTHREAD_MUTEX_INITIALIZER;
    char* buffer;
    int buffer_start;
    int buffer_end;

    int patience;

    int error_value;
} Ticklish;

bool tkh_is_connected(Ticklish *tkh);

void tkh_connect(Ticklish *tkh);

void tkh_disconnect(Ticklish *tkh);

int tkh_wait_for_next_buffer(Ticklish *tkh);

char* tkh_fixed_read(Ticklish *tkh, int n, bool twiddled);

char* tkh_flex_read(Ticklish *tkh, bool dollared);

void tkh_write(Ticklish *tkh, const char *s);

void tkh_query(Ticklish *tkh, char* ask, int n);

void tkh_flex_query(Ticklish *tkh, char* ask);

bool tkh_is_ticklish(Ticklish *tkh);

char* tkh_id(Ticklish *tkh);

int tkh_state(Ticklish *tkh);

bool tkh_ping(Ticklish *tkh);

void tkh_clear(Ticklish *tkh);

bool tkh_is_error(Ticklish *tkh);
bool tkh_is_prog(Ticklish *tkh);
bool tkh_is_run(Ticklish *tkh);
bool tkh_is_done(Ticklish *tkh);

TkhTimed tkh_timesync(Ticklish *tkh);

void tkh_set(Ticklish *tkh, TkhDigital *protocols, int n);

TkhTimed tkh_run();

#endif
