#include "AtmosphereModel.hpp"

AtmosphereModel::AtmosphereModel(Context &context)
: m_context{&context} {

}

void AtmosphereModel::init() {
    createLoopUpTextures();
    createComputePipelines();
    createRenderPipelines();

    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        preProcess(commandBuffer);
    });
}

void AtmosphereModel::preProcess(VkCommandBuffer commandBuffer) {

}

void AtmosphereModel::render(VkCommandBuffer commandBuffer) {

}

void AtmosphereModel::controls() {

}

Context &AtmosphereModel::context() {
    return *m_context;
}

void AtmosphereModel::createLoopUpTextures() {
    textures::create(device(), m_lut.transmittance, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {256, 64, 1});
    textures::create(device(), m_lut.multiScattering, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {32, 32, 1});
    textures::create(device(), m_lut.skyView, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {192, 128, 1});
    textures::create(device(), m_lut.arealPerspective, VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16B16A16_SFLOAT, {32, 32, 32});

    bindlessDescriptor().update({ &m_lut.transmittance, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().transmittanceTextureIndex });
    bindlessDescriptor().update({ &m_lut.multiScattering, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().multiScatteringTextureIndex });
    bindlessDescriptor().update({ &m_lut.skyView, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().skyViewTextureIndex });
    bindlessDescriptor().update({ &m_lut.arealPerspective, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().arealPerspectiveTextureIndex });
}

void AtmosphereModel::createComputePipelines() {

}

void AtmosphereModel::createRenderPipelines() {

}
