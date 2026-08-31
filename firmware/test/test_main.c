#include <stdio.h>

int mu_tests_run = 0;
int mu_tests_failed = 0;

void run_merge_tests(void);
void run_xbox_report_tests(void);
void run_joycon_decode_tests(void);

int main(void)
{
    printf("== merge_engine ==\n");
    run_merge_tests();
    printf("== xbox_report ==\n");
    run_xbox_report_tests();
    printf("== joycon_decode ==\n");
    run_joycon_decode_tests();

    printf("\n%d checks, %d failed\n", mu_tests_run, mu_tests_failed);
    return mu_tests_failed == 0 ? 0 : 1;
}
