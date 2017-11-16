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

double tkh_timeval_to_double(const struct timeval *tv) {
    return tv->tv_sec + 1e-6*(tv->tv_usec);
}

struct timeval tkh_timeval_from_double(double t) {
    struct timeval tv;
    tv.tv_sec = (int)floor(t);
    tv.tv_usec = lrint((t - tv.tv_sec)*1e6);
    tkh_timeval_normalize(&tv);
    return tv;
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


int tkh_encode_drift_into(double drift, char* target, int max_length) {
    if (max_length < 9) return -1;
    if (drift < 0) *target = '-';
    else *target = '+';
    drift = fabs(drift);
    int value = (drift >= 1.00000001e-8 && drift < 1.3) ? lrint(1.0/drift) : 0;
    snprintf(target+1, 9, "%08d", value);
    target[9] = 0;
    return 9;
}

double tkh_decode_drift(const char *s) {
    int sign;
    if (*s == '+') sign = 1;
    else if (*s == '-') sign = -1;
    else return NAN;
    int number = 0;
    int i;
    for (i = 1; i < 9; i++) {
        char c = s[i];
        if (c < '0' || c > '9') return NAN;
        number = number*10 + (c - '0');
    }
    double x = sign * number;
    if (x == 0) return 0;
    else return 1.0/x;
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

int tkh_encode_name_into(const char *s, char *target, int max_length) {
    size_t n = strnlen(s, 53) + 11;
    if (n > max_length) return -1;
    snprintf(target, n, "$IDENTITY%s\n", s);
    if (target[n-2] != '\n') target[n-2] = '\n';
    return n;
}

int tkh_decode_name_into(const char *s, char *id, int max_length, char *version) {
    if (!tkh_string_is_ticklish(s)) return -1;
    size_t n = strnlen(s, 64) - 12;
    if (version != NULL) {
        version[0] = s[8];
        version[1] = s[9];
        version[2] = s[10];
        version[3] = 0;
    }
    if (n+1 > max_length || n < 0) return -1;
    memcpy(id, s+12, n);
    id[n] = 0;
    return n+1;
}


bool tkh_string_is_ticklish(const char *s) {
    return strncmp(s, "Ticklish1.", 10) == 0;
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
