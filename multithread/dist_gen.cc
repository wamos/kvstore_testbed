#include "dist_gen.h"
#include "cpp_dist.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inside this "extern C" block, I can implement functions in C++, which will externally 
//   appear as C functions (which means that the function IDs will be their names, unlike
//   the regular C++ behavior, which allows defining multiple functions with the same name
//   (overloading) and hence uses function signature hashing to enforce unique IDs),

void GenPoissonArrival(double rate, uint32_t size, double* poisson_array) {
    DistGen dist_gen;
    dist_gen.ExponentialGen(rate, size);
    dist_gen.OutputExponentialArray(poisson_array);
}

#ifdef __cplusplus
}
#endif