/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ticklish_util.h"
#include "ticklish.h"

int main(int argn, char** args) {
    int i;
    if (argn > 1) {
        printf("This example does not take any arguments.\n");
        return 1;
    }

    Ticklish **tkhs;
    int ntkh = tkh_find_all_ticklish(&tkhs);
    if (ntkh == 0 || tkhs == NULL) {
        printf("Did not find any Ticklish.\n");
        return 1;
    }

    printf("We're going to check for working serial ports!\n");
    int nserp = 0;
    char **serpnames = NULL;
    nserp = tkh_get_all_port_descriptions(&serpnames);
    if (nserp == 0 || serpnames == NULL) {
        printf("Did not find any ports at all.  Did you plug anything in??\n");
        return 1;
    }
    printf("Found %d ports!\n", nserp);
    for (i = 0; i < nserp; i++) {
        printf("  %s\n", serpnames[i]);
        free(serpnames[i]);
    }
    free(serpnames);
    serpnames = NULL;

    printf("Now getting a Teensy board running Ticklish.\n");
    Ticklish *tkh = tkh_find_first_ticklish();
    if (tkh == NULL) {
        printf("Didn't get one :(  Quitting.\n");
        return 1;
    }

    printf("\n");
    printf("Very good!  We got %s, opened it, and verified it works.\n", tkh->portname);
    printf("\n");
    printf("Let's check the ID.\n");
    char* tid = tkh_id(tkh);
    printf("  Hello, I'm Ticklish and my name is: %s\n", tid);
    if (tid != NULL) { free(tid); tid = NULL; }

    printf("\n");
    printf("Now let's set up a protocol.\n");
    printf("  First we'll wait for 3 seconds.\n");
    printf("  Then we'll blink once a second (half on, half off) 10 times\n");
    printf("  Then we'll wait for 5 more seconds.\n");
    printf("  Then we'll do five triple-blinks every two seconds\n");
    printf("    (A triple-blink is 100 ms on, 200 ms off.)\n");
    printf("\n");
    printf("Let's set up.\n");
    tkh_clear(tkh);
    if (tkh->error_value != 0) {
        printf("Got an error!  Quitting!\n");
        return 1;
    }

    enum TkhState state = tkh_state(tkh);
    printf("Cleared previous state.  Ready to program: %s\n", (state == TKH_PROGRAM) ? "true" : "false");

    TkhDigital part_one = tkh_simple_digital('X', 3, 1, 0.5, 10);
    TkhDigital part_two = tkh_pulsed_digital('X', 5.0, 2.0, 5, 0.3, 0.1, 3);

    char* part_one_string = tkh_digital_to_string(&part_one, false);
    char* part_two_string = tkh_digital_to_string(&part_two, false);
    printf("Protocol (two steps):\n");
    printf("  %s\n", part_one_string);
    printf("  %s\n", part_two_string);
    free(part_one_string);
    free(part_two_string);

    TkhDigital parts[2] = { part_one, part_two };

    tkh_set(tkh, parts, 2);
    if (tkh->error_value != 0) {
        printf("Got an error while setting!  Quitting!\n");
        return 1;
    }

    printf("\n");
    printf("All set.  Errors?  %s.\n", (tkh_is_error(tkh)) ? "Yes" : "No");

    printf("\n");
    printf("Let's GO!\n");
    TkhTimed tkt = tkh_run(tkh);
    if (tkh->error_value != 0) {
        printf("Got an error while trying to start run.  Quitting!\n");
        return 1;
    }
    printf("\n");
    printf("Now running; computer and Ticklish board synced.\n");
    printf("  Max error estimated as %ld s, %ld us\n", tkt.window.tv_sec, tkt.window.tv_usec);
    printf("\n");
    printf("Check out the lights for a bit!  We'll wait.\n");

    usleep(7000000);

    TkhTimed tkt2 = tkh_timesync(tkh);
    struct timeval elapsed = tkt2.timestamp;
    tkh_timeval_minus_eq(&elapsed, &tkt.timestamp);
    struct timeval expected = tkt.board_at;
    tkh_timeval_plus_eq(&expected, &elapsed);
    struct timeval maxerror = tkt.window;
    tkh_timeval_plus_eq(&maxerror, &tkt2.window);
    struct timeval ourerror;
    if (tkh_timeval_compare(&tkt2.board_at, &expected) < 0) {
        ourerror = expected;
        tkh_timeval_minus_eq(&ourerror, &tkt2.board_at);
    }
    else {
        ourerror = tkt2.board_at;
        tkh_timeval_minus_eq(&ourerror, &expected);
    }
    printf("Okay, we expect to be %lld us into the protocol now.\n", tkh_micros_from_timeval(&expected));
    printf("And the board reports: %lld us\n", tkh_micros_from_timeval(&tkt2.board_at));
    printf("  We thought the error could be as big as %lld us\n", tkh_micros_from_timeval(&maxerror));
    printf("  And it was actually %lld us\n", tkh_micros_from_timeval(&ourerror));
    printf("\n");
    printf("Okay, let's wait until we're done.\n");

    int count = 0;
    while (tkh_is_run(tkh)) {
      printf("  Not yet!\n");
      usleep(2000000);
      count++;
      if (count * 2000000ll > part_one.duration + part_two.duration) {
        printf("    Um...we didn't stop???  Aborting.\n");
        tkh_clear(tkh);
      }
    }

    printf("Done!\n");
    printf("\n");
    printf("Cleaning up!\n");
    tkh_clear(tkh);
    tkh_disconnect(tkh);
    printf("\n");
    printf("All done.\n");

    tkh_destruct(tkh);
    return 0;
}
