/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "ticklish_util.h"

int tkh_char_to_state(char c) {
    switch(c) {
        case '*': return TKH_RUNNING;
        case '/': return TKH_ALLDONE;
        case '.': return TKH_PROGRAM;
        default:  return TKH_ERRORED;
    }
}


char* tkh_encode_time(const struct timeval *tv) {
    char buffer[64];
    buffer[63] = 0;  // Just in case any string impl fails to leave a terminating zero
    if (tv->tv_sec >= 99999999) return strdup("99999999");
    else if (tv->tv_sec == 0) {
        snprintf(buffer, 63, "%.6f", tv->tv_usec*1e-6);
        return strdup(buffer);
    }
    else {
        snprintf(buffer, 63, "0%d.%06d", tv->tv_sec, tv->tv_usec);
        if (buffer[8] == '.') return strndup(buffer, 8);
        else return strndup(buffer + 1, 8);
    }
}

struct timeval tkh_decode_time(const char *s);



float tkh_decode_voltage(const char *s) {
    int ndp = 0;
    const char *c = s;
    int n = 0;
    for (; n < 5 && *c; c = c+1) {
        if (isdigit(*c)) n++;
        else if (*c == '.') {
            if (ndp < 1) ndp++;
            else return NAN;
        }
        else return NAN;
    }
    if (n > 4 || ndp != 1) return NAN;
    errno = 0;
    float v = strtof(s, NULL);
    if (errno) { errno = 0; return NAN; }
    else return v;
}

int tkh_decode_state(const char *s) {
    if (strnlen(s,2) != 1) return TKH_ERRORED;
    return tkh_char_to_state(*s);
}

char* tkh_encode_name(const char *s);
char* tkh_decode_name(const char *s);

int tkh_is_ticklish(const char *s);
int tkh_is_time_report(const char *s);
