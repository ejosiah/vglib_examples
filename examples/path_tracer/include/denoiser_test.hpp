#pragma once

#include "vulkan_context.hpp"
#include "Texture.h"
#include "denoiser.hpp"
#include "vulkan_denoiser.hpp"

#include  <stb_image.h>
#include <stb_image_write.h>
#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfMatrixAttribute.h>
#include <ImfStringAttribute.h>
#include <ImfTiledInputFile.h>
#include <ImfTiledOutputFile.h>
#include <ImfNamespace.h>
#include <ImfRgbaFile.h>

namespace IMF = OPENEXR_IMF_NAMESPACE;
using namespace IMF;
using namespace IMATH_NAMESPACE;

std::vector<float> loadExr(const std::string& path, int& width, int& height){
    RgbaInputFile file{path.data()};
    const RgbaChannels channels = file.channels();
    Box2i dw = file.dataWindow();
    width = dw.max.x - dw.min.x + 1;
    height = dw.max.y - dw.min.y + 1;
    spdlog::info("width: {}, height: {}\n\n", width, height);
    Array2D<Rgba> pixels;
    pixels.resizeErase (height, width);

    file.setFrameBuffer (&pixels[0][0] - dw.min.x - dw.min.y * width, 1, width);
    file.readPixels (dw.min.y, dw.max.y);

    std::vector<float> data;
    data.reserve(width * height * 4);
    for(int i = 0; i < height; i++){
        auto pixel = pixels[i];
        for(int j = 0; j < width; j++){
            data.push_back(pixel[i].r);
            data.push_back(pixel[i].g);
            data.push_back(pixel[i].b);
            data.push_back(pixel[i].a);
        }
    }

    return data;
}

std::vector<float> loadHdr(const std::string& path, int& width, int& height){
    int channels;
    float* start = stbi_loadf(path.c_str(), &width, &height, &channels, 4);
    float* end = start + (width * height * 4);
    std::vector<float> out(start, end);
    stbi_image_free(start);
    return out;
}

void testDenoiser(){
    fs::current_path("C:\\temp");
//    fs::current_path(R"(C:\ProgramData\NVIDIA Corporation\OptiX SDK 7.6.0\SDK\optixDenoiser\data)");

    int width, height;

    auto color = loadHdr("path_traced_image_0.hdr", width, height);
    auto albedo = loadHdr("path_traced_image_albedo_0.hdr", width, height);
    auto normal = loadHdr("path_traced_image_normal_0.hdr", width, height);

//    std::vector<float> color = loadExr("beauty.exr", width, height);
//    std::vector<float> albedo = loadExr("albedo.exr", width, height);
//    std::vector<float> normal = loadExr("normal.exr", width, height);
//    std::vector<float> diffuse = loadExr("diffuse.exr", width, height);
//    std::vector<float> glossy = loadExr("glossy.exr", width, height);
//    std::vector<float> specular = loadExr("specular.exr", width, height);

//    std::vector<float*> aovs{ diffuse.data(), glossy.data(), specular.data()};
    std::vector<float*> aovs;

    std::vector<float> output_beauty(width * height * 4);
//    std::vector<float> output_diffuse(width * height * 4);
//    std::vector<float> output_glossy(width * height * 4);
//    std::vector<float> output_specular(width * height * 4);
    std::vector<float*> outputs{ output_beauty.data()};

    Denoiser::Data data{
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        color.data(),
        albedo.data(),
        normal.data(),
        outputs,
        aovs
    };
    Denoiser::Settings settings{};
    settings.kernelPredictionMode = false;
    try {
        Denoiser denoiser{std::make_shared<OptixContext>(), data, settings};
        denoiser.exec();
        denoiser.getResults();

        stbi_write_hdr("beauty_denoised.hdr", width, height, 4, outputs[0]);

    }catch(const std::exception& e){
        spdlog::error("error executing denoiser: reason: {}", e.what());
        throw e;
    }
}

void testCudaInterop(){
    fs::current_path("C:\\temp");

    ContextCreateInfo info{};
    info.instanceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    info.instanceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);

    VulkanContext ctx{info};
    ctx.init();
    auto& device = ctx.device;

    Texture color, albedo, normal;
    textures::hdr(device, color, "path_traced_image_0.hdr");
    textures::hdr(device, normal, "path_traced_image_normal_0.hdr");
    textures::hdr(device, albedo, "path_traced_image_albedo_0.hdr");

    auto width = color.width;
    auto height = color.height;
    Texture denoisedTexture;
    textures::create(device, denoisedTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                     {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));


    VkDeviceSize size = color.image.size;
    auto bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    auto nBuffer = device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size);
    auto cBuffer = device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size);
    auto aBuffer = device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size);

    cuda::Buffer colorBuffer{ device, cBuffer};
    cuda::Buffer normalBuffer{ device, nBuffer};
    cuda::Buffer albedoBuffer{ device, aBuffer};



    std::vector<cuda::Buffer> outputs;
    outputs.emplace_back(device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size));

    VulkanDenoiser::Data data{
            static_cast<uint32_t>(color.width),
            static_cast<uint32_t>(color.height),
            colorBuffer,
            albedoBuffer,
            normalBuffer,
            outputs,
    };

    try {
        auto semaphore = device.createTimelineSemaphore();
        cuda::Semaphore denoiseSemaphore = cuda::Semaphore{device};
        VulkanDenoiser denoiser{std::make_shared<OptixContext>(), data, {}};
        ctx.device.commandPoolFor(device.findFirstActiveQueue().value()).oneTimeCommand([&](auto cb){
            denoiser.update(cb, color.image, albedo.image, normal.image);
        });
        denoiser.exec();

        ctx.device.commandPoolFor(device.findFirstActiveQueue().value()).oneTimeCommand([&](auto cb){
            denoiser.copyOutputTo(cb, denoisedTexture.image);
        });
        textures::save(device, denoisedTexture, FileFormat::HDR, "output.hdr");
//        stbi_write_hdr("path_traced_image_denoised_0.hdr", color.width, color.height, 4, reinterpret_cast<float*>(outputs[0].buf.map()));

    }catch(const std::exception& e){
        spdlog::error("error executing denoiser: reason: {}", e.what());
        throw e;
    }

}