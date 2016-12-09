/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#ifndef KERRR_TICKLISH_UTIL
#define KERRR_TICKLISH_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <sys/time.h>


enum TkhState { TKH_UNKNOWN, TKH_ERRORED, TKH_ALLDONE, TKH_PROGRAM, TKH_RUNNING };

enum TkhState tkh_char_to_state(char c);

static inline bool tkh_timeval_is_valid(const struct timeval *tv) { return tv->tv_usec >= 0; }
void tkh_timeval_normalize(struct timeval *tv);
void tkh_timeval_minus_eq(struct timeval *tv, const struct timeval *subtract_me);
void tkh_timeval_plus_eq(struct timeval *tv, const struct timeval *add_me);
int tkh_timeval_compare(const struct timeval *tva, const struct timeval *tvb);

/** We recklessly reuse timeval to store durations (not time-since-epoch) */
char* tkh_encode_time(const struct timeval *tv);
int tkh_encode_time_into(const struct timeval *tv, char* target, int max_length);

/** We recklessly reuse timeval to store durations (not time-since-epoch) */
struct timeval tkh_decode_time(const char *s);

struct timeval tkh_timeval_from_micros(long long micros);
long long tkh_micros_from_timeval(const struct timeval *tv);


float tkh_decode_voltage(const char *s);

enum TkhState tkh_decode_state(const char *s);


char* tkh_encode_name(const char *s);
char* tkh_decode_name(const char *s);


bool tkh_string_is_ticklish(const char *s);
bool tkh_string_is_time_report(const char *s);


#ifdef __cplusplus
}
#endif

#endif
