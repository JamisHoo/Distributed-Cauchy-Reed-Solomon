#include "timer.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

double timer_start() {
    struct timeval timer;
    
    gettimeofday(&timer, 0);

    return timer.tv_sec + (double)timer.tv_usec / 1e6;
}

double timer_end(double start_time, const char* prompt) {
    struct timeval timer;
    double end_time;

    gettimeofday(&timer, 0);
    end_time = timer.tv_sec + (double)timer.tv_usec / 1e6;

    printf(prompt, end_time - start_time);

    return end_time;
}


