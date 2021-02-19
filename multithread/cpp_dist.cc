#include <cstdio>
#include <string>
#include <map>
#include <cstdlib>
#include <cmath> 

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

void DistGen::BimodalGen(double dist1_prob, uint32_t dist1_value, uint32_t dist2_value, uint32_t size){
    #ifdef USE_CONSTANT_SEED
        std::mt19937 gen(rand());    // Seeded in main with argument
    #else
        std::random_device rd;
        std::mt19937 gen(rd());
    #endif
    std::uniform_int_distribution<int> uniform_dist(1, 100);
    bimodal_vector.reserve(size);
    // e.g. dist1_prob = 0.9, dist1_value= 0.5 unit, dist2_prob = 0.1, dist2_value= 5 units
    int threshold = int( round(dist1_prob*100.0) );
    //printf("dist1_value:%u,dist1_prob:%lf\n", dist1_value, dist1_prob);
    //printf("dist2_value:%u,dist2_prob:%lf\n", dist2_value, 1 - dist1_prob);
    //printf("threshold:%d\n", threshold);

    for(uint32_t i = 0; i < size; i++) {
        if(uniform_dist(gen) > threshold){
            bimodal_vector.push_back(dist2_value);
        }
        else{
            bimodal_vector.push_back(dist1_value);
        }        
    }
}

int DistGen::OutputBimodalInMicroseconds(int* output_array){
    if(output_array != nullptr){
        for(uint32_t i = 0; i < bimodal_vector.size(); i++) {
            output_array[i] = bimodal_vector[i];
            //printf("bimodal:%d\n", output_array[i]);
        }
        bimodal_vector.clear();
        return 0;
    }
    else{
        return -1;
    }

}

