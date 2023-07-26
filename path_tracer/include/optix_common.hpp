#pragma once

#include <cuda.h>
#include <cuda_runtime.h>

#include <optix.h>
#include <optix_function_table.h>
#include <optix_stubs.h>
#include <optix_types.h>
#include <driver_types.h>

#define OPTIX_CHECK(call)                                                                                              \
  do                                                                                                                   \
  {                                                                                                                    \
    OptixResult res = call;                                                                                            \
    if(res != OPTIX_SUCCESS)                                                                                           \
    {                                                                                                                  \
      auto msg = fmt::format("Optix call ({}) failed with code {} ({} : {})\n", #call, res, __FILE__, __LINE__);      \
      spdlog::error(msg);                                                                                              \
      throw std::runtime_error(msg);                                                                                   \
    }                                                                                                                  \
  } while(false)

#define CUDA_CHECK(call)                                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    cudaError_t error = call;                                                                                          \
    if(error != cudaSuccess)                                                                                           \
    {                                                                                                                  \
      auto msg = fmt::format("CUDA call ({}) failed with code {} ({} : {})\n", #call, cudaGetErrorString( error ), __FILE__, __LINE__);     \
      spdlog::error(msg);                                                                                              \
    }                                                                                                                  \
  } while(false)