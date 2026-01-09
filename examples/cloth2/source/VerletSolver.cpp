#include "VerletSolver.hpp"
#include "filemanager.hpp"
#include <utility>

VerletSolver::VerletSolver(VulkanDevice &device,
                           VulkanDescriptorPool &descriptorPool,
                           VulkanDescriptorSetLayout accStructDescriptorSetLayout,
                           VkDescriptorSet accStructDescriptorSet,
                           std::shared_ptr<Cloth> cloth,
                           std::shared_ptr<Geometry> geometry,
                           int fps)
: Solver(device, descriptorPool, std::move(accStructDescriptorSetLayout), accStructDescriptorSet, std::move(cloth), std::move(geometry), fps)
{}

void VerletSolver::init0() {
    createBuffers();
    createDescriptorSetLayout();
    updateDescriptorSets();
    sets.push_back(descriptorSet[0]);
    sets.push_back(descriptorSet[1]);
    sets.push_back(_geometrySet);
    sets.push_back(_accStructDescriptorSet);
}

void VerletSolver::solve0(VkCommandBuffer commandBuffer) {
    const auto gx = uint32_t(_cloth->gridSize().x + wgSize - 1)/wgSize;
    const auto gy = uint32_t(_cloth->gridSize().y + wgSize - 1)/wgSize;

    uint32_t numIterations = 1;
    constants.timeStep = _fixedUpdate.period()/static_cast<float>(numIterations);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("verlet_integrator"));
    vkCmdPushConstants(commandBuffer, layout("verlet_integrator"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    for(auto i = 0; i < numIterations; i++) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("verlet_integrator"), 0, COUNT(sets), sets.data(), 0, nullptr);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        Barrier::computeWriteToRead(commandBuffer );
    }
}

void VerletSolver::createDescriptorSetLayout() {
    descriptorSetLayout =
            device->descriptorSetLayoutBuilder()
                    .name("positions_0")
                    .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                    .createLayout();
}

void VerletSolver::createBuffers() {
    auto numPoints = _cloth->vertexCount();
    positions[0] = _points;
    positions[1] = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, numPoints * sizeof(glm::vec4));

    device->graphicsCommandPool().oneTimeCommand([this](auto commandBuffer) {
        VkBufferCopy region{0, 0, _points.size};
        vkCmdCopyBuffer(commandBuffer, positions[0], positions[1], 1, &region);
        Barrier::transferWriteToComputeRead(commandBuffer, { positions[1] });
    });
}


void VerletSolver::updateDescriptorSets() {
    auto sets = _descriptorPool->allocate({ descriptorSetLayout, descriptorSetLayout });
    descriptorSet[0] = sets[0];
    descriptorSet[1] = sets[1];

    device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("verlet_positions_0", descriptorSet[0]);
    device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("verlet_positions_1", descriptorSet[1]);


    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = descriptorSet[0];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo pointsInfo0{ positions[0], 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &pointsInfo0;

    writes[1].dstSet = descriptorSet[1];
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo pointsInfo1{ positions[1], 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &pointsInfo1;


    device->updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> VerletSolver::pipelineMetaData0() {
    return {
            {
                "verlet_integrator",
                FileManager::resource("verlet_integrator.comp.spv"),
                { &descriptorSetLayout, &descriptorSetLayout, &_geometrySetLayout, &_accStructDescriptorSetLayout},
                { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
            }
    };
}
