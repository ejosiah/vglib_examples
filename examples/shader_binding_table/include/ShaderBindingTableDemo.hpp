#define MAX_IN_FLIGHT_FRAMES 1
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "sbt_domain.hpp"
#include "ComputePipelins.hpp"

class ShaderBindingTableDemo : public VulkanBaseApp{
public:
    enum ShaderType : int {
        RayGen, Miss, Hit, ShaderCount
    };

    explicit ShaderBindingTableDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createHitGroupColorBuffer();

    void loadTeaPot();

    void updateAS();

    void destroyAS();

    void createDisplay();

    void loadPrototypeModel();

    void loadModel(VulkanDrawable& drawable, const glm::mat4& transform = glm::mat4{1});

    void createBLAS(GeomItr start, GeomItr end);

    void createTLAS();

    static std::function<AsGeometryInfo(VulkanDrawable&)> createAsGeometryFactory(const VulkanDevice& device);

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createRayTracingPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderScene(VkCommandBuffer commandBuffer);

    void renderHitGroup(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    int nextOffset();

    void recomputeAllOffsets();

    void rayTrace(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    static int computeHitGroup(int instanceOffset, int geometryIndex, int rayOffset, int rayStride);

    int computeMaxHitGroup() const;

    bool sbtIndexOutOfBounds() const;

protected:

    void ensureAlignmentScratchBufferSize(VkAccelerationStructureBuildSizesInfoKHR& info) const;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR getAccelerationStructureProperties() const;

    ScratchBuffer createScratchBuffer(VkDeviceSize size) const;

    void endFrame() override;

private:
    static constexpr VkBufferUsageFlags rtxUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    static constexpr VkBufferUsageFlags asUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    struct {
        Pipeline main;
        Pipeline hitGroup;
    } render;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
    } raytrace;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    BindlessDescriptor bindlessDescriptor;
    std::vector<Blas> blasData;
    struct {
        Texture texture;
        VkDescriptorSet descriptorSet;
    } display;
    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VulkanBuffer inverseCamProj;
    std::span<glm::mat4> inverseCamera{};
    Tlas tlas;
    VulkanBuffer asInstances;

    VulkanDescriptorSetLayout vertexDescriptorSetLayout;
    VkDescriptorSet vertexDescriptorSet;

    VulkanDescriptorSetLayout hitGroupDescriptorSetLayout;
    VkDescriptorSet hitGroupDescriptorSet;

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    struct Constants {
        uint32_t cullmask{0xFF};
        uint32_t offset{0};
        uint32_t stride{0};
        uint32_t miss{0};
    } constants;

    Constants prevConstants;

    std::vector<InstanceDesc> instanceDescriptions;
    VulkanBuffer instanceGeometryMap;
    mesh::Mesh bunny;
    VulkanDrawable vkBunny;

    mesh::Mesh teapot;
    VulkanDrawable vkTeapot;
    int nextGeometry{1};
    bool instanceUpdated{};
    bool autoComputeOffset{};
    bool addHitGroup{};
    bool removeHitGroup{};
    int numHitGroups{3};
    bool recreateAS{true};
    std::string message;
    struct {
        VulkanBuffer gpu;
        std::span<glm::vec4> cpu;
    } hitGroups;
};