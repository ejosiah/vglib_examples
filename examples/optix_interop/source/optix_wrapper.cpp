#include <optix_function_table_definition.h>
#include "optix_wrapper.hpp"
#include <spdlog/spdlog.h>

static void context_log_cb(unsigned int level, const char* tag, const char* message, void* /*cbdata */)
{
    spdlog::error("[{:2d}][{:12s}]{}", level, tag, message);
}

CUdevice OptixWrapper::cudaDevice = 0;
CUcontext OptixWrapper::cudaCtx = nullptr;
CUstream OptixWrapper::cudaStream = nullptr;
OptixDeviceContext OptixWrapper::optixDevice = nullptr;

void OptixWrapper::init() {
    auto cuRes = cuInit(0);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to initialize cuda");
        throw std::runtime_error{"unable to initialize cuda"};
    }

    cuRes = cuCtxCreate(&cudaCtx, CU_CTX_SCHED_SPIN, cudaDevice);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to initialize cuda context");
        throw std::runtime_error{"unable to initialize cuda context"};
    }

    cuRes = cuStreamCreate(&cudaStream, CU_STREAM_DEFAULT);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to initialize cuda stream");
        throw std::runtime_error{"unable to initialize cuda stream"};
    }

    OPTIX_CHECK(optixInit());
    OPTIX_CHECK(optixDeviceContextCreate(cudaCtx, nullptr, &optixDevice));
    OPTIX_CHECK(optixDeviceContextSetLogCallback(optixDevice, context_log_cb, nullptr, 4));

    spdlog::info("Optix successfully initialized");
}