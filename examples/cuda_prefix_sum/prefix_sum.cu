#include <cmath>
#include <cassert>
#include <algorithm>
#include <numeric>
#include <vector>
#include <iostream>
#include <iterator>
#include "prefix_sum.h"

#define Banks 32
#define LogBanks 5
#define CONFLICT_FREE_OFFSET(n) (((n) >> LogBanks) + ((n) >> (2*LogBanks)))

constexpr int extraElementsBytes(int n){
    return ((n-1)/Banks) * sizeof(float);
}

// constexpr int extraElementsBytes(int n){
//     return (n/Banks) + ((n/Banks)/Banks) * sizeof(float);
// }


__device__ float prefixSum(float* data){

    float sum = 0;
    
    #pragma unroll
    for(int i = 0; i < 4; i++){
        float temp = data[i];
        data[i] = sum;
        sum += temp;
    }

    return sum;
}

__device__ void add(float* data, float value){

    #pragma unroll
    for(int i = 0; i < 4; i++){
        data[i] += value;
    }
}

__device__ void copy(float* src, float* dest, int srcIdx, int size){
    #pragma unroll 
    for(int i = 0; i < 4; i++){
        int index = srcIdx + i;
        //dest[i] = index < size ? src[index] : 0;
        if(index < size){
        //    printf("copy: index: %d, size: %d\n", index, size);
            dest[i] = src[index];
        }else{
            dest[i] = 0;
        }
    }
}

__device__ void set(float* dest, float* src, int destIdx, int size){
    #pragma unroll
    for(int i = 0; i < 4; i++){
        int index = destIdx + i;
        if(index < size){
          //  printf("set: index: %d, size: %d\n", index, size);
            dest[index] = src[i];
        }
    }
}

// int offset = (blockIdx.x * N);
// int end = offset + N;
// float sum = sums[blockIdx.x];
// for(int idx = threadIdx.x + offset; idx < end; idx += blockDim.x ){
//     out_data[idx] += sum;
// }

__global__ void prefixSumKernel(float* data, float* sum, int N){

    extern __shared__ float s_data[];
    
    int n = max(2, N / 4);
    float temp0[4];
    float temp1[4];
    
    int thid = threadIdx.x;
    
    // __shared__ float temp[8447];
    // int offset = (blockIdx.x * N);
    // int end = offset + N;

    // for(int idx = thid + offset; idx < end; idx += blockDim.x){
    //     temp[idx] = data[idx];
    // }
    
    int ai = 4 * thid + (blockIdx.x * N);
    int bi = ai + N/2;
    
    //int nOffset = (blockIdx.x + 1) * N;
    //  int aiSize = ((blockIdx.x + 1) * N);
    //  int biSize = aiSize + N;
     int aiSize = (blockIdx.x * N) + (N >> 1);
     int biSize = (blockIdx.x * N) + N;

    // if(thid == blockDim.x - 1){
    //     printf("bid: %d, ai: %d, bi: %d, aiSize: %d, biSize: %d, N: %d\n", blockIdx.x, ai, bi, aiSize, biSize, N);
    // }

    copy(data, temp0, ai, aiSize);
    copy(data, temp1, bi, biSize);

    int l_ai = thid;
    int l_bi = thid + n/2;

    l_ai += CONFLICT_FREE_OFFSET(l_ai);
    l_bi += CONFLICT_FREE_OFFSET(l_bi);

    // int ai = 2 *gthid;
    // int bi = 2 * gthid + 1;

    // s_data[2 * thid + 0] = ai < nOffset ? data[ai] : 0;
    // s_data[2 * thid + 1] = bi < nOffset ? data[bi] : 0;
    // s_data[l_ai] = data[ai];
    // s_data[l_bi] = data[bi];

    s_data[l_ai] = prefixSum(temp0);
    s_data[l_bi] = prefixSum(temp1);

    // if(thid == 255){
    //     printf("sum[0] = %d\n", int(s_data[l_ai]));
    //     printf("sum[1] = %d\n", int(s_data[l_bi]));
    // }
    
    int offset = 1;

    

    for(int d = (n >> 1); d > 0; d >>= 1){
        __syncthreads();
        if(thid < d){
            int ai = offset * (2 * thid + 1) - 1;
            int bi = offset * (2 * thid + 2) - 1;

            ai += CONFLICT_FREE_OFFSET(ai);
            bi += CONFLICT_FREE_OFFSET(bi);

            s_data[bi] += s_data[ai];
        }
        offset *= 2;
    }

    if(thid == 0){
        int N_1 = n - 1 + CONFLICT_FREE_OFFSET(n - 1);
        float temp = s_data[N_1];
     //   printf("sum[%d] = %d\n", thid, int(temp));
        s_data[N_1] = 0;
        sum[blockIdx.x] = temp;
    }

    for(int d = 1; d < n; d *= 2){
        offset >>= 1;
        __syncthreads();
        if(thid < d){
            int ai = offset * (2 * thid + 1) - 1;
            int bi = offset * (2 * thid + 2) - 1;

            ai += CONFLICT_FREE_OFFSET(ai);
            bi += CONFLICT_FREE_OFFSET(bi);

            float temp = s_data[bi];
            s_data[bi] = temp + s_data[ai];
            s_data[ai] = temp;
        }
    }

    __syncthreads();
    // if(ai < nOffset) data[ai] = s_data[2 * thid];
    // if(bi < nOffset) data[bi] = s_data[2 * thid + 1];
    // data[ai] = s_data[l_ai];
    // data[bi] = s_data[l_bi];

    float sum0 = s_data[l_ai];
    float sum1 = s_data[l_bi];

    add(temp0, sum0);
    add(temp1, sum1);

    // data[ai] =   temp0[0];
    // data[ai+1] = temp0[1];
    // data[ai+2] = temp0[2];
    // data[ai+3] = temp0[3];


    // data[bi] =   temp1[0];
    // data[bi+1] = temp1[1];
    // data[bi+2] = temp1[2];
    // data[bi+3] = temp1[3];
    set(data, temp0, ai, aiSize);
    set(data, temp1, bi, biSize);
}

__global__ void addKernel(float* out_data, float* sums, int N){
    int offset = (blockIdx.x * N);
    int end = offset + N;
    float sum = sums[blockIdx.x];
    for(int idx = threadIdx.x + offset; idx < end; idx += blockDim.x ){
        out_data[idx] += sum;
    }
}

void prefixSum(float *d_in, float* h_in, int size){
    constexpr int SizePerBlock = 2048;
    constexpr int N = SizePerBlock * 4;
    int numBlocks = std::max(1, size/N);
    constexpr int numThreads = N/8;
    int sharedSize = numThreads * 2;
    int sharedSizeBytes = sharedSize * sizeof(float) + extraElementsBytes(sharedSize);
    int sumSize = numBlocks * sizeof(float);

    float* d_sum = nullptr;
    cudaMalloc(&d_sum, sumSize);
    
    std::cout << "numBlocks: " << numBlocks << "\n";
    printf("numBlocks: %d, numThreads: %d,N: %d, sharedSizeBytes: %d, sumSize: %d\n", numBlocks, numThreads, N, sharedSizeBytes/4, sumSize/4);

    prefixSumKernel<<<numBlocks, numThreads, sharedSizeBytes>>>(d_in, d_sum, N);
    cudaDeviceSynchronize();

    std::vector<float> expected(size);
    std::copy(h_in, h_in + size, std::begin(expected));

    std::vector<float> actual(size);
    cudaMemcpy(&actual[0], d_in, size * sizeof(float), cudaMemcpyDeviceToHost);

    for(auto i = 0; i < numBlocks; i++){
        auto start = std::begin(expected);
        auto finish = std::begin(expected);

        std::advance(start, i * N);
        std::advance(finish, i * N + N);
        std::exclusive_scan(start, finish, start, 0.0f);
    }

    for(int i = 0; i < size; i++){
        if(actual[i] != expected[i]){
            printf("data scan check: index[%d] %d != %d at block: %d\n", i, int(actual[i]), int(expected[i]), i/N);
            assert(actual[i] == expected[i]);
        }
    }

    if(numBlocks > 1){


        float* d_sum1 = nullptr;
        handleError(cudaMalloc(&d_sum1, sizeof(float)));
        int sumNumBlocks = static_cast<int>(std::pow(2, std::ceil(std::log2(numBlocks))));
        int sumNumThreads = sumNumBlocks/2;
        sharedSize = sumNumThreads * 2;
        sharedSizeBytes = sharedSize * sizeof(float) + extraElementsBytes(sharedSize);

        printf("\n\n");
        prefixSumKernel<<<1, sumNumThreads, sharedSizeBytes>>>(d_sum, d_sum1, numBlocks);

        handleError(cudaDeviceSynchronize());
        std::vector<float> sums(numBlocks);
        cudaMemcpy(&sums[0], d_sum, sumSize, cudaMemcpyDeviceToHost);
            
        // printf("\n");
        // for(auto x : sums) printf("%d ", int(x));
        // printf("\n");


        addKernel<<<numBlocks, numThreads>>>(d_in, d_sum, N);

        handleError(cudaDeviceSynchronize());
        handleError(cudaFree(d_sum));
        handleError(cudaFree(d_sum1));
    }
}