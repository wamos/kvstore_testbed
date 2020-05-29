#include <cstdio>
#include <iostream>
#include <string>
#include <map>
//#include <random>

#include "cpp_dist.h"

//ref: https://stackoverflow.com/questions/2744181/how-to-call-c-function-from-c

DistGen::DistGen(){    
}

DistGen::~DistGen(){    
}

void DistGen::ExponentialGen(double rate, uint32_t size){
    std::random_device rd;
    std::mt19937 gen(rd());
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
        return 0;
    }
    else{
        return -1;
    }
}

