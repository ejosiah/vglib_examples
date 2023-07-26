#pragma once

#include <cassert>
#include <cuda_runtime.h>

constexpr bool debug = true;

constexpr void handleError(cudaError_t err){
    if(err != cudaSuccess){
        printf("cuda error: code: %d, msg: %s", err, cudaGetErrorString(err));
        assert(err == cudaSuccess);
    }
}