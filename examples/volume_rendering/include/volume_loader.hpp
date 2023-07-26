#pragma once

#include "common.h"

struct DataSetHeader{
    int sizeX{};
    int sizeY{};
    int sizeZ{};
    int savedBorderSize{};
    float extentX{};
    float extentY{};
    float extentZ{};
};


inline std::tuple<DataSetHeader, std::vector<float>> load_volume(const std::string& path){
    std::ifstream fin(path.data(), std::ios::binary | std::ios::ate);
    if(!fin.good()) throw std::runtime_error{"Failed to open file:" + path};

    DataSetHeader header{};

    size_t size = fin.tellg();
    fin.seekg(0);
    std::vector<char> buf(4);
    fin.read(buf.data(), BYTE_SIZE(buf));
    std::reverse(buf.begin(), buf.end());

    header.sizeX = *reinterpret_cast<int*>(buf.data());

    fin.read(buf.data(), BYTE_SIZE(buf));
    std::reverse(buf.begin(), buf.end());
    header.sizeY = *reinterpret_cast<int*>(buf.data());

    fin.read(buf.data(), BYTE_SIZE(buf));
    std::reverse(buf.begin(), buf.end());
    header.sizeZ = *reinterpret_cast<int*>(buf.data());

    fin.read(buf.data(), BYTE_SIZE(buf));
    std::reverse(buf.begin(), buf.end());
    header.savedBorderSize = *reinterpret_cast<int*>(buf.data());

    fin.read(buf.data(), BYTE_SIZE(buf));
    std::reverse(buf.begin(), buf.end());
    header.extentX = glm::intBitsToFloat(*reinterpret_cast<int*>(buf.data()));

    fin.read(buf.data(), BYTE_SIZE(buf));
    std::reverse(buf.begin(), buf.end());
    header.extentY = glm::intBitsToFloat(*reinterpret_cast<int*>(buf.data()));

    fin.read(buf.data(), BYTE_SIZE(buf));
    std::reverse(buf.begin(), buf.end());
    header.extentZ = glm::intBitsToFloat(*reinterpret_cast<int*>(buf.data()));

    auto dataSize = size - sizeof(header);
    std::vector<unsigned char> uBuf(dataSize);
    fin.read(reinterpret_cast<char*>(uBuf.data()), dataSize);

    std::vector<float> dataSet;
    dataSet.reserve(dataSize);
    for(int i = 0; i < dataSize; i++){
        auto iValue = uBuf[i];
        auto value = static_cast<float>(iValue)/255.f;
        dataSet.push_back(value);
    }

    spdlog::info("loaded dataSet: {}, size: [{}, {}, {}]", path, header.sizeX, header.sizeY, header.sizeZ);

    fin.close();

    return std::make_tuple(header, dataSet);
}

inline std::tuple<DataSetHeader, std::vector<float>> load_beatle_volume(const std::string& path){
    std::ifstream fin(path.data(), std::ios::binary | std::ios::ate);
    if(!fin.good()) throw std::runtime_error{"Failed to open file:" + path};

    DataSetHeader header{};

    size_t size = fin.tellg();
    fin.seekg(0);

    unsigned short dimSize;
    fin.read(reinterpret_cast<char*>(&dimSize), sizeof(dimSize));
    header.sizeX = dimSize;

    fin.read(reinterpret_cast<char*>(&dimSize), sizeof(dimSize));
    header.sizeY = dimSize;

    fin.read(reinterpret_cast<char*>(&dimSize), sizeof(dimSize));
    header.sizeZ = dimSize;


    auto dataSetSize = header.sizeX * header.sizeY * header.sizeZ;
    std::vector<unsigned short> buf(dataSetSize);
    fin.read(reinterpret_cast<char*>(buf.data()), sizeof(unsigned short ) * dataSetSize);

    std::vector<float> dataSet;
    dataSet.reserve(dataSetSize);

    const auto maxVal = static_cast<float>((1 << 12) - 1); // data is stored in only 12 bits

    for(int i = 0; i < dataSetSize; i++){
        auto value = static_cast<float>(buf[i])/maxVal;
        dataSet.push_back(value);
    }
    spdlog::info("loaded dataSet: {}, size: [{}, {}, {}]", path, header.sizeX, header.sizeY, header.sizeZ);

    fin.close();

    return std::make_tuple(header, dataSet);
}

inline std::tuple<DataSetHeader, std::vector<float>> noise(const VulkanDevice& device, const VulkanDescriptorPool& pool, const FileManager& fileManager){
    DataSetHeader header{ 128, 128, 128, 0, 1, 1, 1};
    
    VkDeviceSize size = 128 * 128 * 128;
    VulkanBuffer buffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, size * sizeof(float));
    
    VulkanDescriptorSetLayout setLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
    
    auto descriptorSet = pool.allocate( {setLayout} ).front();
    
    auto writes = initializers::writeDescriptorSets();
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo bufferInfo{buffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &bufferInfo;

    device.updateDescriptorSets(writes);

    auto module = VulkanShaderModule{ fileManager.getFullPath("noise.comp.spv").value().string(), device};
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    auto layout = device.createPipelineLayout({ setLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = layout;

    auto pipeline = device.createComputePipeline(computeCreateInfo);

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, 16, 16, 16);
    });

    auto start = reinterpret_cast<float*>(buffer.map());
    auto end = start + size;

    std::vector<float> dataSet(start, end);
    buffer.unmap();

    return std::make_tuple(header, dataSet);
}