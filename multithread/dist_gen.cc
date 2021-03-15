#include "dist_gen.h"
#include "cpp_dist.h"

#ifdef __cplusplus
extern "C" {
#endif

//ref: https://stackoverflow.com/questions/2744181/how-to-call-c-function-from-c
// Inside this "extern C" block, I can implement functions in C++, which will externally 
//   appear as C functions (which means that the function IDs will be their names, unlike
//   the regular C++ behavior, which allows defining multiple functions with the same name
//   (overloading) and hence uses function signature hashing to enforce unique IDs),

void GenPoissonArrival(double rate, uint32_t size, double* poisson_array) {
    DistGen dist_gen;
    dist_gen.ExponentialGen(rate, size);
    dist_gen.OutputExponentialInMicroseconds(poisson_array);
}

void GenUniformDist(int lower_bound, int upper_bound, uint32_t seed, uint32_t size, int* output_array){
    DistGen dist_gen;
    dist_gen.UniformRandomGen(lower_bound, upper_bound, seed, size);
    dist_gen.OutputUniformRandomInMicroseconds(output_array);
}

void GenBimoalDist(double dist1_prob, uint32_t dist1_value, uint32_t dist2_value, uint32_t size, int* output_array){
    DistGen dist_gen;
    dist_gen.BimodalGen(dist1_prob, dist1_value, dist2_value, size);
    dist_gen.OutputBimodalInMicroseconds(output_array);
}

void GenExpDist(double rate, uint32_t size, uint32_t* output_array){
    DistGen dist_gen;
    dist_gen.ExponentialGen(rate, size);
    dist_gen.OutputExponential(output_array);
}

#ifdef __cplusplus
}
#endif