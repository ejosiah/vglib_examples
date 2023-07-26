#include <cassert>
#include <cstdio>
#include <random>
#include <algorithm>
#include <vector>
#include <numeric>
#include "common.h"
#include "prefix_sum.h"


void ntimes(int n){
    auto rng = []{
        static std::default_random_engine engine{ 123456789 };
        static std::uniform_int_distribution<int> dist{0, 20};

        return static_cast<float>( dist(engine)) ;
    };

    for(int i = 0; i < n; i++){
        int size = 1 << (i + 20);
        int byteSize = size * sizeof(float);
        std::vector<float> data(size);
        std::generate(begin(data), end(data), [&]{ return rng(); });

        for(int i = 1020; i < 1024; i++){
            printf("%d ", int(data[i]));
        }
        printf("\n");
        for(int i = 2044; i < 2048; i++){
            printf("%d ", int(data[i]));
        }
        printf("\n");

        float* deviceData;

        handleError(cudaMalloc(&deviceData, byteSize));

        handleError(cudaMemcpy(deviceData, &data[0], byteSize, cudaMemcpyHostToDevice));



        handleError(cudaDeviceSynchronize());

        std::vector<float> expected(size);
        std::copy(begin(data), end(data), begin(expected));
        std::exclusive_scan(begin(expected), end(expected), begin(expected), 0.0f);

        auto start = begin(data);
        auto finish = begin(data);
        std::advance(finish, data.size()/2);
        //auto sum = std::accumulate(start, end(data), 0.0f);
        auto sum = std::accumulate(start, finish, 0.0f);
        printf("expected sum1: %d\n", int(sum));
        start = finish;
        finish = end(data);
        sum = std::accumulate(start, finish, 0.0f);
         printf("expected sum2: %d\n", int(sum));

        printf("running prefixSum, size: %d\n", size);
        prefixSum(deviceData, &data[0], size);


        std::vector<float> actual(size);
        handleError(cudaMemcpy(&actual[0], deviceData, byteSize, cudaMemcpyDeviceToHost));
        
        auto isEqual = actual == expected;

        if(!isEqual){
            
             
            int n = size / 32;

            // for(int j = 0; j < n; j++){
            //     int offset = j * 32;

            //     printf("offset: %d\nexpected: ", offset);
            //     for(int i = j * 32; i < offset + 32; i++) printf("%d ", int(expected[i]));
                
            //     printf("\nactual:   ");
            //     for(int i = j * 32; i < offset + 32; i++) printf("%d ", int(actual[i]));

            //     printf("\nmismatch: ");
            //     for(int i = j * 32; i < offset + 32; i++) {
            //         if(expected[i] != actual[i]){
            //             printf("%d != %d at index: %d ", int(expected[i]), int(actual[i]), i);
            //         }
            //     }
            //     printf("\n\n");
            // }

            // for(int i = 0; i < size; i++){
            //     if(expected[i] != actual[i]){
            //         printf("%d != %d at index: %d\n", int(actual[i]), int(expected[i]), i);
            //     }
            // }

            assert(isEqual);
            printf("\n");
        }

        handleError(cudaFree(deviceData));
    }
}

int main(){

    ntimes(1);

    return 0;
}