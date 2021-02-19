#ifndef DIST_GEN_H
#define DIST_GEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void GenPoissonArrival(double rate, uint32_t size, double* poisson_array);
void GenUniformDist(int lower_bound, int upper_bound, uint32_t seed, uint32_t size, int* output_array);
void GenBimoalDist(double dist1_prob, uint32_t dist1_value, uint32_t dist2_value, uint32_t size, int* output_array);
void GenExpDist(double rate, uint32_t size, double* output_array);

#ifdef __cplusplus
}
#endif

#endif //DIST_GEN_H