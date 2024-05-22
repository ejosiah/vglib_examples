#include "Integrator.hpp"

#include <utility>
#include "DescriptorSetBuilder.hpp"

Integrator::Integrator(
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

void Integrator::init() {
    _profiler.addQuery("integrator");
    initializeBuffers();
    initDescriptorSetLayout();
    initDescriptorSets();
    init0();
    createPipelines();
}

void Integrator::integrate(VkCommandBuffer commandBuffer) {
    _fixedUpdate([&]{
        _profiler.profile("integrator", commandBuffer, [&]{
            integrate0(commandBuffer);

            Barrier::computeWriteToRead(commandBuffer, { _points } );
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("copy_positions"));
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("copy_positions"), 0, 1, &_attributesSet, 0, VK_NULL_HANDLE);
            vkCmdPushConstants(commandBuffer, layout("copy_positions"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
            vkCmdDispatch(commandBuffer, _cloth->gridSize().x/10, _cloth->gridSize().y/10, 1);


            Barrier::computeWriteToFragmentRead(commandBuffer, { _cloth->buffer() });
        });
    });
}

void Integrator::update(float dt) {
    _fixedUpdate.advance(dt);
    constants.elapsedTime += dt;
}

VulkanDescriptorPool &Integrator::descriptorPool() {
    return *_descriptorPool;
}

void Integrator::onFrameEnd() {
    _profiler.commit();
}

void Integrator::initializeBuffers() {
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
}

void Integrator::initDescriptorSetLayout() {
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

void Integrator::initDescriptorSets() {
    auto sets = _descriptorPool->allocate({ _attributesSetLayout, _geometrySetLayout });
    _attributesSet = sets[0];
    _geometrySet = sets[1];

    device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("integrator_cloth_attributes_set", _attributesSet);

    auto writes = initializers::writeDescriptorSets<3>();
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
    VkDescriptorBufferInfo normalInfo{ _cloth->buffer(), 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &normalInfo;

    writes[2].dstSet = _geometrySet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo geometryInfo{ _geometry->uboBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &geometryInfo;

    device->updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> Integrator::pipelineMetaData() {
    auto meta = pipelineMetaData0();
    meta.push_back({
          "copy_positions",
          R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\spv\copy_positions.comp.spv)",
          { &_attributesSetLayout},
          { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
    });
    return meta;
}
