/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "ticklish_util.h"
#include "ticklish.h"

#define LOCKON pthread_mutex_lock(&(tkh->my_mutex))
#define UNLOCK pthread_mutex_unlock(&(tkh->my_mutex))


/**********************/
/* TkhTimed functions */
/**********************/

void tkh_timed_init(TkhTimed *tkt) {
    tkt->zero.tv_sec = 0;
    tkt->zero.tv_usec = -1;
    tkt->window.tv_sec = 0;
    tkt->window.tv_usec = -1;
    tkt->timestamp.tv_sec = 0;
    tkt->timestamp.tv_usec = -1;
    tkt->board_at.tv_sec = 0;
    tkt->board_at.tv_usec = -1;
}

bool tkh_timed_is_valid(TkhTimed *tkt) {
    return
        tkh_timeval_is_valid(&(tkt->zero)) && 
        tkh_timeval_is_valid(&(tkt->window)) &&
        tkh_timeval_is_valid(&(tkt->timestamp)) &&
        tkh_timeval_is_valid(&(tkt->board_at));
}

char* tkh_timed_to_string(TkhTimed *tkt) {
    char buffer[144];
    snprintf(
        buffer, 144,
        "%ld.%06ld + <= %ld.%06ld; here %ld.%06ld, there %ld.%06ld",
        tkt->zero.tv_sec, tkt->zero.tv_usec,
        tkt->window.tv_sec, tkt->window.tv_usec,
        tkt->timestamp.tv_sec, tkt->timestamp.tv_usec,
        tkt->board_at.tv_sec, tkt->board_at.tv_usec
    );
    buffer[143] = 0;
    return strdup(buffer);
}



/************************/
/* TkhDigital functions */
/************************/

bool tkh_digital_is_valid(TkhDigital *tkh) {
    return 
        tkh->channel >= 'A' && tkh->channel <= 'X' &&
        tkh->duration > 0 && tkh->delay > 0 && tkh->block_high > 0 && tkh->block_low >= 0 && tkh->pulse_high >= 0 && tkh->pulse_low >= 0 &&
        tkh->duration <= TKH_MAX_TIME_MICROS && tkh->delay <= TKH_MAX_TIME_MICROS &&
        tkh->block_high <= TKH_MAX_TIME_MICROS && tkh->block_low <= TKH_MAX_TIME_MICROS &&
        tkh->pulse_high <= TKH_MAX_TIME_MICROS && tkh->pulse_low <= TKH_MAX_TIME_MICROS;
}

TkhDigital tkh_zero_digital(char channel) {
    TkhDigital result;
    result.channel = channel;
    result.duration = 0;
    result.delay = 0;
    result.block_high = 0;
    result.block_low = 0;
    result.pulse_high = 0;
    result.pulse_low = 0;
    result.upright = true;
    return result;
}

TkhDigital tkh_simple_digital(char channel, double delay, double interval, double high, unsigned int count) {
    TkhDigital result = tkh_zero_digital(channel);
    result.duration = -1;  // Invalid default
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
    TkhDigital result = tkh_zero_digital(channel);
    result.duration = -1;  // Invalid default
    long long delus = (long long)rint(1e6 * delay);
    long long intus = (long long)rint(1e6 * interval);
    long long pintus  = (long long)rint(1e6 * pulse_interval);
    long long phius = (long long)rint(1e6 * pulse_high);
    long long hius = phius + (pulse_count-1)*pintus;
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
        (!command) ?
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


/*****************************/
/* Ticklish struct functions */
/*****************************/

Ticklish* tkh_construct(struct sp_port* port) {
    Ticklish *tv = (Ticklish*)malloc(sizeof(Ticklish));
    tv->my_port = port;
    tv->portname = strdup(sp_get_port_name(tv->my_port));
    tv->my_id = NULL;
    tv->buffer = NULL;
    tv->buffer_start = 0;
    tv->buffer_end = 0;
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
        LOCKON;
        tkh_disconnect(tkh);
        if (tkh->my_port != NULL) { sp_free_port((struct sp_port*)tkh->my_port); tkh->my_port = NULL; }
        if (tkh->my_id != NULL) { free((void*)tkh->my_id); tkh->my_id = NULL; }
        if (tkh->buffer != NULL) { free((void*)tkh->buffer); tkh->buffer = NULL; }
        if (tkh->portname != NULL) { free((void*)tkh->portname); tkh->portname = NULL; }
        UNLOCK;
        pthread_mutex_destroy(&(tkh->my_mutex));
    }
    free(tkh);
}


bool tkh_is_connected(Ticklish *tkh) {
    return tkh->buffer != NULL;
}


void tkh_connect(Ticklish *tkh) {
    if (!tkh_is_connected(tkh)) {
        LOCKON;
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
            }
            else {
                printf("Did not open; error value %d\n", sp_ok);
                tkh->error_value = 1;
            }
        }
        UNLOCK;
    }
}

void tkh_disconnect(Ticklish *tkh) {
    if (tkh_is_connected(tkh)) {
        LOCKON;
        if (tkh->buffer != NULL && tkh->my_port != NULL) {
            enum sp_return sp_ok = sp_close(tkh->my_port);
            if (sp_ok == SP_OK) {
                free((void*)(tkh->buffer));
                tkh->buffer = NULL;
                tkh->buffer_start = 0;
                tkh->buffer_end = 0;
                tkh->error_value = 0;
            }
            else tkh->error_value = 1;
        }
        UNLOCK;
    }
}

int tkh_wait_for_next_buffer(Ticklish *tkh) {
    char buffer[128];
    int N = TICKLISH_BUFFER_N - (tkh->buffer_end - tkh->buffer_start);
    if (N > 128) N = 128;
    if (N <= 0) return -1;
    enum sp_return ret = sp_blocking_read_next(tkh->my_port, buffer, N, TICKLISH_PATIENCE);
    if (ret <= 0) return -1;
    else {
        int n = (ret >= 128) ? 128 : ret;
        ;
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


char* tkh_fixed_read(Ticklish *tkh, int n, bool twiddled) {
    if (tkh->buffer == NULL || n <= 0) return NULL;
    char* buffer = (char*)malloc(n+1);
    int i = 0;
    int ret = 0;
    do {
        LOCKON;
        while (!twiddled && tkh->buffer_start < tkh->buffer_end) { 
            twiddled = tkh->buffer[tkh->buffer_start] == '~';
            tkh->buffer_start++;
        }
        if (tkh->buffer_end - tkh->buffer_start >= n - i) {
            memcpy(buffer + i, (char*)(tkh->buffer + tkh->buffer_start), n - i);
            tkh->buffer_start += n - i;
            i = n;
        }
        else if (tkh->buffer_end > tkh->buffer_start) {
            memcpy(buffer + i, (char*)(tkh->buffer + tkh->buffer_start), tkh->buffer_end - tkh->buffer_start);
            i += tkh->buffer_end - tkh->buffer_start;
            tkh->buffer_start = tkh->buffer_end = 0;
        }
        UNLOCK;
        if (i < n) ret = tkh_wait_for_next_buffer(tkh);
    } while (!(i == n || ret != 0));
    if (ret != 0) {
        free((void*)buffer);
        return NULL;
    }
    else {
        buffer[n] = 0;
        return buffer;
    }
}


char* tkh_flex_read(Ticklish *tkh, bool dollared) {
    if (tkh->buffer == NULL) return NULL;
    char* buffer = (char*)malloc(64);
    int N = 64;
    int i = 0;
    bool mistake = false;
    bool newlined = false;
    do {
        LOCKON;
        while (!dollared && tkh->buffer_start < tkh->buffer_end) { 
            dollared = tkh->buffer[tkh->buffer_start] == '$';
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
        UNLOCK;
        if (!newlined) {
            int ret = tkh_wait_for_next_buffer(tkh);
            if (ret < 0) {
                mistake = true;
                newlined = true;
            }
        }
    } while (!newlined);
    if (mistake) {
        free((void*)buffer);
        return NULL;
    }
    else {
        buffer[i] = 0;
        return buffer;
    }
}

void tkh_write(Ticklish *tkh, const char *s) {
    LOCKON;
    tkh->error_value = 0;
    if (!tkh_is_connected(tkh)) {
        tkh_connect(tkh);
    }
    UNLOCK;
    if (tkh->error_value != 0) return;
    int n = strnlen(s, TICKLISH_MAX_OUT);
    enum sp_return ret = sp_blocking_write(tkh->my_port, s, n, TICKLISH_PATIENCE);
    if (ret != n) {
        LOCKON;
        tkh->error_value = -1;
        UNLOCK;
    }
}


char* tkh_query(Ticklish *tkh, const char *ask, int n) {
    LOCKON;
    tkh->error_value = 0;
    UNLOCK;
    tkh_write(tkh, ask);
    if (tkh->error_value) return NULL;
    return tkh_fixed_read(tkh, n, false);
}


char* tkh_flex_query(Ticklish *tkh, const char *ask) {
    LOCKON;
    tkh->error_value = 0;
    UNLOCK;
    tkh_write(tkh, ask);
    if (tkh->error_value) return NULL;
    return tkh_flex_read(tkh, false);
}


bool tkh_is_ticklish(Ticklish *tkh) {
    char* reply = tkh_flex_query(tkh, "~?");
    if (reply == NULL) return false;
    bool ans = (tkh->error_value == 0) && tkh_string_is_ticklish(reply);
    free((void*)reply);
    return ans;
}


char* tkh_id(Ticklish *tkh) {
    if (tkh->my_id != NULL) {
        LOCKON;
        char* ans = (tkh->my_id != NULL) ? strdup((char*)tkh->my_id) : NULL;
        UNLOCK;
        if (ans != NULL) return ans;
    }
    char* reply = tkh_flex_query(tkh, "~?");
    if (reply == NULL) return NULL;
    if (tkh->error_value != 0) {
        free((void*) reply);
        return NULL;
    }
    char name[64];
    char version[4];
    tkh_decode_name_into(reply, name, 60, version);
    char* my_name = strndup(name, 60);
    free((void*) reply);
    LOCKON;
    tkh->my_id = my_name;
    // Manually unrolled assignment due to volatile modifier not playing nice with strncpy
    tkh->version[0] = version[0];
    tkh->version[1] = version[1];
    tkh->version[2] = version[2];
    tkh->version[3] = version[3];
    UNLOCK;
    char* ans = strdup(my_name);
    return ans;
}


enum TkhState tkh_state(Ticklish *tkh) {
    char* reply = tkh_query(tkh, "~@", 1);
    if (reply == NULL) return TKH_UNKNOWN;
    enum TkhState ans = (tkh->error_value == 0) ? tkh_decode_state(reply) : TKH_UNKNOWN;
    free((void*) reply);
    return ans;
}


bool tkh_ping(Ticklish *tkh) {
    char* reply = tkh_flex_query(tkh, "~'");
    if (reply == NULL) return false;
    bool ans = (tkh->error_value == 0) && !(*reply);
    free((void*) reply);
    return ans;
}


void tkh_clear(Ticklish *tkh) {
    tkh_write(tkh, "~.");
    if (!tkh_ping(tkh)) {
        LOCKON;
        tkh->error_value = -1;
        UNLOCK;
    }
}


bool tkh_is_error(Ticklish *tkh) { 
    enum TkhState s = tkh_state(tkh);
    return (s == TKH_ERRORED) || (s == TKH_UNKNOWN);
}

bool tkh_is_prog(Ticklish *tkh) { return tkh_state(tkh) == TKH_PROGRAM; }

bool tkh_is_run(Ticklish *tkh) { return tkh_state(tkh) == TKH_RUNNING; }

bool tkh_is_done(Ticklish *tkh) { return tkh_state(tkh) == TKH_ALLDONE; }


TkhTimed tkh_timesync(Ticklish *tkh) {
    struct timeval tv0, tv1;
    TkhTimed tkt;
    tkh_timed_init(&tkt);
    int errorcode;
    errorcode = gettimeofday(&tv0, NULL);
    if (errorcode) {
        LOCKON;
        tkh->error_value = -1;
        UNLOCK;
        return tkt;
    }
    char* reply = tkh_flex_query(tkh, "~#");
    if (reply == NULL) {
        LOCKON;
        tkh->error_value = -1;
        UNLOCK;
        return tkt;
    }
    if (tkh->error_value != 0) {
        free((void*) reply);
        return tkt;
    }
    errorcode = gettimeofday(&tv1, NULL);
    if (errorcode || !tkh_string_is_time_report(reply)) {
        LOCKON;
        tkh->error_value = -1;
        UNLOCK;
        free((void*) reply);
        return tkt;
    }
    struct timeval tvb = tkh_decode_time(reply);
    free((void*) reply);
    struct timeval tvw = tv1;
    tkh_timeval_minus_eq(&tvw, &tv0);
    if (tkh_timeval_compare(&tv1, &tv0) == 0) { tvw.tv_usec = 5000; }  // Recklessly guess 5 ms difference
    tkt.zero = tv0;
    tkh_timeval_minus_eq(&(tkt.zero), &tvb);
    tkt.window = tvw;
    tkt.timestamp = tv0;
    tkt.board_at = tvb;
    return tkt;
}

double tkh_get_drift(Ticklish *tkh) {
    char *reply = tkh_query(tkh, "~^+00000000?", 11);
    double ans = tkh_decode_drift(reply);
    free((void*) reply);
    return ans;
}

double tkh_set_drift(Ticklish *tkh, double drift, bool writeEEPROM) {
    char buffer[16];
    if (tkh_is_error(tkh)) return NAN;
    buffer[0] = '~';
    buffer[1] = '^';
    tkh_encode_drift_into(drift, buffer+2, 13);
    buffer[11] = (writeEEPROM) ? '!' : '.';
    buffer[12] = 0;
    char *reply = tkh_query(tkh, buffer, 11);
    double ans = tkh_decode_drift(reply);
    free((void*) reply);
    return ans;
}

int tkh_fix_drift(Ticklish *tkh, TkhTimed *first, TkhTimed *second, double minDrift, bool writeEEPROM) {
    struct timeval zero_tv = second->zero;
    tkh_timeval_minus_eq(&zero_tv, &(first->zero));
    double delta_zero = tkh_timeval_to_double(&zero_tv);
    struct timeval board_tv = second->board_at;
    tkh_timeval_minus_eq(&board_tv, &(first->board_at));
    double delta_board = tkh_timeval_to_double(&board_tv);
    double drift = (delta_board == 0) ? 0 : delta_zero/delta_board;
    double already = tkh_get_drift(tkh);
    if (fabs(drift) < minDrift) return 0;
    if (isnan(already) || isnan(tkh_set_drift(tkh, drift + already, writeEEPROM))) return -1;
    if (tkh_is_error(tkh)) return -1;
    return 1;
}

int tkh_zero_drift(Ticklish *tkh) {
    char *reply = tkh_query(tkh, "~^+00000000.", 11);
    int ans = isnan(tkh_decode_drift(reply));
    free((void*) reply);
    return ans;
}

bool tkh_private_check_channels(TkhDigital *protocols, int n) {
    for (int i = 0; i < n; i++) {
        char c = protocols[i].channel;
        if (c < 'A' || c > 'X') return false;
    }
    return true;
}

void tkh_set(Ticklish *tkh, TkhDigital *protocols, int n) {
    int counts[24];
    if (!tkh_private_check_channels(protocols, n)) {
        LOCKON;
        tkh->error_value = -1;
        UNLOCK;
        return;
    }
    int i,j;
    for (j = 0; j < 24; j++) counts[j] = 0;
    char buffer[64];
    for (i = 0; i < n; i++) {
        char channel = protocols[i].channel;
        buffer[0] = '~';
        buffer[1] = channel;
        if (counts[channel - 'A']) {
            buffer[2] = '&';
            buffer[3] = 0;            
            tkh_write(tkh, buffer);
            if (tkh->error_value != 0) return;
        }
        char *cmd = tkh_digital_to_string(protocols + i, true);
        if (cmd == NULL) {
            LOCKON;
            tkh->error_value = -1;
            UNLOCK;
            return;
        }
        int l = strnlen(cmd, 61);
        memcpy(buffer + 2, cmd, l);
        free((void*) cmd);
        buffer[l+2] = 0;
        counts[channel - 'A']++;
        tkh_write(tkh, buffer);
        if (tkh->error_value != 0) return;
        if (!tkh_ping(tkh)) return;
    }
}

TkhTimed tkh_run(Ticklish *tkh) {
    TkhTimed tkt;
    tkh_timed_init(&tkt);
    enum TkhState state = tkh_state(tkh);
    switch(state) {
        case TKH_PROGRAM: break;
        case TKH_ALLDONE:
            tkh_write(tkh, "~\"");
            if (!tkh_ping(tkh)) return tkt;
            break;
        default: return tkt;
    }
    tkh_write(tkh, "~*");
    if (tkh->error_value != 0 || !tkh_ping(tkh)) return tkt;
    else return tkh_timesync(tkh);
}


int tkh_private_count_port_pointers(struct sp_port **portptrs) {
    int nports = 0;
    if (portptrs != NULL) for (; portptrs[nports] != NULL; nports++) {}
    return nports;
}


int tkh_get_all_port_descriptions(char ***descsp) {
    struct sp_port **portptrs;
    enum sp_return ret = sp_list_ports(&portptrs);
    if (ret != SP_OK) return 0;
    int nports = tkh_private_count_port_pointers(portptrs);
    if (nports == 0) {
        if (portptrs != NULL) sp_free_port_list(portptrs);
        *descsp = NULL;
        return 0;
    }
    char** descs = (char**)malloc(sizeof(char*) * nports);
    *descsp = descs;
    for (int i = 0; i < nports; i++) {
        struct sp_port *port = portptrs[i];
        char *name = sp_get_port_name(port);
        char *manf = sp_get_port_usb_manufacturer(port);
        int portnamelen = (name != NULL) ? strlen(name) : 6;
        int portmanflen = (manf != NULL) ? strlen(manf) : 6;
        char *desc = (char*)malloc(6 + portnamelen + portmanflen);
        snprintf(desc, 6 + portnamelen + portmanflen, "%s at %s\n", manf, name);
        desc[5 + portnamelen + portmanflen] = 0;
        descs[i] = desc;
    }
    sp_free_port_list(portptrs);
    return nports;
}


Ticklish* tkh_private_find_next_ticklish(struct sp_port **portptrs, int nports, int *j) {
    for (int i = *j; i < nports; i++) {
        struct sp_port *port = portptrs[i];
        const char* manf = sp_get_port_usb_manufacturer(port);
        if (manf != NULL && strcmp(manf, "Teensyduino") == 0) {
            struct sp_port *promising;
            enum sp_return ret = sp_copy_port(port, &promising);
            if (ret == SP_OK) {
                Ticklish *tkh = tkh_construct(promising);
                tkh_connect(tkh);
                if (!tkh_is_ticklish(tkh)) {
                    tkh_destruct(tkh);
                }
                else {
                    *j = i+1;
                    return tkh;
                }
            }
        }        
    }
    *j = nports;
    return NULL;
}


Ticklish* tkh_find_first_ticklish() {
    struct sp_port **portptrs;
    enum sp_return ret = sp_list_ports(&portptrs);
    if (ret != SP_OK) return NULL;
    int nports = tkh_private_count_port_pointers(portptrs);
    if (nports == 0) {
        if (portptrs != NULL) sp_free_port_list(portptrs);
        return NULL;
    }
    int i = 0;
    Ticklish *ans = tkh_private_find_next_ticklish(portptrs, nports, &i);
    sp_free_port_list(portptrs);
    return ans;
}


int tkh_find_all_ticklish(Ticklish ***tkhsp) {
    struct sp_port **portptrs;
    enum sp_return ret = sp_list_ports(&portptrs);
    if (ret != SP_OK) {
        *tkhsp = NULL;
        return 0;
    }
    int nports = tkh_private_count_port_pointers(portptrs);
    if (nports == 0) {
        if (portptrs != NULL) sp_free_port_list(portptrs);
        *tkhsp = NULL;
        return 0;
    }
    int i = 0;
    int k = 0;
    Ticklish **tkhs = (Ticklish**)malloc(sizeof(Ticklish*)*nports);
    while (i < nports) {
        tkhs[k] = tkh_private_find_next_ticklish(portptrs, nports, &i);
        if (tkhs[k] != NULL) k++;
    }
    sp_free_port_list(portptrs);
    if (k == 0) {
        *tkhsp = NULL;
    }
    else if (k == nports) {
        *tkhsp = tkhs;
    }
    else {
        *tkhsp = (Ticklish**)malloc(sizeof(Ticklish*)*k);
        memcpy(*tkhsp, tkhs, sizeof(Ticklish*)*k);
        free(tkhs);
    }
    return k;
}
