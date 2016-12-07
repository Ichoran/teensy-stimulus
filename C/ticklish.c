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


Ticklish* tkh_construct(struct sp_port* port) {
    Ticklish *tv = (Ticklish*)malloc(sizeof(Ticklish));
    tv->my_port = port;
    tv->portname = strdup(sp_get_port_name(tv->my_port));
    tv->my_port = NULL;
    tv->my_id = NULL;
    tv->buffer = NULL;
    tv->buffer_start = 0;
    tv->buffer_end = 0;
    tv->patience = 0;
    tv->error_value = 0;
    pthread_mutexattr_t pmat;
    pthread_mutexattr_init(&pmat);
    pthread_mutexattr_settype(&pmat, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&(tv->my_mutex), &pmat);
    pthread_mutexattr_destroy(&pmat);  // Contract is contents are COPIED.  Thus, we can destroy it now!
    return tv;
}

void tkh_destruct(Ticklish *tkh) {
    if (tkh->portname != NULL) {
        pthread_mutex_lock(&(tkh->my_mutex));
        tkh_disconnect(tkh);
        if (tkh->my_port != NULL) { sp_free_port((struct sp_port*)tkh->my_port); tkh->my_port = NULL; }
        if (tkh->my_id != NULL) { free((void*)tkh->my_id); tkh->my_id = NULL; }
        if (tkh->buffer != NULL) { free((void*)tkh->buffer); tkh->buffer = NULL; }
        if (tkh->portname != NULL) { free((void*)tkh->portname); tkh->portname = NULL; }
        pthread_mutex_unlock(&(tkh->my_mutex));
        pthread_mutex_destroy(&(tkh->my_mutex));
    }
}


bool tkh_is_connected(Ticklish *tkh) {
    tkh->buffer != NULL;
}


void tkh_connect(Ticklish *tkh) {
    if (!tkh_is_connected(tkh)) {
        pthread_mutex_lock(&(tkh->my_mutex));
        if (tkh->my_port != NULL && tkh->buffer == NULL) {
            sp_set_bits(tkh->my_port, 8);
            sp_set_parity(tkh->my_port, SP_PARITY_NONE);
            sp_set_stopbits(tkh->my_port, 1);
            sp_set_baudrate(tkh->my_port, 115200);
            enum sp_return sp_ok = sp_open(tkh->my_port, SP_MODE_READ_WRITE);
            if (sp_ok == SP_OK) {
                tkh->buffer = (char*)malloc(TICKLISH_BUFFER_N);
                tkh->buffer_start = 0;
                tkh->buffer_end = 0;
                tkh->error_value = 0;
                tkh->patience = -1;                
            }
            else tkh->error_value = 1;
        }
        pthread_mutex_unlock(&(tkh->my_mutex));
    }
}

void tkh_disconnect(Ticklish *tkh) {
    if (tkh_is_connected(tkh)) {
        pthread_mutex_lock(&(tkh->my_mutex));
        if (tkh->buffer != NULL && tkh->my_port != NULL) {
            enum sp_return sp_ok = sp_close(tkh->my_port);
            if (sp_ok == SP_OK) {
                free((void*)(tkh->buffer));
                tkh->buffer = NULL;
                tkh->buffer_start = 0;
                tkh->buffer_end = 0;
                tkh->error_value = 0;
                tkh->patience = -1;
            }
            else tkh->error_value = 1;
        }
        pthread_mutex_unlock(&(tkh->my_mutex));
    }
}

int tkh_wait_for_next_buffer(Ticklish *tkh) {
    char buffer[128];
    int N = TICKLISH_BUFFER_N - (tkh->buffer_end - tkh->buffer_start);
    if (N > 128) N = 128;
    if (N <= 0) return -1;
    enum sp_return ret = sp_blocking_read_next(tkh->my_port, buffer, N, tkh->patience);
    if (ret <= 0) return -1;
    else {
        int n = (ret >= 128) ? 128 : ret;
        pthread_mutex_lock(&(tkh->my_mutex));
        if (n < TICKLISH_BUFFER_N - tkh->buffer_end) {
            memcpy((void*)(tkh->buffer + tkh->buffer_end), buffer, n);
            tkh->buffer_end += n;
            n = 0;
        }
        else {
            if (tkh->buffer_start > 0) {            
                memmove((void*)(tkh->buffer + tkh->buffer_start), (void*)tkh->buffer, tkh->buffer_end - tkh->buffer_start);
                tkh->buffer_end -= tkh->buffer_start;
                tkh->buffer_start = 0;
            }
            int m = TICKLISH_BUFFER_N - tkh->buffer_end;
            if (n < m) m = n;
            memcpy((void*)(tkh->buffer + tkh->buffer_end), buffer, m);
            tkh->buffer_end += m;
            n -= m;
        }
        pthread_mutex_unlock(&(tkh->my_mutex));
        return n;
    }
}


char* thk_fixed_read(Ticklish *tkh, int n, bool twiddled) {
    if (tkh->buffer == NULL || n <= 0) return NULL;
    char* buffer = (char*)malloc(n+1);
    int i = 0;
    int ret = 0;
    do {
        pthread_mutex_lock(&(tkh->my_mutex));
        while (!twiddled && tkh->buffer_start < tkh->buffer_end) { 
            twiddled = tkh->buffer[tkh->buffer_start] == '~';
            tkh->buffer_start++;
        }
        if (tkh->buffer_end - tkh->buffer_start >= n - i) {
            memcpy(buffer + i, tkh->buffer + tkh->buffer_start, n - i);
            tkh->buffer_start += n - i;
            i = n;
        }
        else if (tkh->buffer_end > tkh->buffer_start) {
            memcpy(buffer + i, tkh->buffer + tkh->buffer_start, tkh->buffer_end - tkh->buffer_start);
            i += tkh->buffer_end - tkh->buffer_start;
            tkh->buffer_start = tkh->buffer_end = 0;
        }
        pthread_mutex_unlock(&(tkh->my_mutex));
        if (i < n) ret = tkh_wait_for_next_buffer(tkh);
    } until (i == n || ret != 0);
    if (ret != 0) {
        free((void*)buffer);
        return NULL;
    }
    else {
        buffer[n] = 0;
        return buffer;
    }
}


char* thk_flex_read(Ticklish *tkh, bool dollared) {
    if (tkh->buffer == NULL) return NULL;
    char* buffer = (char*)malloc(64);
    int N = 64;
    int i = 0;
    bool mistake = false;
    bool newlined = false;
    do {
        pthread_mutex_lock(&(tkh->my_mutex));
        while (!dollared && tkh->buffer_start < tkh->buffer_end) { 
            twiddled = tkh->buffer[tkh->buffer_start] == '$';
            tkh->buffer_start++;
        }
        while (dollared && tkh->buffer_end > tkh->buffer_start && !newlined) {
            char c = tkh->buffer[tkh->buffer_start];
            tkh->buffer_start++;
            if (c == '\n') newlined = true;
            else if (c == '~') {
                tkh->buffer_start--;
                newlined = true;
                mistake = true;
            }
            else {
                buffer[i] = c;
                i++;
                if (i >= N) {
                    char* temp = (char*)malloc(N*2);
                    memcpy(temp, buffer, N);
                    free((void*)buffer);
                    buffer = temp;
                    N *= 2;
                }
            }
        }
        pthread_mutex_unlock(&(tkh->my_mutex));
        if (!newlined) ret = tkh_wait_for_next_buffer(tkh);
    } until (newlined);
    if (mistake) {
        free((void*)buffer);
        return NULL;
    }
    else {
        buffer[n] = 0;
        return buffer;
    }
}

void tkh_write(Ticklish *tkh, const char *s) {

}


char* tkh_query(Ticklish *tkh, const char *message, int n) {
    pthread_mutex_lock(&(tkh->my_mutex));
    tkh->error_value = 0;
    pthread_mutex_unlock(&(tkh->my_mutex));
    tkh_write(tkh, message);
    if (tkh->error_value) return NULL;
    return tkh_fixed_read(tkh, n, false);
}


char* tkh_flex_query(Ticklish *tkh, const char *message) {
    pthread_mutex_lock(&(tkh->my_mutex));
    tkh->error_value = 0;
    pthread_mutex_unlock(&(tkh->my_mutex));
    tkh_write(tkh, message);
    if (tkh->error_value) return NULL;
    return tkh_flex_read(tkh, false);
}

