#pragma once

#include "VulkanBuffer.h"
#include "FileManager.hpp"
#include "primitives.h"
#include "Mesh.h"

#include <glm/glm.hpp>
#include <cinttypes>

struct Geometry {
    enum class Type : int { Sphere = 0, Box, Cow, None };


    VulkanBuffer vertices;
    VulkanBuffer indices;
    VulkanBuffer uboBuffer;
    float radius = 1;
    uint32_t indexCount;

    struct {
        glm::mat4 worldSpaceXform = glm::mat4(1);
        glm::mat4 localSpaceXform = glm::mat4(1);
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } cube;

    struct {
        glm::mat4 worldSpaceXform = glm::mat4(1);
        glm::mat4 localSpaceXform = glm::mat4(1);
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } sphere;

    struct {
        glm::mat4 worldSpaceXform = glm::mat4(1);
        glm::mat4 localSpaceXform = glm::mat4(1);
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } cow;

    struct {
        glm::mat4 worldSpaceXform = glm::mat4(1);
        glm::mat4 localSpaceXform = glm::mat4(1);
        glm::vec3 center;
        float radius;
    } ubo;

    void bindVertexBuffers(VkCommandBuffer commandBuffer) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertices, &offset);
        vkCmdBindIndexBuffer(commandBuffer, indices, 0, VK_INDEX_TYPE_UINT32);
    }

    void initialize(VulkanDevice& device) {
        auto s = primitives::sphere(25, 25, 1.0, glm::mat4(1),  {1, 1, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        sphere.vertices = device.createDeviceLocalBuffer(s.vertices.data(), sizeof(Vertex) * s.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        sphere.indices = device.createDeviceLocalBuffer(s.indices.data(), sizeof(uint32_t) * s.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        auto c = primitives::cube({1, 1, 0, 1});
        cube.vertices = device.createDeviceLocalBuffer(c.vertices.data(), BYTE_SIZE(c.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        cube.indices = device.createDeviceLocalBuffer(c.indices.data(), BYTE_SIZE(c.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        std::vector<mesh::Mesh> meshes;
        mesh::load(meshes, FileManager::instance().getFullPath("cow.ply")->string());
        auto& mesh = meshes.front();
        for(auto& vertex : mesh.vertices){
            vertex.color = glm::vec4(1, 1, 0, 1);
        }
        cow.vertices = device.createDeviceLocalBuffer(mesh.vertices.data(), BYTE_SIZE(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        cow.indices = device.createDeviceLocalBuffer(mesh.indices.data(), BYTE_SIZE(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        uboBuffer = device.createDeviceLocalBuffer(&ubo, sizeof(ubo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        switchTo(_type);
    }
    
    void switchTo(Type type) {
        if(type == Type::Sphere) {
            vertices = sphere.vertices;
            indices = sphere.indices;
        }else if(type == Type::Box) {
            vertices = cube.vertices;
            indices = cube.indices;
        }else if(type == Type::Cow) {
            vertices = cow.vertices;
            indices =  cow.indices;
        }
        indexCount = indices.sizeAs<uint32_t>();
        _type = type;
    }

    Type type() const { return _type; }
    
private:
    Type _type = Type::Box;
};