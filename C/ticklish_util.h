/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#ifndef KERRR_TICKLISH_UTIL
#define KERRR_TICKLISH_UTIL

#include <stdbool.h>
#include <sys/time.h>

#define TKH_RUNNING 2
#define TKH_ALLDONE 1
#define TKH_PROGRAM 0
#define TKH_ERRORED -1

int tkh_char_to_state(char c);


/** We recklessly reuse timeval to store durations (not time-since-epoch) */
char* tkh_encode_time(const struct timeval *tv);

/** We recklessly reuse timeval to store durations (not time-since-epoch) */
struct timeval tkh_decode_time(const char *s);


float tkh_decode_voltage(const char *s);

int tkh_decode_state(const char *s);


char* tkh_encode_name(const char *s);
char* tkh_decode_name(const char *s);


bool tkh_string_is_ticklish(const char *s);
bool tkh_string_is_time_report(const char *s);

#endif
