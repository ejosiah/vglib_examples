#include "Solver.hpp"

#include <utility>
#include "DescriptorSetBuilder.hpp"

Solver::Solver(
        VulkanDevice &device,
        VulkanDescriptorPool &descriptorPool,
        VulkanDescriptorSetLayout accStructDescriptorSetLayout,
        VkDescriptorSet accStructDescriptorSet,
        std::shared_ptr<Cloth> cloth,
        std::shared_ptr<Geometry> geometry,
        int fps
        )
        : ComputePipelines(&device)
        , _descriptorPool(&descriptorPool)
        , _accStructDescriptorSetLayout(accStructDescriptorSetLayout)
        , _accStructDescriptorSet(accStructDescriptorSet)
        , _cloth(std::move(cloth))
        , _fixedUpdate(fps)
        , _profiler()
        , _geometry(geometry)
        , constants{ .inv_cloth_size{ cloth->size()/cloth->gridSize() } }
        {}

void Solver::init() {
    _profiler.addQuery("integrator");
    initializeBuffers();
    initDescriptorSetLayout();
    initDescriptorSets();
    initHashGrid();
    init0();
    createPipelines();
    postInit();
}

void Solver::solve(VkCommandBuffer commandBuffer) {
    _fixedUpdate([&]{
        _profiler.profile("integrator", commandBuffer, [&]{
            solve0(commandBuffer);

            auto gx = uint32_t(_cloth->gridSize().x + wgSize - 1)/wgSize;
            const auto gy = uint32_t(_cloth->gridSize().y + wgSize - 1)/wgSize;

            Barrier::computeWriteToRead(commandBuffer );
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("copy_positions"));
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("copy_positions"), 0, 1, &_attributesSet, 0, VK_NULL_HANDLE);
            vkCmdPushConstants(commandBuffer, layout("copy_positions"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
            vkCmdDispatch(commandBuffer, gx, gy, 1);


            Barrier::computeWriteToFragmentRead(commandBuffer);
        });
    });
}

void Solver::update(float dt) {
    _fixedUpdate.advance(dt);
    constants.elapsedTime += dt;
}

VulkanDescriptorPool &Solver::descriptorPool() {
    return *_descriptorPool;
}

void Solver::onFrameEnd() {
    _profiler.commit();
}

void Solver::initializeBuffers() {
    const auto mesh = _cloth->initialState();
    auto numPoints = mesh.vertices.size();

    std::vector<glm::vec4> vertices{};
    std::vector<glm::vec4> vNormals{};
    vertices.reserve(numPoints);
    vNormals.reserve(numPoints);

    for(auto vertex : mesh.vertices){
        vertices.push_back(vertex.position);
        vNormals.emplace_back( vertex.normal, 0 );
    }
    _points = device->createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    constants.numPoints = numPoints;
}

void Solver::initDescriptorSetLayout() {
    _attributesSetLayout =
        device->descriptorSetLayoutBuilder()
                .name("cloth_attributes")
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
                .binding(3)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    _geometrySetLayout =
        device->descriptorSetLayoutBuilder()
                .name("geometry_descriptor_set_layout")
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
}

void Solver::initDescriptorSets() {
    auto sets = _descriptorPool->allocate({ _attributesSetLayout, _geometrySetLayout });
    _attributesSet = sets[0];
    _geometrySet = sets[1];

    device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("integrator_cloth_attributes_set", _attributesSet);

    auto writes = initializers::writeDescriptorSets<5>();
    writes[0].dstSet = _attributesSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo pointsInfo{ _points, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &pointsInfo;

    writes[1].dstSet = _attributesSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo vertexInfo{ _cloth->buffer(), 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &vertexInfo;

    writes[2].dstSet = _attributesSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo indexInfo{ _cloth->indexes(), 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &indexInfo;

    writes[3].dstSet = _attributesSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo massInfo{ _cloth->invMass(), 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &massInfo;

    writes[4].dstSet = _geometrySet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo geometryInfo{ _geometry->uboBuffer, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &geometryInfo;

    device->updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> Solver::pipelineMetaData() {
    auto meta = pipelineMetaData0();
    meta.push_back({
          "copy_positions",
          FileManager::resource("copy_positions.comp.spv"),
          { &_attributesSetLayout},
          { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
    });
    meta.push_back({
          "update_normals",
          FileManager::resource("update_normals.comp.spv"),
          { &_attributesSetLayout},
          { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
    });
    return meta;
}

void Solver::postInit() {

}

void Solver::initHashGrid() {
    const auto numPoints = _cloth->numPoints();
    _hashGrid.size = numPoints * 5;
    _hashGrid.spacing = glm::max(constants.inv_cloth_size.x, constants.inv_cloth_size.y) * 2.f;

    auto capacity = (_hashGrid.size + 1) * sizeof(uint32_t);
    _hashGrid.counts = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, capacity, "hash_grid_counts");

    capacity = numPoints * sizeof(int);
    _hashGrid.cellIds = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, capacity, "hash_grid_cellIDs");

    _hashGrid.prefixSum = PrefixSum{ device };
    _hashGrid.prefixSum.init();

}
