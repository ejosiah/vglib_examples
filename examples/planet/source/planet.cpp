#include "planet.hpp"
#include "constant_buffers.hpp"
#include <fmt/format.h>

Planet::Planet(VulkanDevice& device, const std::string& name, float planetRadius, const glm::dvec3 &planetCenter, float toggleDistance,
    float triangleSize, uint32_t materialID)
        : m_Device{&device}
        , m_name(name)
        , m_PlanetRadius(planetRadius)
        , m_PlanetCenter(planetCenter)
        , m_ToggleDistance(toggleDistance)
        , m_TriangleSize(triangleSize)
        , m_MaterialID(materialID) {}

void Planet::initialize(const cbt_large::CBT &cbt, const CPUMesh &mesh) {

    m_GeometryCB = m_Device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(GeometryCB), fmt::format("{}_geometry_cb", m_name));
    m_PlanetCB = m_Device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(PlanetCB), fmt::format("{}_planet_cb", m_name));
    m_UpdateCB = m_Device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(UpdateCB), fmt::format("{}_update_cb", m_name));

    initialize_cbt_mesh(mesh, cbt, *m_Device, m_CBTMesh);
    initialize_base_mesh(mesh, *m_Device, m_BaseMesh);
}
