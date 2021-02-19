#ifndef CPP_DIST_H
#define CPP_DIST_H

#include <iostream>
#include <random>
#include <vector>

class DistGen {
    public:
        DistGen();
        ~DistGen();
        void ExponentialGen(double rate, uint32_t size);
        void UniformRandomGen(int lower_bound, int upper_bound, uint32_t seed, uint32_t size);
        void BimodalGen(double dist1_prob, uint32_t dist1_value, uint32_t dist2_value, uint32_t size);
        int OutputExponentialInMicroseconds(double* output_array);
        int OutputUniformRandomInMicroseconds(int* output_array);
        int OutputBimodalInMicroseconds(int* output_array);

    private:
        std::vector<double> exponential_vector;
        std::vector<int> uniform_random_vector;
        std::vector<int> bimodal_vector;
};

#endif //CPP_DIST_H
