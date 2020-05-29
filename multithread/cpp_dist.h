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
        int OutputExponentialInMicroseconds(double* output_array);

    private:
        std::vector<double> exponential_vector;
};

#endif //CPP_DIST_H
