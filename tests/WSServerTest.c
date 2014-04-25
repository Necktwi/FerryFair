/*
 * File:   WSServerTest.c
 * Author: Gowtham
 *
 * Created on Dec 12, 2013, 11:32:23 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include "libwebsockets.h"

/*
 * Simple C Test Suite
 */


void testWSServer() {
    int argc=1;
    char argv[1][9]={"WSServer"};
    int result = WSServer(argc, argv);
    if (1 /*check result*/) {
        printf("%%TEST_FAILED%% time=0 testname=testWSServer (WSServerTest) message=error message sample\n");
    }
}

int main(int argc, char** argv) {
    printf("%%SUITE_STARTING%% WSServerTest\n");
    printf("%%SUITE_STARTED%%\n");

    printf("%%TEST_STARTED%%  testWSServer (WSServerTest)\n");
    testWSServer();
    printf("%%TEST_FINISHED%% time=0 testWSServer (WSServerTest)\n");

    printf("%%SUITE_FINISHED%% time=0\n");

    return (EXIT_SUCCESS);
}
