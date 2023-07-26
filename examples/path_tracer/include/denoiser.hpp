#pragma once

#include "common.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "Texture.h"

#include "optix_wrapper.hpp"
#include <optix_denoiser_tiling.h>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>
#include <memory>

class Denoiser{
public:
    struct Data{
        uint32_t width{};
        uint32_t height{};
        float* color{};
        float* albedo{};
        float* normal{};
        std::vector<float*> outputs;
        std::vector<float*> aovs;
        float* flow{};
    };

    struct Settings{
        uint32_t tileWidth{};
        uint32_t tileHeight{};
        bool kernelPredictionMode{};
        bool temporalMode{};
        bool applyFlowMode{};
        bool upscale2XMode{};
        OptixDenoiserAlphaMode alphaMode{OPTIX_DENOISER_ALPHA_MODE_COPY};
    };

    Denoiser(std::shared_ptr<OptixContext> context, const Data& data, const Settings& settings);

    ~Denoiser();

    void exec();

    void getResults();

//    void update(const Data& data);


private:
    std::shared_ptr<OptixContext> m_context{};
    OptixDenoiser m_denoiser{};
    std::vector<float *> m_host_outputs;
    uint32_t m_tileWidth{};
    uint32_t m_tileHeight{};
    uint32_t m_overlap{};
    uint32_t m_scratchSize{};
    CUdeviceptr m_scratch{};
    CUdeviceptr m_state{};
    CUdeviceptr m_intensity{};
    CUdeviceptr m_avgColor{};
    uint32_t m_stateSize;
    std::vector<OptixDenoiserLayer> m_layers;
    OptixDenoiserGuideLayer m_guideLayer{};
    OptixDenoiserParams m_params{};
};

Denoiser::Denoiser(std::shared_ptr<OptixContext> context, const Data &data, const Denoiser::Settings& settings) :
 m_context{std::move(context)}
 {
    ASSERT(data.color)
    ASSERT(data.outputs.size() >= 1)
    ASSERT(data.width != 0)
    ASSERT(data.height != 0)
    ASSERT_MSG(!data.normal || data.albedo, "Currently albedo is required if normal input is given")
    ASSERT_MSG( (settings.tileWidth == 0 && settings.tileHeight == 0 )
                || ( settings.tileWidth > 0 && settings.tileHeight > 0 ),
                "tile size must be > 0 for width and height" )

    uint32_t outScale = 1;
    bool kpMode = settings.kernelPredictionMode;
    if(settings.upscale2XMode){
        kpMode = true;
        outScale = 2;
    }

    m_host_outputs = data.outputs;

    OptixDenoiserOptions options = {};
    options.guideAlbedo = data.albedo ? 1 : 0;
    options.guideNormal = data.normal ? 1 : 0;
    m_tileWidth = settings.tileWidth > 0 ? settings.tileWidth : data.width;
    m_tileHeight = settings.tileHeight > 0 ? settings.tileHeight :  data.height;


    OptixDenoiserModelKind modelKind = OPTIX_DENOISER_MODEL_KIND_HDR;
    OPTIX_CHECK(optixDenoiserCreate(m_context->m_optixDevice, modelKind, &options, &m_denoiser));

    // allocate device memory for denoiser
    OptixDenoiserSizes denoiserSizes;

    OPTIX_CHECK(optixDenoiserComputeMemoryResources(m_denoiser, m_tileWidth, m_tileHeight, &denoiserSizes));

    if(settings.tileWidth == 0){
        m_scratchSize = static_cast<uint32_t>( denoiserSizes.withoutOverlapScratchSizeInBytes );
        m_overlap = 0;
    }else {
        m_scratchSize = static_cast<uint32_t>( denoiserSizes.withOverlapScratchSizeInBytes );
        m_overlap = denoiserSizes.overlapWindowSizeInPixels;
    }

    if(data.aovs.empty() && !kpMode){
        CUDA_CHECK( cudaMalloc(
                reinterpret_cast<void**>( &m_intensity ),
                sizeof(float)
        ) );
    }else{
        CUDA_CHECK( cudaMalloc(
                reinterpret_cast<void**>( &m_avgColor ),
                sizeof(float)
        ) );
    }

    CUDA_CHECK( cudaMalloc(
            reinterpret_cast<void**>( &m_scratch ),
            m_scratchSize
    ) );

    m_stateSize = static_cast<uint32_t>(denoiserSizes.stateSizeInBytes);
    CUDA_CHECK( cudaMalloc(
            reinterpret_cast<void**>( &m_state ),
            m_stateSize
    ) );

    OptixDenoiserLayer layer{};
    layer.input = createOptixImage2D(data.width, data.height, data.color);
    layer.output = createOptixImage2D(data.width, data.height);
    m_layers.push_back(layer);

    if(data.albedo){
        m_guideLayer.albedo = createOptixImage2D(data.width, data.height, data.albedo);
    }
    if(data.normal){
        m_guideLayer.normal = createOptixImage2D(data.width, data.height, data.normal);
    }

    for(auto i = 0u; i < data.aovs.size(); i++){
        layer.input = createOptixImage2D(data.width, data.height, data.aovs[i]);
        layer.output = createOptixImage2D(outScale * data.width, outScale * data.height);
        m_layers.push_back(layer);
    }

    // setup denoiser
    OPTIX_CHECK(optixDenoiserSetup(m_denoiser,
                                   nullptr,
                                   m_tileWidth + 2 * m_overlap,
                                   m_tileHeight + 2 * m_overlap,
                                   m_state,
                                   m_stateSize,
                                   m_scratch,
                                   m_scratchSize));

     m_params.denoiseAlpha    = (OptixDenoiserAlphaMode)settings.alphaMode;
     m_params.hdrIntensity    = m_intensity;
     m_params.hdrAverageColor = m_avgColor;
     m_params.blendFactor     = 0.0f;
     m_params.temporalModeUsePreviousLayers = 0;

     spdlog::info("denoiser successfully initialized");
}

Denoiser::~Denoiser() {
    optixDenoiserDestroy( m_denoiser );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_intensity)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_avgColor)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_scratch)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_state)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_guideLayer.albedo.data)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_guideLayer.normal.data)) );

    for(auto & layer : m_layers){
        CUDA_CHECK( cudaFree(reinterpret_cast<void*>(layer.input.data) ) );
        CUDA_CHECK( cudaFree(reinterpret_cast<void*>(layer.output.data) ) );
        CUDA_CHECK( cudaFree(reinterpret_cast<void*>(layer.previousOutput.data) ) );
    }
    spdlog::info("denoiser disposed");
}

void Denoiser::exec() {
    if(m_intensity){
        OPTIX_CHECK( optixDenoiserComputeIntensity(
                m_denoiser,
                nullptr, // CUDA stream
                &m_layers[0].input,
                m_intensity,
                m_scratch,
                m_scratchSize
        ) );
    }

    if(m_avgColor){
        OPTIX_CHECK( optixDenoiserComputeAverageColor(
                m_denoiser,
                nullptr, // CUDA stream
                &m_layers[0].input,
                m_avgColor,
                m_scratch,
                m_scratchSize
        ) );
    }

#if 0
    OPTIX_CHECK( optixDenoiserInvoke(
            m_denoiser,
            nullptr, // CUDA stream
            &m_params,
            m_state,
            m_stateSize,
            &m_guideLayer,
            m_layers.data(),
            static_cast<unsigned int>( m_layers.size() ),
            0, // input offset X
            0, // input offset y
            m_scratch,
            m_scratchSize
    ) );
#else
    OPTIX_CHECK( optixUtilDenoiserInvokeTiled(
                    m_denoiser,
                    nullptr, // CUDA stream
                    &m_params,
                    m_state,
                    m_stateSize,
                    &m_guideLayer,
                    m_layers.data(),
                    static_cast<unsigned int>( m_layers.size() ),
                    m_scratch,
                    m_scratchSize,
                    m_overlap,
                    m_tileWidth,
                    m_tileHeight
                    ) );
#endif

    cudaDeviceSynchronize();
    cudaError_t error = cudaGetLastError();
    if( error != cudaSuccess )
    {
        auto msg = fmt::format("CUDA error on synchronize with error '{}' ({} : {})", cudaGetErrorString( error ), __FILE__, __LINE__);
        spdlog::error(msg);
        throw std::runtime_error( msg );
    }
    spdlog::info("denoiser successfully executed");
}

void Denoiser::getResults() {
    const uint64_t frame_byte_size = m_layers[0].output.width*m_layers[0].output.height*sizeof(float4);
    for( size_t i=0; i < m_layers.size(); i++ )
    {
        CUDA_CHECK( cudaMemcpy(
                m_host_outputs[i],
                reinterpret_cast<void*>( m_layers[i].output.data ),
                frame_byte_size,
                cudaMemcpyDeviceToHost
        ) );
    }
    spdlog::info("denoiser results transferred to host");
}