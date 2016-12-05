/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "ticklish_util.h"
#include "ticklish.h"

bool tkh_timed_is_valid(TkhTimed *tkh) {
    return
        tkh_timeval_is_valid(&(tkh->zero)) && 
        tkh_timeval_is_valid(&(tkh->window)) &&
        tkh_timeval_is_valid(&(tkh->timestamp)) &&
        tkh_timeval_is_valid(&(tkh->board_at));
}

char* tkh_timed_to_string(TkhTimed *tkh) {
    char buffer[144];
    snprintf(
        buffer, 144,
        "%ld.%06ld + <= %ld.%06ld; here %ld.%06ld, there %ld.%06ld",
        tkh->zero.tv_sec, tkh->zero.tv_usec,
        tkh->window.tv_sec, tkh->window.tv_usec,
        tkh->timestamp.tv_sec, tkh->timestamp.tv_usec,
        tkh->board_at.tv_sec, tkh->board_at.tv_usec
    );
    buffer[143] = 0;
    return strdup(buffer);
}


bool tkh_digital_is_valid(TkhDigital *tkh) {
    return 
        tkh->channel >= 'A' && tkh->channel <= 'X' &&
        tkh->duration > 0 && tkh->delay > 0 && tkh->block_high > 0 && tkh->block_low >= 0 && tkh->pulse_high >= 0 && tkh->pulse_low >= 0 &&
        tkh->duration <= TKH_MAX_TIME_MICROS && tkh->delay <= TKH_MAX_TIME_MICROS &&
        tkh->block_high <= TKH_MAX_TIME_MICROS && tkh->block_low <= TKH_MAX_TIME_MICROS &&
        tkh->pulse_high <= TKH_MAX_TIME_MICROS && tkh->pulse_low <= TKH_MAX_TIME_MICROS;
}

TkhDigital tkh_simple_digital(char channel, double delay, double interval, double high, unsigned int count) {
    TkhDigital result;
    result.duration = -1;  // Invalid default
    result.channel = channel;
    long long delus = (long long)rint(1e6 * delay);
    long long intus = (long long)rint(1e6 * interval);
    long long hius  = (long long)rint(1e6 * high);
    if (count == 0 && delus == 0) return result;
    if (intus <= hius || hius <= 0) return result;
    long long totus = delus + ((count > 0) ? (hius + (count-1)*intus) : 0);
    result.duration = totus;
    result.delay = delus;
    result.block_high = result.pulse_high = hius;
    result.block_low = result.pulse_low = intus - hius;
    result.upright = true;
    if (!tkh_digital_is_valid(&result)) result.duration = -1;
    return result;
}

TkhDigital tkh_pulsed_digital(char channel, double delay, double interval, unsigned int count, double pulse_interval, double pulse_high, unsigned int pulse_count) {
    TkhDigital result;
    result.duration = -1;
    result.channel = channel;
    long long delus = (long long)rint(1e6 * delay);
    long long intus = (long long)rint(1e6 * interval);
    long long pintus  = (long long)rint(1e6 * pulse_interval);
    long long phius = (long long)rint(1e6 * pulse_high);
    long long hius = pulse_high + (pulse_count-1)*pulse_interval;
    long long totus = delus + ((count > 0) ? (hius + (count-1)*intus) : 0);
    if (count == 0 && delus == 0) return result;
    if (intus <= hius || pintus <= phius || hius <= 0 || phius <= 0) return result;
    result.duration = totus;
    result.delay = delus;
    result.block_high = hius;
    result.block_low = intus - hius;
    result.pulse_high = phius;
    result.pulse_low = pintus - phius;
    result.upright = true;
    if (!tkh_digital_is_valid(&result)) result.duration = -1;
    return result;
}

char* tkh_digital_to_string(TkhDigital *tdg, bool command) {
    char buffer[80];
    int stride = (command) ? 9 : 10;
    strcpy(
        buffer,
        (command) ?
            "t12345678 d12345678 y12345678 n12345678 p12345678 q12345678 _" :
            "=12345678;12345678;12345678;12345678;12345678;12345678_"
    );
    buffer[79] = 0;  // Make sure we stop even if we messed up the strings above
    struct timeval tv;
    tv = tkh_timeval_from_micros(tdg->duration);   tkh_encode_time_into(&tv, buffer + 1 + 0*stride, 8);
    tv = tkh_timeval_from_micros(tdg->delay);      tkh_encode_time_into(&tv, buffer + 1 + 1*stride, 8);
    tv = tkh_timeval_from_micros(tdg->block_high); tkh_encode_time_into(&tv, buffer + 1 + 2*stride, 8);
    tv = tkh_timeval_from_micros(tdg->block_low);  tkh_encode_time_into(&tv, buffer + 1 + 3*stride, 8);
    tv = tkh_timeval_from_micros(tdg->pulse_high); tkh_encode_time_into(&tv, buffer + 1 + 4*stride, 8);
    tv = tkh_timeval_from_micros(tdg->pulse_low);  tkh_encode_time_into(&tv, buffer + 1 + 5*stride, 8);
    buffer[6*stride] = (tdg->upright) ? 'u' : 'i';
    return strdup(buffer);
}

bool tkh_is_connected(Ticklish *tkh) {
    tkh->my_port != NULL;
}
