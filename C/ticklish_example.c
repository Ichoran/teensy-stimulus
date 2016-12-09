/* Copyright (c) 2016 by Rex Kerr and Calico Life Sciences */

#include <stdio.h>
#include <stdlib.h>

#include "ticklish_util.h"
#include "ticklish.h"

int main(int argn, char** args) {
    Ticklish **tkhs;
    int ntkh = tkh_find_all_ticklish(&tkhs);
    if (tkhs == NULL) printf("Did not find any Ticklish.\n");
    else {
        printf("Found %d!\n", ntkh);
        for (int i = 0; i < ntkh; i++) { tkh_destruct(tkhs[i]); }
        free(tkhs);
        tkhs = NULL;
    }
    return 0;
}
