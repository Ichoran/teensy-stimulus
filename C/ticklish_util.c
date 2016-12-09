/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "ticklish_util.h"


 void tkh_timeval_normalize(struct timeval *tv) {
    if (tv->tv_usec < 0) {
        tv->tv_sec -= 1;
        tv->tv_usec += 1000000;
    }
    if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
        int div = (tv->tv_usec - 999999) / 1000000;
        tv->tv_sec += div;
        tv->tv_usec -= 1000000*div;
    }
}

void tkh_timeval_minus_eq(struct timeval *tv, const struct timeval *subtract_me) {
    tv->tv_sec -= subtract_me->tv_sec;
    tv->tv_usec -= subtract_me->tv_usec;
    tkh_timeval_normalize(tv);
}

void tkh_timeval_plus_eq(struct timeval *tv, const struct timeval *add_me) {
    tv->tv_sec += add_me->tv_sec;
    tv->tv_usec += add_me->tv_usec;
    if (tv->tv_usec >= 1000000) {
        tv->tv_sec += 1;
        tv->tv_usec -= 1000000;
    }
    tkh_timeval_normalize(tv);
}

int tkh_timeval_compare(const struct timeval *tva, const struct timeval *tvb) {
    struct timeval tv = *tva;
    tkh_timeval_minus_eq(&tv, tvb);
    if (tv.tv_sec < 0) return -1;
    else if (tv.tv_sec > 0 || tv.tv_usec > 0) return 1;
    else return 0;
}


enum TkhState tkh_char_to_state(char c) {
    switch(c) {
        case '*': return TKH_RUNNING; break;
        case '/': return TKH_ALLDONE; break;
        case '.': return TKH_PROGRAM; break;
        case '!': return TKH_ERRORED; break;
        default:  return TKH_UNKNOWN;
    }
}


int tkh_encode_time_into(const struct timeval *tv, char* target, int max_length) {
    if (max_length < 8) return -1;
    if (tv->tv_sec >= 99999999) { memset(target, '9', 8); }
    else if (tv->tv_usec < 0) { memset(target, '!', 8); }
    else {
        char buffer[32];
        if (tv->tv_sec == 0) { 
            snprintf(buffer, 32, "0.%06ld", tv->tv_usec);
            memcpy(target, buffer, 8);
        }
        else {
            char buffer[32];
            snprintf(buffer, 32, "%ld.%06ld", tv->tv_sec, tv->tv_usec);
            if (buffer[7] == '.') {
                memcpy(target+1, buffer, 7);
                *target = '0';
            }
            else memcpy(target, buffer, 8);
        }
    }
    if (max_length > 8) target[8] = 0;
    return 8;
}

char* tkh_encode_time(const struct timeval *tv) {
    char buffer[10];
    tkh_encode_time_into(tv, buffer, 8);
    buffer[8] = 0;  // Just in case any string impl fails to leave a terminating zero
    strdup(buffer);
}


struct timeval tkh_decode_time(const char *s) {
    struct timeval tv = {0, -1};   // Error by default
    if (!tkh_string_is_time_report(s)) return tv;
    errno = 0;
    double t = strtod(s, NULL);
    tv.tv_sec = (int)floor(t);
    tv.tv_usec = (int)rint((t - floor(t))*10000000);
    return tv;
}

struct timeval tkh_timeval_from_micros(long long micros) {
    struct timeval tv = {0, -1};
    if (micros < 0 || micros > 2000000000000000ll) return tv;
    tv.tv_sec = micros / 1000000;
    tv.tv_usec = micros - tv.tv_sec*1000000;
    return tv;
}


long long tkh_micros_from_timeval(const struct timeval *tv) {
    if (tv->tv_usec < 0) return -1;
    else return ((long long)tv->tv_sec)*1000000 + tv->tv_usec;
}



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

enum TkhState tkh_decode_state(const char *s) {
    if (strnlen(s,2) != 1) return TKH_UNKNOWN;
    return tkh_char_to_state(*s);
}

char* tkh_encode_name(const char *s) {
    size_t n = strnlen(s, 53);
    char* name = malloc(11+n);
    snprintf(name, 11+n, "$IDENTITY%s\n", s);
    name[n+10] = 0;  // In case snprintf impl doesn't leave a null on the end
    return name;
}

char* tkh_decode_name(const char *s) {
    if (!tkh_string_is_ticklish(s)) return NULL;
    else return strndup(s+12, 51);
}


bool tkh_string_is_ticklish(const char *s) {
    return strncmp(s, "Ticklish1.0 ", 12) == 0;
}

bool tkh_string_is_time_report(const char *s) {
    if (!s[8] == '.') return 0;
    for (int i = 0; i<15; i++) {
        if (i != 8) {
            if (!isdigit(s[i])) return 0;
        }
    }
    return (s[15] == 0); 
}
