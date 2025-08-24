#pragma once

#include "Shared.hpp"
#include "ComputePipelins.hpp"
#include <string>

struct DisplacementMap {
    Texture values;
    Texture normals;
    uint width{};
    uint height{};
};

struct DisplacementMapInfo {
    uint values_tex_id{~0u};
    uint normal_tex_id{~0u};
    uint width{};
    uint height{};
};

enum class DisplacementMethod { File };


class DisplacementMapGenerator {
public:
    DisplacementMapGenerator(Context& context, DisplacementMethod method, uint width, uint height, std::string path = "");

    void init();

    void exec(VkCommandBuffer commandBuffer);

    DisplacementMapInfo displacementMapInfo() const;

protected:
    void createComputePipelines();

    void loadDisplacementMap();

    void computeFileDisplacementMap(VkCommandBuffer commandBuffer);

    void generateNormalMap(VkCommandBuffer commandBuffer);

    std::vector<PipelineMetaData> metadata();

    VkDescriptorSet bindlessDescriptorSet();

    VulkanDescriptorSetLayout& bindlessDescriptorSetLayout();

    BindlessDescriptor& bindlessDescriptor();

    VulkanDevice& device();

private:
    struct FileInfo {
        VulkanBuffer pixels;
        int width;
        int height;
        int channels;
    };

    Context* m_context;
    std::string m_path;
    DisplacementMap m_displacementMap;
    DisplacementMapInfo m_info;
    DisplacementMethod m_method{DisplacementMethod::File};
    ComputePipelines m_compute;
    FileInfo m_fileInfo;
};