#pragma once

#include "Texture.h"

#include <glm/glm.hpp>

#include <vector>
#include <optional>

class Offscreen {
public:
    struct ColorAttachment {
        Texture* texture;
        VkFormat format;
        glm::vec4 clearValue{0, 0, 0, 1};
    };

    struct DepthStencilAttachment {
        Texture* texture;
        VkFormat format;
        glm::vec2 clearValue{1, 0};
    };

    struct RenderInfo {
        std::vector<ColorAttachment> colorAttachments;
        std::optional<DepthStencilAttachment> depthAttachment;
        std::optional<DepthStencilAttachment> stencilAttachment;
        glm::uvec2 renderArea{};
        uint32_t numLayers{1};
        uint32_t viewMask{};
    };
    Offscreen() = default;

    void render(VkCommandBuffer commandBuffer, const RenderInfo renderInfo, auto scene) {
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.flags = 0;
        info.renderArea = {{0, 0}, {renderInfo.renderArea.x, renderInfo.renderArea.y}};
        info.layerCount = renderInfo.numLayers;
        info.viewMask = renderInfo.viewMask;

        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        for(auto [texture, format, cv] : renderInfo.colorAttachments) {
            VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
            attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachmentInfo.clearValue.color = {cv.r, cv.g, cv.b, cv.a};
            attachmentInfo.imageView = texture->imageView.handle;

            colorAttachments.push_back(attachmentInfo);
        }

        info.colorAttachmentCount = colorAttachments.size();
        info.pColorAttachments = colorAttachments.data();

        VkRenderingAttachmentInfo depthAttachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        if(renderInfo.depthAttachment.has_value()) {
            auto [texture, format, cv] = *renderInfo.depthAttachment;

            depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
            depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachmentInfo.clearValue.depthStencil = {cv.x, static_cast<uint32_t>(cv.y)};
            depthAttachmentInfo.imageView = texture->imageView.handle;

            info.pDepthAttachment = &depthAttachmentInfo;
        }

        VkRenderingAttachmentInfo stencilAttachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        if(renderInfo.stencilAttachment.has_value()) {
            auto [texture, format, cv] = *renderInfo.stencilAttachment;

            VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            stencilAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
            stencilAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            stencilAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            stencilAttachmentInfo.clearValue.depthStencil = {cv.x, static_cast<uint32_t>(cv.y)};
            stencilAttachmentInfo.imageView = texture->imageView.handle;

            info.pStencilAttachment = &stencilAttachmentInfo;
        }

        vkCmdBeginRendering(commandBuffer, &info);
        scene();
        vkCmdEndRendering(commandBuffer);

    }

};