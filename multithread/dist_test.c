#include "dist_gen.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    int hist[30] = {0};
    double* poisson_arrival = (double *)malloc( 10000* sizeof(double) );

    GenPoissonArrival(1, 10000, poisson_arrival);    
    for(int n = 0; n < 100; n++) {
        printf("%.3lf\n", poisson_arrival[n]);
    }
    printf("\n");

    return 0;
}