#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include "dist_gen.h"

int main() {
    int hist[30] = {0};
    double* poisson_arrival = (double *)malloc( 1000* sizeof(double) );

    double rate = 10000.0;
    uint64_t timestamp = 0;
    GenPoissonArrival(rate, 1000, poisson_arrival);   
    for(int n = 0; n < 1000; n++) {
        printf("%" PRIu64 ", %.3lf\n", (uint64_t) round(poisson_arrival[n]), poisson_arrival[n]);
        //uint64_t temp = (uint64_t) poisson_arrival[n];
        //printf("%" PRIu64 "\n", timestamp + temp);
    }
    printf("\n");

    return 0;
}