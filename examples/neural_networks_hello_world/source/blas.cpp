#include "device/blas/blas.h"

#include "Barrier.hpp"
#include "ComputePipelins.hpp"
#include "filemanager.hpp"
#include <glm/glm.hpp>

#include "SkyBox.hpp"

struct BlasContext {
    VulkanDescriptorPool descriptorPool{};
    ComputePipelines compute;
    VulkanDescriptorSetLayout dotDescriptorSetLayout{};
    VulkanDescriptorSetLayout transposeDescriptorSetLayout{};
    VkDescriptorSet dotDescriptorSet{};
    VkDescriptorSet transposeDescriptorSet{};
};

static BlasContext* g;
static VulkanDevice* g_device{};


auto aresource(const std::string& name) {
    const auto res = FileManager::instance().getFullPath(name);
    assert(res.has_value());
    return res->string();
}

void createDescriptorPool() {
    constexpr uint32_t maxSets = 10;
    std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets},
    }};

    g->descriptorPool = g_device->createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void createDescriptorSetLayouts() {
    g->dotDescriptorSetLayout =
        g_device->descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    g->transposeDescriptorSetLayout =
        g_device->descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    auto sets = g->descriptorPool.allocate({ g->dotDescriptorSetLayout, g->transposeDescriptorSetLayout });


    g->dotDescriptorSet = sets[0];
    g->transposeDescriptorSet = sets[1];
}

void updateDotDescriptors(VulkanBuffer a, VulkanBuffer b, VulkanBuffer c) {
    auto writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = g->dotDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo aInfo{ a, 0,  VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &aInfo;

    writes[1].dstSet = g->dotDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo bInfo{ b, 0,  VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &bInfo;

    writes[2].dstSet = g->dotDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo cInfo{ c, 0,  VK_WHOLE_SIZE};
    writes[2].pBufferInfo = &cInfo;

    g_device->updateDescriptorSets(writes);
}

void updateTransposeDescriptors(VulkanBuffer a, VulkanBuffer b) {
    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = g->transposeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo aInfo{ a, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &aInfo;

    writes[1].dstSet = g->transposeDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo bInfo{ b, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &bInfo;

    g_device->updateDescriptorSets(writes);
}

void initComputePipelines() {
    g->compute = ComputePipelines{g_device, {
            {
                "dot_product",
                aresource("dot_product.comp.spv"),
                { &g->dotDescriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 3 } }
            },
            {
                "transpose",
                aresource("transpose.comp.spv"),
                { &g->transposeDescriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 2 } }
            },
            {
                "sigmoid",
                aresource("sigmoid.comp.spv"),
                { &g->transposeDescriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 2 } }
            },
            {
                "sigmoid_prime",
                aresource("sigmoid_prime.comp.spv"),
                { &g->transposeDescriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 2 } }
            },
            {
                "cost_derivative",
                aresource("cost_derivative.comp.spv"),
                { &g->dotDescriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 3 } }
            },
            {
                "add",
                aresource("add.comp.spv"),
                { &g->dotDescriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 3 } }
            }
    }};
    g->compute.createPipelines();
}

void blas::init(VulkanDevice& device) {
    g_device = &device;
    g = new BlasContext();
    createDescriptorPool();
    createDescriptorSetLayouts();
    initComputePipelines();
}

void blas::dot_product(VkCommandBuffer commandBuffer, const matrix &a, const matrix &b, matrix &result) {
    assert(a.shape.j == b.shape.i);

    result.shape.i = a.shape.i;
    result.shape.j = b.shape.j;

    static std::array<glm::uvec2, 3> shapes{};
    shapes[0] = {a.shape.i, a.shape.j};
    shapes[1] = {b.shape.i, b.shape.j};
    shapes[2] = {result.shape.i, result.shape.j};

    updateDotDescriptors(a.buffer, b.buffer, result.buffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.pipeline("dot_product"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.layout("dot_product"), 0, 1, &g->dotDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, g->compute.layout("dot_product"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 3, shapes.data());
    vkCmdDispatch(commandBuffer, 1, result.shape.i, result.shape.j);
    Barrier::computeWriteToHostRead(commandBuffer);
}

void blas::transpose(VkCommandBuffer commandBuffer, const matrix &a, matrix &result) {
    result.shape.i = a.shape.j;
    result.shape.j = a.shape.i;

    static std::array<glm::uvec2, 2> shapes{};
    shapes[0] = {a.shape.i, a.shape.j};
    shapes[1] = {result.shape.i, result.shape.j};

    updateTransposeDescriptors(a.buffer, result.buffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.pipeline("transpose"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.layout("transpose"), 0, 1, &g->transposeDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, g->compute.layout("transpose"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 2, shapes.data());
    const auto gx = nearestMultiple(result.shape.i, 32u) / 32u;
    const auto gy = nearestMultiple(result.shape.j, 32u) / 32u;
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    Barrier::computeWriteToHostRead(commandBuffer);
}

void blas::sigmoid(VkCommandBuffer commandBuffer, const matrix &x, matrix &result) {
    result.shape = x.shape;

    static std::array<glm::uvec2, 2> shapes{};
    shapes[0] = {x.shape.i, x.shape.j};
    shapes[1] = {result.shape.i, result.shape.j};

    updateTransposeDescriptors(x.buffer, result.buffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.pipeline("sigmoid"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.layout("sigmoid"), 0, 1, &g->transposeDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, g->compute.layout("sigmoid"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 2, shapes.data());
    const auto total = result.shape.i * result.shape.j;
    vkCmdDispatch(commandBuffer, nearestMultiple(total, 256u) / 256u, 1, 1);
    Barrier::computeWriteToHostRead(commandBuffer);
}

void blas::sigmoid_prime(VkCommandBuffer commandBuffer, const matrix &x, matrix &result) {
    result.shape = x.shape;

    static std::array<glm::uvec2, 2> shapes{};
    shapes[0] = {x.shape.i, x.shape.j};
    shapes[1] = {result.shape.i, result.shape.j};

    updateTransposeDescriptors(x.buffer, result.buffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.pipeline("sigmoid_prime"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.layout("sigmoid_prime"), 0, 1, &g->transposeDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, g->compute.layout("sigmoid_prime"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 2, shapes.data());
    const auto total = result.shape.i * result.shape.j;
    vkCmdDispatch(commandBuffer, nearestMultiple(total, 256u) / 256u, 1, 1);
    Barrier::computeWriteToHostRead(commandBuffer);
}

void blas::cost_derivative(VkCommandBuffer commandBuffer, const matrix &a, const matrix &y, matrix &result) {
    assert(a.shape.i == y.shape.i);
    assert(a.shape.j == y.shape.j);
    result.shape = a.shape;

    static std::array<glm::uvec2, 3> shapes{};
    shapes[0] = {a.shape.i, a.shape.j};
    shapes[1] = {y.shape.i, y.shape.j};
    shapes[2] = {result.shape.i, result.shape.j};

    updateDotDescriptors(a.buffer, y.buffer, result.buffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.pipeline("cost_derivative"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.layout("cost_derivative"), 0, 1, &g->dotDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, g->compute.layout("cost_derivative"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 3, shapes.data());
    const auto total = result.shape.i * result.shape.j;
    vkCmdDispatch(commandBuffer, nearestMultiple(total, 256u) / 256u, 1, 1);
    Barrier::computeWriteToHostRead(commandBuffer);
}

void blas::add(VkCommandBuffer commandBuffer, const matrix &a, const matrix &b, matrix &result) {
    assert(a.shape.i == b.shape.i);
    assert(a.shape.j == b.shape.j);
    result.shape = a.shape;

    static std::array<glm::uvec2, 3> shapes{};
    shapes[0] = {a.shape.i, a.shape.j};
    shapes[1] = {b.shape.i, b.shape.j};
    shapes[2] = {result.shape.i, result.shape.j};

    updateDotDescriptors(a.buffer, b.buffer, result.buffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.pipeline("add"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, g->compute.layout("add"), 0, 1, &g->dotDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, g->compute.layout("add"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec2) * 3, shapes.data());
    const auto total = result.shape.i * result.shape.j;
    vkCmdDispatch(commandBuffer, nearestMultiple(total, 256u) / 256u, 1, 1);
    Barrier::computeWriteToHostRead(commandBuffer);
}

void blas::shutdown() {
    delete g;
}
