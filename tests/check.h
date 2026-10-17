#ifndef TESTS_CHECK_H
#define TESTS_CHECK_H

#include <stdio.h>
#include <stdlib.h>

extern int test_failures;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            test_failures++;                                      \
        }                                                         \
    } while (0)

#define LOAD_OR_EXIT(call, err)                                                        \
    do {                                                                               \
        if (!(call)) {                                                                 \
            printf("FAIL %s:%d cannot load game data: %s\n", __FILE__, __LINE__, err); \
            exit(1);                                                                   \
        }                                                                              \
    } while (0)

#endif
