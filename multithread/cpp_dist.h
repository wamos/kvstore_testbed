#ifndef CPP_DIST_H
#define CPP_DIST_H

#include <iostream>
#include <random>
#include <vector>

// class AAA {
//     public:
//         AAA();
//         void sayHi(const char *name);
// };

class DistGen {
    public:
        DistGen();
        ~DistGen();
        void ExponentialGen(double rate, uint32_t size);
        int OutputExponentialArray(double* output_array);

    private:
        std::vector<double> exponential_vector;
};

#endif //CPP_DIST_H
