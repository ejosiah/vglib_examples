#include "vista/ErosionSimulator.hpp"
#include "Barrier.hpp"

#include <algorithm>
#include <imgui.h>
#include <stdexcept>

namespace {
    void copyTexture(VkCommandBuffer commandBuffer, Texture& src, Texture& dst) {
        if(src.width != dst.width || src.height != dst.height) {
            throw std::invalid_argument{"Texture sizes must match for erosion copy"};
        }

        VkImageSubresourceRange srcSubresourceRange{src.aspectMask, 0, 1, 0, 1};
        VkImageSubresourceRange dstSubresourceRange{dst.aspectMask, 0, 1, 0, 1};
        const auto srcOldLayout = src.image.currentLayout;
        const auto dstOldLayout = dst.image.currentLayout;

        if(srcOldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            src.image.transitionLayout(
                commandBuffer,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                srcSubresourceRange,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT
            );
        }

        if(dstOldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            dst.image.transitionLayout(
                commandBuffer,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                dstSubresourceRange,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT
            );
        }

        if(src.format == dst.format) {
            VkImageCopy region{};
            region.srcSubresource = {src.aspectMask, 0, 0, 1};
            region.srcOffset = {0, 0, 0};
            region.dstSubresource = {dst.aspectMask, 0, 0, 1};
            region.dstOffset = {0, 0, 0};
            region.extent = {src.width, src.height, 1};
            vkCmdCopyImage(commandBuffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }else {
            VkImageBlit region{};
            region.srcSubresource = {src.aspectMask, 0, 0, 1};
            region.srcOffsets[0] = {0, 0, 0};
            region.srcOffsets[1] = {static_cast<int32_t>(src.width), static_cast<int32_t>(src.height), 1};
            region.dstSubresource = {dst.aspectMask, 0, 0, 1};
            region.dstOffsets[0] = {0, 0, 0};
            region.dstOffsets[1] = {static_cast<int32_t>(dst.width), static_cast<int32_t>(dst.height), 1};
            vkCmdBlitImage(commandBuffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
        }

        if(dstOldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            dst.image.transitionLayout(
                commandBuffer,
                dstOldLayout,
                dstSubresourceRange,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
            );
        }

        if(srcOldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            src.image.transitionLayout(
                commandBuffer,
                srcOldLayout,
                srcSubresourceRange,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
            );
        }
    }

    void inputUint(const char* label, uint& value) {
        int current = static_cast<int>(value);
        if(ImGui::InputInt(label, &current)) {
            value = static_cast<uint>(std::max(current, 1));
        }
    }
}

ErosionSimulator::ErosionSimulator(Context &context, glm::uvec2 size)
    : m_context{context}
    , m_size{size}
    , m_constants{
        .terrainSize = size
    }{}

void ErosionSimulator::init() {
    createTextures();
    createComputePipelines();
}

void ErosionSimulator::controls(bool show) {
    if(!show) {
        return;
    }

    ImGui::Begin("Erosion");
    ImGui::SetWindowSize({0, 0});

    inputUint("Iterations", m_constants.maxIterations);

    if(ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Time increment (dt)", &m_constants.timeStep, 0.0f, 0.05f, "%.4f");
        ImGui::SliderFloat("Rain rate (Kr)", &m_constants.rainScale, 0.0f, 0.05f, "%.4f");
        ImGui::SliderFloat("Evaporation rate (Ke)", &m_constants.evaporationRate, 0.0f, 0.05f, "%.4f");
        ImGui::SliderFloat("Pipe area (A)", &m_constants.pipeArea, 0.1f, 60.0f, "%.3f");
        ImGui::SliderFloat("Gravity (g)", &m_constants.gravity, 0.1f, 20.0f, "%.3f");
    }

    if(ImGui::CollapsingHeader("Sediment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Sediment capacity (Kc)", &m_constants.sedimentCapacity, 0.1f, 3.0f, "%.3f");
        ImGui::SliderFloat("Soil suspension (Ks)", &m_constants.soilSuspensionRate, 0.1f, 2.0f, "%.3f");
        ImGui::SliderFloat("Sediment deposition (Kd)", &m_constants.sedimentDepositionRate, 0.1f, 3.0f, "%.3f");
        ImGui::SliderFloat("Sediment softening (Kh)", &m_constants.sedimentSofteningRate, 0.0f, 10.0f, "%.3f");
        ImGui::SliderFloat("Max erosion depth (Kdmax)", &m_constants.maximalErosionDepth, 0.0f, 40.0f, "%.3f");
    }

    if(ImGui::CollapsingHeader("Hardness", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Local hardness", &m_localHardness, 0.5f, 0.95f, "%.3f");
        ImGui::SliderFloat("Minimum hardness (Rmin)", &m_constants.minimumHardness, 0.0f, 1.0f, "%.3f");
    }

    if(ImGui::CollapsingHeader("Thermal", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Thermal erosion (Kt)", &m_constants.thermalErosionRate, 0.0f, 3.0f, "%.3f");
        ImGui::SliderFloat("Talus coeff (Ka)", &m_constants.talusAngleTangentCoeff, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Talus bias (Ki)", &m_constants.talusAngleTangentBias, 0.0f, 1.0f, "%.3f");
    }

    ImGui::Text("Iteration: %u / %u", m_iteration, m_constants.maxIterations);

    if (ImGui::Button(m_running ? "Restart" : "Run")) {
        m_running = true;
        m_restartRequested = true;
    }
    if(m_running) {
        ImGui::SameLine();
        if(ImGui::Button("Stop")) {
            m_running = false;
        }
    }

    ImGui::End();
}

void ErosionSimulator::clear(VkCommandBuffer commandBuffer) {
    static constexpr VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkClearColorValue zero{};
    VkClearColorValue one{};
    VkClearColorValue localHardness{};
    one.float32[0] = 1.0f;
    localHardness.float32[0] = m_localHardness;

    vkCmdClearColorImage(commandBuffer, m_waterHeight.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &subresourceRange);
    vkCmdClearColorImage(commandBuffer, m_sedimentAmount.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &subresourceRange);
    vkCmdClearColorImage(commandBuffer, m_flux.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &subresourceRange);
    vkCmdClearColorImage(commandBuffer, m_velocityField.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &subresourceRange);
    vkCmdClearColorImage(commandBuffer, m_rain.image, VK_IMAGE_LAYOUT_GENERAL, &one, 1, &subresourceRange);
    vkCmdClearColorImage(commandBuffer, m_localHardnessCoef.image, VK_IMAGE_LAYOUT_GENERAL, &localHardness, 1, &subresourceRange);
    vkCmdClearColorImage(commandBuffer, m_worksheet.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &subresourceRange);

    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void ErosionSimulator::update(VkCommandBuffer commandBuffer, Texture& displacementMap) {
    if(!m_terrainHeight.isValid()) {
        throw std::runtime_error{"ErosionSimulator::update called before terrain height texture was created"};
    }
    if(displacementMap.width != m_terrainHeight.width || displacementMap.height != m_terrainHeight.height) {
        throw std::invalid_argument{"Displacement map size must match erosion terrain height texture size"};
    }

    m_displacementMap = &displacementMap;
    copyTexture(commandBuffer, displacementMap, m_terrainHeight);
    m_iteration = 0;
    m_constants.iteration = 0;
    m_restartRequested = false;
}

ErosionSimulator::StepResult ErosionSimulator::step(VkCommandBuffer commandBuffer, Texture& displacementMap) {
    if(!m_running) {
        return StepResult::Idle;
    }

    if(m_restartRequested || m_displacementMap != &displacementMap) {
        update(commandBuffer, displacementMap);
    }

    if(m_iteration >= m_constants.maxIterations) {
        m_running = false;
        return StepResult::Finished;
    }

    runIteration(commandBuffer, m_iteration);

    if(!m_displacementMap) {
        throw std::runtime_error{"ErosionSimulator::run called before update set the displacement map"};
    }
    copyTexture(commandBuffer, m_terrainHeight, *m_displacementMap);

    ++m_iteration;
    m_running = m_iteration < m_constants.maxIterations;
    return m_running ? StepResult::Running : StepResult::Finished;
}

void ErosionSimulator::run(VkCommandBuffer commandBuffer, Texture& displacementMap) {
    update(commandBuffer, displacementMap);

    for(uint iteration = 0; iteration < m_constants.maxIterations; ++iteration) {
        runIteration(commandBuffer, iteration);
    }

    copyTexture(commandBuffer, m_terrainHeight, displacementMap);
    m_iteration = m_constants.maxIterations;
    m_running = false;
}

void ErosionSimulator::runIteration(VkCommandBuffer commandBuffer, uint iteration) {
    m_constants.iteration = iteration;

    if(iteration == 0) {
        clear(commandBuffer);
    }

    applyRain(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    computeOutflowFlux(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    computeWaterHeightChange(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    computeSedimentCapacity(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    erodeDepositSediment(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    advectSediment(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    evaporateWater(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);
}

void ErosionSimulator::applyRain(VkCommandBuffer commandBuffer) {
    dispatch(commandBuffer, "erosion_apply_rain");
}

void ErosionSimulator::computeOutflowFlux(VkCommandBuffer commandBuffer) {
    dispatch(commandBuffer, "erosion_compute_outflow_flux");
}

void ErosionSimulator::computeWaterHeightChange(VkCommandBuffer commandBuffer) {
    dispatch(commandBuffer, "erosion_compute_water_height_change");
}

void ErosionSimulator::computeSedimentCapacity(VkCommandBuffer commandBuffer) {
    dispatch(commandBuffer, "erosion_compute_sediment_capacity");
}

void ErosionSimulator::erodeDepositSediment(VkCommandBuffer commandBuffer) {
    dispatch(commandBuffer, "erosion_erode_deposit_sediment");
}

void ErosionSimulator::advectSediment(VkCommandBuffer commandBuffer) {
    dispatch(commandBuffer, "erosion_advect_sediment");
}

void ErosionSimulator::evaporateWater(VkCommandBuffer commandBuffer) {
    dispatch(commandBuffer, "erosion_evaporate_water");
}

void ErosionSimulator::dispatch(VkCommandBuffer commandBuffer, const char* pipelineName) {
    auto descriptorSet = m_context.bindlessDescriptor->descriptorSet;
    const auto gx = (m_size.x + 31u) / 32u;
    const auto gy = (m_size.y + 31u) / 32u;
    const auto layout = m_compute.layout(pipelineName);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipelineName));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
}

void ErosionSimulator::createTextures() {
    auto& device = *m_context.device;
    auto& bindlessDescriptor = *m_context.bindlessDescriptor;
    if(m_size.x == 0 || m_size.y == 0) {
        throw std::invalid_argument{"ErosionSimulator texture size must be non-zero"};
    }
    const auto size = glm::uvec3{m_size.x, m_size.y, 1u};
    constexpr auto addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    auto createTexture = [&](Texture& texture, VkFormat format, uint& textureIndex, uint& imageIndex) {
        textures::create(device, texture, VK_IMAGE_TYPE_2D, format, size, addressMode);
        texture.image.transitionLayout(
            device.commandPoolFor(*device.findFirstActiveQueue()),
            VK_IMAGE_LAYOUT_GENERAL
        );

        if(textureIndex == ~0u) {
            textureIndex = bindlessDescriptor.reserveTextureSlots(1);
        }
        if(imageIndex == ~0u) {
            imageIndex = bindlessDescriptor.reserveImageSlots(1);
        }

        texture.bindingId = textureIndex;
        bindlessDescriptor.update({ &texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureIndex, VK_IMAGE_LAYOUT_GENERAL });
        bindlessDescriptor.update({ &texture, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, imageIndex, VK_IMAGE_LAYOUT_GENERAL });
    };

    createTexture(m_terrainHeight, VK_FORMAT_R32_SFLOAT, m_constants.terrainHeightTextureIndex, m_constants.terrainHeightImageIndex);
    createTexture(m_waterHeight, VK_FORMAT_R32_SFLOAT, m_constants.waterHeightTextureIndex, m_constants.waterHeightImageIndex);
    createTexture(m_sedimentAmount, VK_FORMAT_R32_SFLOAT, m_constants.sedimentAmountTextureIndex, m_constants.sedimentAmountImageIndex);
    createTexture(m_flux, VK_FORMAT_R32G32B32A32_SFLOAT, m_constants.fluxTextureIndex, m_constants.fluxImageIndex);
    createTexture(m_velocityField, VK_FORMAT_R32G32_SFLOAT, m_constants.velocityFieldTextureIndex, m_constants.velocityFieldImageIndex);
    createTexture(m_rain, VK_FORMAT_R32_SFLOAT, m_constants.rainTextureIndex, m_constants.rainImageIndex);
    createTexture(m_localHardnessCoef, VK_FORMAT_R32_SFLOAT, m_constants.localHardnessCoefTextureIndex, m_constants.localHardnessCoefImageIndex);
    createTexture(m_worksheet, VK_FORMAT_R32G32B32A32_SFLOAT, m_constants.worksheetTextureIndex, m_constants.worksheetImageIndex);
}

void ErosionSimulator::createComputePipelines() {
    m_compute = ComputePipelines{m_context.device, metadata()};
    m_compute.createPipelines();
}

std::vector<PipelineMetaData> ErosionSimulator::metadata() {
    return {
        {
            .name = "erosion_apply_rain",
            .shadePath = FileManager::resource("vista_erosion_apply_rain.comp.spv"),
            .layouts = {const_cast<VulkanDescriptorSetLayout*>(m_context.bindlessDescriptor->descriptorSetLayout)},
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants)}}
        },
        {
            .name = "erosion_compute_outflow_flux",
            .shadePath = FileManager::resource("vista_erosion_compute_outflow_flux.comp.spv"),
            .layouts = {const_cast<VulkanDescriptorSetLayout*>(m_context.bindlessDescriptor->descriptorSetLayout)},
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants)}}
        },
        {
            .name = "erosion_compute_water_height_change",
            .shadePath = FileManager::resource("vista_erosion_compute_water_height_change.comp.spv"),
            .layouts = {const_cast<VulkanDescriptorSetLayout*>(m_context.bindlessDescriptor->descriptorSetLayout)},
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants)}}
        },
        {
            .name = "erosion_compute_sediment_capacity",
            .shadePath = FileManager::resource("vista_erosion_compute_sediment_capacity.comp.spv"),
            .layouts = {const_cast<VulkanDescriptorSetLayout*>(m_context.bindlessDescriptor->descriptorSetLayout)},
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants)}}
        },
        {
            .name = "erosion_erode_deposit_sediment",
            .shadePath = FileManager::resource("vista_erosion_erode_deposit_sediment.comp.spv"),
            .layouts = {const_cast<VulkanDescriptorSetLayout*>(m_context.bindlessDescriptor->descriptorSetLayout)},
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants)}}
        },
        {
            .name = "erosion_advect_sediment",
            .shadePath = FileManager::resource("vista_erosion_advect_sediment.comp.spv"),
            .layouts = {const_cast<VulkanDescriptorSetLayout*>(m_context.bindlessDescriptor->descriptorSetLayout)},
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants)}}
        },
        {
            .name = "erosion_evaporate_water",
            .shadePath = FileManager::resource("vista_erosion_evaporate_water.comp.spv"),
            .layouts = {const_cast<VulkanDescriptorSetLayout*>(m_context.bindlessDescriptor->descriptorSetLayout)},
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants)}}
        }
    };
}
