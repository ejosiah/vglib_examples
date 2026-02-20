#include "CameraInfo.hpp"

CameraInfo::CameraInfo(VulkanDevice &device, VulkanDescriptorPool &descriptorPool, Camera &camera,
                       uint32_t width, uint32_t height, float near, float far)
: m_device{&device}
, m_descriptorPool{&descriptorPool}
, m_camera{&camera}
, m_previous{camera}
, m_defaults{
    .viewportSize =  {width, height},
    .near =  1.0,
    .far =  100
}
{}

void CameraInfo::init() {
    createDescriptorSetLayout();
    createBuffer();
    updateDescriptorSet();
}

void CameraInfo::newFrame() {
    uniforms.cpu->projection = m_camera->proj;
    uniforms.cpu->view = m_camera->view;
    uniforms.cpu->model = m_camera->model;
    uniforms.cpu->inverseView = glm::inverse(m_camera->view);
    uniforms.cpu->inverseProjection = glm::inverse(m_camera->proj);
    uniforms.cpu->inverseViewProjection = glm::inverse(m_camera->proj * m_camera->view);
    uniforms.cpu->previousViewProjection = m_previous.proj * m_previous.view;
    uniforms.cpu->position = position();

}

void CameraInfo::endFrame() {
    m_previous = *m_camera;
}

void CameraInfo::createDescriptorSetLayout() {
    m_descriptorSetLayout =
     m_device->descriptorSetLayoutBuilder()
         .name("camera_info_set_layout")
         .binding(0)
             .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
             .descriptorCount(1)
             .shaderStages(VK_SHADER_STAGE_ALL)
     .createLayout();
}

void CameraInfo::updateDescriptorSet() {
    auto sets = m_descriptorPool->allocate({ m_descriptorSetLayout });
    m_descriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{uniforms.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uniformInfo;

    m_device->updateDescriptorSets(writes);
}

void CameraInfo::createBuffer() {
    uniforms.gpu = m_device->createCpuVisibleBuffer(&m_defaults, sizeof(m_defaults), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = reinterpret_cast<Data*>(uniforms.gpu.map());
}

VulkanDescriptorSetLayout* CameraInfo::descriptorSetLayout() {
    return &m_descriptorSetLayout;
}

const VkDescriptorSet* CameraInfo::descriptorSet() const {
    return &m_descriptorSet;
}

glm::vec3 CameraInfo::position() const {
    return (glm::inverse(m_camera->view) * glm::vec4(0)).xyz();
}
