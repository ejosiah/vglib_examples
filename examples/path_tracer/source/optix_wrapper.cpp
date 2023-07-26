#include <optix_function_table_definition.h>
#include "optix_wrapper.hpp"
#include <spdlog/spdlog.h>

static void context_log_cb(unsigned int level, const char* tag, const char* message, void* /*cbdata */)
{
    switch(level){
        case 4:
            spdlog::info("[{:2d}][{:12s}]{}", level, tag, message);
            break;
        case 3:
            spdlog::warn("[{:2d}][{:12s}]{}", level, tag, message);
            break;
        case 2:
            spdlog::error("[{:2d}][{:12s}]{}", level, tag, message);
            break;
        case 1:
            spdlog::critical("[{:2d}][{:12s}]{}", level, tag, message);
            break;
        default:
            break; // log off
    }
}


OptixContext::OptixContext(){
        auto cuRes = cuInit(0);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to initialize cuda");
        throw std::runtime_error{"unable to initialize cuda"};
    }

    cuRes = cuCtxCreate(&m_cudaCtx, CU_CTX_SCHED_SPIN, m_cudaDevice);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to initialize cuda context");
        throw std::runtime_error{"unable to initialize cuda context"};
    }

    cuRes = cuStreamCreate(&m_cudaStream, CU_STREAM_DEFAULT);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to initialize cuda stream");
        throw std::runtime_error{"unable to initialize cuda stream"};
    }

    OPTIX_CHECK(optixInit());
    OPTIX_CHECK(optixDeviceContextCreate(m_cudaCtx, nullptr, &m_optixDevice));
    OPTIX_CHECK(optixDeviceContextSetLogCallback(m_optixDevice, context_log_cb, nullptr, 4));

    spdlog::info("Optix successfully initialized");
}

OptixContext::~OptixContext() {
    optixDeviceContextDestroy( m_optixDevice );

    auto cuRes = cuStreamDestroy(m_cudaStream);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to destroy cuda stream");
    }

    cuRes = cuCtxDestroy_v2(m_cudaCtx);
    if(cuRes != CUDA_SUCCESS){
        spdlog::error("unable to destroy cuda context");
    }
}

