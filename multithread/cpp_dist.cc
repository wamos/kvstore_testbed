#include <cstdio>
#include <string>
#include <map>
#include <cstdlib>

#include "cpp_dist.h"
#define USE_CONSTANT_SEED
//ref: https://stackoverflow.com/questions/2744181/how-to-call-c-function-from-c

DistGen::DistGen(){    
}

DistGen::~DistGen(){    
}

void DistGen::ExponentialGen(double rate, uint32_t size){
    srand(1);
    #ifdef USE_CONSTANT_SEED
        std::mt19937 gen(rand());    // Seeded in main with argument
    #else
        std::random_device rd;
        std::mt19937 gen(rd());
    #endif
    std::exponential_distribution<double> expo_dist(rate);
    exponential_vector.reserve(size);
    for(uint32_t i = 0; i < size; i++) {
        exponential_vector.push_back(expo_dist(gen));
    }
}

int DistGen::OutputExponentialInMicroseconds(double* output_array){
    if(output_array != nullptr){
        for(uint32_t i = 0; i < exponential_vector.size(); i++) {
            output_array[i] = exponential_vector[i]*1000000.0;
        }
        //exponential_vector.clear();
        return 0;
    }
    else{
        return -1;
    }
}

void DistGen::UniformRandomGen(int lower_bound, int upper_bound, uint32_t seed, uint32_t size){
    srand(seed);
    #ifdef USE_CONSTANT_SEED
        std::mt19937 gen(rand());    // Seeded in main with argument
    #else
        std::random_device rd;
        std::mt19937 gen(rd());
    #endif
    std::uniform_int_distribution<int> uniform_dist(lower_bound, upper_bound);
    uniform_random_vector.reserve(size);
    for(uint32_t i = 0; i < size; i++) {
        uniform_random_vector.push_back(uniform_dist(gen));
    }
}

int DistGen::OutputUniformRandomInMicroseconds(int* output_array){
    if(output_array != nullptr){
        for(uint32_t i = 0; i < uniform_random_vector.size(); i++) {
            output_array[i] = uniform_random_vector[i];
        }
        uniform_random_vector.clear();
        return 0;
    }
    else{
        return -1;
    }
}

