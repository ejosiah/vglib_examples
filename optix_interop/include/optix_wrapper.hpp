#pragma once

#include <cuda.h>
#include <cuda_runtime.h>

#include <optix.h>
#include <optix_function_table.h>
#include <optix_stubs.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>

#define OPTIX_CHECK(call)                                                                                              \
  do                                                                                                                   \
  {                                                                                                                    \
    OptixResult res = call;                                                                                            \
    if(res != OPTIX_SUCCESS)                                                                                           \
    {                                                                                                                  \
      std::stringstream ss;                                                                                            \
      ss << "Optix call (" << #call << " ) failed with code " << res << " (" __FILE__ << ":" << __LINE__ << ")\n";     \
      std::cerr << ss.str().c_str() << std::endl;                                                                      \
      throw std::runtime_error(ss.str().c_str());                                                                      \
    }                                                                                                                  \
  } while(false)

#define CUDA_CHECK(call)                                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    cudaError_t error = call;                                                                                          \
    if(error != cudaSuccess)                                                                                           \
    {                                                                                                                  \
      std::stringstream ss;                                                                                            \
      ss << "CUDA call (" << #call << " ) failed with code " << error << " (" __FILE__ << ":" << __LINE__ << ")\n";    \
      throw std::runtime_error(ss.str().c_str());                                                                      \
    }                                                                                                                  \
  } while(false)


class OptixWrapper{
public:
   static void init();

    static CUdevice cudaDevice;
    static CUcontext cudaCtx;
    static CUstream cudaStream;
    static OptixDeviceContext optixDevice;
};