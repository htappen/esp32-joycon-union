/* Minimal test harness (no external deps). */
#ifndef MINUNIT_H
#define MINUNIT_H

#include <stdio.h>
#include <string.h>

extern int mu_tests_run;
extern int mu_tests_failed;

#define mu_check(msg, cond)                                                  \
    do {                                                                    \
        mu_tests_run++;                                                     \
        if (!(cond)) {                                                      \
            mu_tests_failed++;                                              \
            printf("  FAIL: %s (%s:%d) [%s]\n", (msg), __FILE__, __LINE__,  \
                   #cond);                                                  \
        }                                                                   \
    } while (0)

#define mu_eq_int(msg, expect, actual)                                       \
    do {                                                                    \
        mu_tests_run++;                                                     \
        long _e = (long)(expect), _a = (long)(actual);                     \
        if (_e != _a) {                                                     \
            mu_tests_failed++;                                              \
            printf("  FAIL: %s (%s:%d) expected %ld got %ld\n", (msg),      \
                   __FILE__, __LINE__, _e, _a);                            \
        }                                                                   \
    } while (0)

#define mu_run(test_fn)                                                      \
    do {                                                                    \
        printf("%s\n", #test_fn);                                           \
        test_fn();                                                          \
    } while (0)

#endif /* MINUNIT_H */
