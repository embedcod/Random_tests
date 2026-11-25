#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

void app_main(void)
{
    while (true) {
        printf("Good afternoon Engineer?\n");
        sleep(5);
    }
}