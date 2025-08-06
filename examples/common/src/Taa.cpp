#include "taa/Taa.hpp"
#include "Barrier.hpp"
#include "filemanager.hpp"

namespace taa {

    Taa::Taa(VulkanDevice &device,
             VulkanDescriptorPool& descriptorPool,
             BindlessDescriptor &descriptor,
             Texture &colorBuffer,
             Texture &depthBuffer,
             BaseCameraController& camera,
             glm::vec2& jitter,
             const Settings& settings)
             : ComputePipelines(&device),
             m_device{&device},
             m_descriptorPool{&descriptorPool},
             m_bindlessDescriptor{&descriptor},
             m_colorBuffer{&colorBuffer},
             m_depthBuffer{&depthBuffer},
             m_camera{&camera},
             m_jitter{&jitter},
             m_settings{settings}
             {}


    std::vector<PipelineMetaData> Taa::pipelineMetaData() {
        static std::array<uint32_t, 7> specializations{};

        specializations[0] = to<uint32_t>(m_settings.historySamplingFilter);
        specializations[1] = to<uint32_t>(m_settings.subSampleFilter);
        specializations[2] = to<uint32_t>(m_settings.historyConstraint);
        specializations[3] = to<uint32_t>(m_settings.enableTemporalFiltering);
        specializations[4] = to<uint32_t>(m_settings.enableInverseLuminanceFiltering);
        specializations[5] = to<uint32_t>(m_settings.enableLuminanceDifferenceFiltering);
        specializations[6] = to<uint32_t>(!m_settings.fullTaa);

        return {
                {
                    .name = "taa_motion_vector",
                    .shadePath = FileManager::resource("taa_camera_motion.comp.spv"),
                    .layouts = { &m_uniformDescriptorSetLayout, const_cast<VulkanDescriptorSetLayout*>(m_bindlessDescriptor->descriptorSetLayout) },
                },
                {
                    .name = "taa_resolve",
                    .shadePath = FileManager::resource("taa_resolve.comp.spv"),
                    .layouts = { &m_uniformDescriptorSetLayout, const_cast<VulkanDescriptorSetLayout*>(m_bindlessDescriptor->descriptorSetLayout) },
                    .specializationConstants = {
                        .entries = {
                                {0, sizeof(uint32_t) * 0, sizeof(uint32_t)},
                                {1, sizeof(uint32_t) * 1, sizeof(uint32_t)},
                                {2, sizeof(uint32_t) * 2, sizeof(uint32_t)},
                                {3, sizeof(uint32_t) * 3, sizeof(uint32_t)},
                                {4, sizeof(uint32_t) * 4, sizeof(uint32_t)},
                                {5, sizeof(uint32_t) * 5, sizeof(uint32_t)},
                                {6, sizeof(uint32_t) * 6, sizeof(uint32_t)},
                         },
                        .data = specializations.data(),
                        .dataSize = specializations.size() * sizeof(uint32_t)
                    }
                },
        };
    }

    void Taa::newFrame() {
        auto camera = m_camera->cam();
        m_uniforms.cpu->current_view_projection = camera.proj * camera.view;
        m_uniforms.cpu->inverse_current_view_projection = glm::inverse(camera.proj * camera.view);

        auto pCamera = m_camera->previousCamera();
        m_uniforms.cpu->previous_view_projection = pCamera.proj * pCamera.view;
        m_uniforms.cpu->inverse_previous_view_projection = glm::inverse(pCamera.proj * pCamera.view);
        m_uniforms.cpu->jitter_xy = *m_jitter;

        m_bindlessDescriptor->update({ &m_history[m_resolve], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->history_color_texture_index, VK_IMAGE_LAYOUT_GENERAL });
        m_resolve = (++m_resolve % 2);
        m_bindlessDescriptor->update({ &m_history[m_resolve], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_uniforms.cpu->resolve_image_index, VK_IMAGE_LAYOUT_GENERAL });
    }

    void Taa::init() {
        initUniforms();
        initTextures();
        createDescriptorSetLayout();
        createPipelines();
    }

    void Taa::initTextures() {
        const auto width =  m_settings.resolution.x;
        const auto height = m_settings.resolution.y;
        auto format = m_colorBuffer->format;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.flags = 0;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = { width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        m_history[0].image = m_device->createImage(imageInfo);
        m_history[0].imageView = m_history[0].image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
        m_history[0].image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);
        m_device->setName<VK_OBJECT_TYPE_IMAGE>("taa_history_buffer_0", m_history[0].image.image);
        m_device->setName<VK_OBJECT_TYPE_IMAGE_VIEW>("taa_history_buffer_view_0", m_history[0].imageView.handle);

        m_history[1].image = m_device->createImage(imageInfo);
        m_history[1].imageView = m_history[1].image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
        m_history[1].image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);
        m_device->setName<VK_OBJECT_TYPE_IMAGE>("taa_history_buffer_1", m_history[1].image.image);
        m_device->setName<VK_OBJECT_TYPE_IMAGE_VIEW>("taa_history_buffer_view_1", m_history[1].imageView.handle);

        format = VK_FORMAT_R16G16_SFLOAT;
        imageInfo.format = format;
        m_velocity.image = device->createImage(imageInfo);
        m_velocity.imageView = m_velocity.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
        m_velocity.image.transitionLayout(device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);
        m_device->setName<VK_OBJECT_TYPE_IMAGE>("taa_velocity_buffer", m_velocity.image.image);
        m_device->setName<VK_OBJECT_TYPE_IMAGE_VIEW>("taa_velocity_buffer_view", m_velocity.imageView.handle);
        
        updateBindings();
    }

    void Taa::initUniforms() {
        m_uniforms.gpu = device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UniformData), "tta_constants");
        m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
        m_uniforms.cpu->resolution = glm::vec2(m_settings.resolution);
    }
    

    void Taa::createDescriptorSetLayout() {
        m_uniformDescriptorSetLayout = 
            m_device->descriptorSetLayoutBuilder()
                .name("tta_constants_descriptor_set_layout")
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .createLayout();
        
        updateDescriptorSets();
    }

    void Taa::updateDescriptorSets() {
        m_uniformDescriptorSet = m_descriptorPool->allocate({ m_uniformDescriptorSetLayout }).front();
        
        auto writes = initializers::writeDescriptorSets();
        writes[0].dstSet = m_uniformDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        VkDescriptorBufferInfo uniformInfo{ m_uniforms.gpu, 0, VK_WHOLE_SIZE };
        writes[0].pBufferInfo = &uniformInfo;
        
        m_device->updateDescriptorSets(writes);
        
    }

    void Taa::resize(const glm::uvec2 &resolution) {
        m_settings.resolution = resolution;
        initUniforms();
        initTextures();
        updateDescriptorSets();
        createPipelines();
    }

    void Taa::exec(VkCommandBuffer commandBuffer) {
        (*this)(commandBuffer);
    }

    void Taa::operator()(VkCommandBuffer commandBuffer) {
        computeMotionVectors(commandBuffer);
        resolve(commandBuffer);
        copyResolveToColorBuffer(commandBuffer);
    }

    void Taa::computeMotionVectors(VkCommandBuffer commandBuffer) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = m_uniformDescriptorSet;
        sets[1] = m_bindlessDescriptor->descriptorSet;

        glm::uvec3 gc{ (m_settings.resolution.x + 7)/8, (m_settings.resolution.y + 7)/8, 1 };

        static VkClearColorValue clearColor{ 0.f, 0.f, 0.f, 0.f}; // TODO copy from motion vector for dynamic objects
        vkCmdClearColorImage(commandBuffer, m_velocity.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("taa_motion_vector"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("taa_motion_vector"), 0, sets.size(), sets.data(), 0, 0);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);

        Barrier::computeWriteToRead(commandBuffer);
    }

    void Taa::resolve(VkCommandBuffer commandBuffer) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = m_uniformDescriptorSet;
        sets[1] = m_bindlessDescriptor->descriptorSet;

        glm::uvec3 gc{ (m_settings.resolution.x + 7)/8, (m_settings.resolution.y + 7)/8, 1 };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("taa_resolve"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("taa_resolve"), 0, sets.size(), sets.data(), 0, 0);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
        
    }

    void Taa::copyResolveToColorBuffer(VkCommandBuffer commandBuffer) {
        VkImageSubresourceLayers subResource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        static VkImageCopy2 region{ VK_STRUCTURE_TYPE_IMAGE_COPY_2 };
        region.srcSubresource = subResource;
        region.dstSubresource = subResource;
        region.extent = {m_settings.resolution.x, m_settings.resolution.y, 1};

        static VkCopyImageInfo2 copyInfo{ VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2 };
        copyInfo.srcImage = m_history[m_resolve].image;
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstImage = m_colorBuffer->image;
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        Barriers::push(m_history[m_resolve].image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Barriers::push(m_colorBuffer->image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, m_colorBuffer->image.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        Barriers::flush(commandBuffer);

        vkCmdCopyImage2(commandBuffer, &copyInfo);

        Barriers::push(m_history[m_resolve].image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_colorBuffer->image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_colorBuffer->image.currentLayout);
        Barriers::flush(commandBuffer);
    }

    void Taa::updateBindings() {
        if(m_uniforms.cpu->color_buffer_index != ~0u) {
            m_uniforms.cpu->color_buffer_index = m_bindlessDescriptor->update(*m_colorBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            m_uniforms.cpu->depth_buffer_index = m_bindlessDescriptor->update(*m_depthBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            m_uniforms.cpu->velocity_texture_index = m_bindlessDescriptor->update(m_velocity, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
            m_uniforms.cpu->history_color_texture_index = m_bindlessDescriptor->update(m_history[1], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);

            m_uniforms.cpu->velocity_image_index = m_bindlessDescriptor->update(m_velocity, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
            m_uniforms.cpu->resolve_image_index = m_bindlessDescriptor->update(m_history[0], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
        }else {
            m_bindlessDescriptor->update({ m_colorBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->color_buffer_index });
            m_bindlessDescriptor->update({ m_depthBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->depth_buffer_index });
            m_bindlessDescriptor->update({ m_depthBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->velocity_texture_index, VK_IMAGE_LAYOUT_GENERAL });
            m_bindlessDescriptor->update({ &m_history[1], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->history_color_texture_index, VK_IMAGE_LAYOUT_GENERAL });

            m_bindlessDescriptor->update({ &m_velocity, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_uniforms.cpu->velocity_image_index, VK_IMAGE_LAYOUT_GENERAL });
            m_bindlessDescriptor->update({ &m_history[0], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_uniforms.cpu->resolve_image_index, VK_IMAGE_LAYOUT_GENERAL });
        }
        m_resolve = 0;
    }

    void Taa::endFrame() {
        m_uniforms.cpu->previous_jitter_xy = *m_jitter;
    }

    Settings &Taa::settings() {
        return m_settings;
    }
}