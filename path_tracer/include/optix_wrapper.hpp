#pragma once

#include "optix_common.hpp"
#include <spdlog/spdlog.h>




struct OptixContext{
    OptixContext();

    ~OptixContext();

    CUdevice m_cudaDevice{};
    CUcontext m_cudaCtx{};
    CUstream m_cudaStream{};
    OptixDeviceContext m_optixDevice{};
};

// Create four channel float OptixImage2D with given dimension. Allocate memory on device and
// Copy data from host memory given in hmem to device if hmem is nonzero.
inline OptixImage2D createOptixImage2D( unsigned int width, unsigned int height, const float * hmem = nullptr )
{
    OptixImage2D oi;

    const uint64_t frame_byte_size = width * height * sizeof(float4);
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &oi.data ), frame_byte_size ) );
    if( hmem )
    {
        CUDA_CHECK( cudaMemcpy(
                reinterpret_cast<void*>( oi.data ),
                hmem,
                frame_byte_size,
                cudaMemcpyHostToDevice
        ) );
    }
    oi.width              = width;
    oi.height             = height;
    oi.rowStrideInBytes   = width*sizeof(float4);
    oi.pixelStrideInBytes = sizeof(float4);
    oi.format             = OPTIX_PIXEL_FORMAT_FLOAT4;
    return oi;
}

// Copy OptixImage2D from src to dest.
inline void copyOptixImage2D( OptixImage2D& dest, const OptixImage2D& src )
{
    CUDA_CHECK( cudaMemcpy( (void*)dest.data, (void*)src.data, src.width * src.height * sizeof( float4 ), cudaMemcpyDeviceToDevice ) );
}
