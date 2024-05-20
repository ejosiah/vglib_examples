#include "VerletIntegrator.hpp"

VerletIntegrator::VerletIntegrator(VulkanDevice &device,
                                   VulkanDescriptorPool &descriptorPool,
                                   std::shared_ptr<Cloth> cloth,
                                   std::shared_ptr<Geometry> geometry,
                                   int fps)
: Integrator(device, descriptorPool, std::move(cloth), std::move(geometry), fps)
, sets(3)
{}

void VerletIntegrator::init0() {
    createBuffers();
    createDescriptorSetLayout();
    updateDescriptorSets();
    sets[0] = descriptorSet[0];
    sets[1] = descriptorSet[1];
    sets[2] = _geometrySet;
}

void VerletIntegrator::integrate0(VkCommandBuffer commandBuffer) {
    uint32_t numIterations = 1;
    constants.timeStep = _fixedUpdate.period()/static_cast<float>(numIterations);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("verlet_integrator"));
    vkCmdPushConstants(commandBuffer, layout("verlet_integrator"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    for(auto i = 0; i < numIterations; i++) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("verlet_integrator"), 0, COUNT(sets), sets.data(), 0, nullptr);
        vkCmdDispatch(commandBuffer, _cloth->gridSize().x/10, _cloth->gridSize().y/10, 1);
        Barrier::computeWriteToRead(commandBuffer, { positions[0], positions[1] } );
 //       std::swap(descriptorSet[0], descriptorSet[1]);
    }
}

void VerletIntegrator::createDescriptorSetLayout() {
    descriptorSetLayout =
            device->descriptorSetLayoutBuilder()
                    .name("positions_0")
                    .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                    .createLayout();
}

void VerletIntegrator::createBuffers() {
    auto numPoints = _cloth->vertexCount();
    positions[0] = _points;
    positions[1] = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, numPoints * sizeof(glm::vec4));

    device->graphicsCommandPool().oneTimeCommand([this](auto commandBuffer) {
        VkBufferCopy region{0, 0, _points.size};
        vkCmdCopyBuffer(commandBuffer, positions[0], positions[1], 1, &region);
        Barrier::transferWriteToComputeRead(commandBuffer, { positions[1] });
    });
}


void VerletIntegrator::updateDescriptorSets() {
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

std::vector<PipelineMetaData> VerletIntegrator::pipelineMetaData0() {
    return {
            {
                "verlet_integrator",
                R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\spv\verlet_integrator.comp.spv)",
                { &descriptorSetLayout, &descriptorSetLayout, &_geometrySetLayout},
                { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
            }
    };
}
