#include <cstdio>

int main(){
    int nDevices;

    cudaGetDeviceCount(&nDevices);

    for(auto i = 0; i < nDevices; i++){
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, i);
        printf("Device Number %d\n", i);
        printf("\tDevice name: %s\n", prop.name);
        printf("\tMemory Clock Rate (KHz): %d\n", prop.memoryClockRate);
        printf("\tMemory Bus Width (bits): %d\n", prop.memoryBusWidth);
        printf("\tPeak Memory Bandwidth (GB/s): %f\n", 2.0 * prop.memoryClockRate * (prop.memoryBusWidth/8) / 1.0e6);
        printf("\tMultiGPU board: %s\n", prop.isMultiGpuBoard ? "true" : "false");
        printf("\tNum SM: %d\n", prop.multiProcessorCount);
        printf("\tWarp Size: %d\n", prop.warpSize);
        printf("\tMax Blocks per SM %d\n", prop.maxBlocksPerMultiProcessor);
        printf("\tMax Grid size: [%d, %d, %d]\n", prop.maxGridSize[0], prop.maxGridSize[1], prop.maxGridSize[2]);
        printf("\tMax Threads Per SM %d\n", prop.maxThreadsPerMultiProcessor);
        printf("\tRegisters per block: %d\n", prop.regsPerBlock);
        printf("\tRegisters per SM: %d\n", prop.regsPerMultiprocessor);
        printf("\tReserved shared Memory Per block: %zd (bytes)\n", prop.reservedSharedMemPerBlock);
        printf("\tShared Memory Per block: %zd (bytes)\n", prop.sharedMemPerBlock);
        printf("\tShared Memory per SM: %zd (bytes)\n", prop.sharedMemPerMultiprocessor);
        printf("\n\n");
    }


    return 0;
}