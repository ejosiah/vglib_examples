#include "gltf/GltfLoader.hpp"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"
#include "ComputePipelins.hpp"

enum class ShaderIndex : int {
    RayGen, Miss, ImplIntersect,
    DiffuseHitImpl, MetalHitImpl, DielectricHitImpl,
    DiffuseHitTri, Count};
//    DiffuseHitTri, MetalHitTri, DielectricHitTri, Count };

enum class RayType : int { Primary, Count };

enum class TextureType : int { TwoD, ThreeD };

struct UniformData {
    glm::mat4 viewInverse{1};
    glm::mat4 projInverse{1};
    glm::vec3 cameraPosition;
    uint frame = 0;
    uint maxBounce = 50;
    uint sampleCount = 10000;
    uint currentSample = 0;
    float apertureSize{0};
    float focalDistance{2};
    int adaptiveSampling{1};
    int blueNoise{0};
    int litBackGround{1};
};

struct DiffuseMaterial {
    glm::vec3 color{1};
    int textureId{-1};

    glm::vec3 emission{0};
    int textureType{to<int>(TextureType::TwoD)};

    float scale{1};
    int useTriplanarMapping{0};
    int padding[2];
};

struct MetalMaterial{
    glm::vec3 color{0.6};
    float roughness{0};
};

struct DielectricMaterial {
    float ior{1.5};
};

struct Scene {
    std::string name;
    struct {
        struct {
            std::vector<imp::Sphere> spheres;
            std::vector<DiffuseMaterial> materials;
            uint32_t hitGroup{};
        } diffuse;
        struct {
            std::vector<imp::Sphere> spheres;
            std::vector<MetalMaterial> materials;
            uint32_t hitGroup{};
        } metal;
        struct {
            std::vector<imp::Sphere> spheres;
            std::vector<DielectricMaterial> materials;
            uint32_t hitGroup{};
        } dielectric;

    } implicits;

    struct {
        struct {
            VulkanDrawable objects;
            std::vector<DiffuseMaterial> materials;
            uint32_t hitGroup{};
        } diffuse;
        struct {
            VulkanDrawable objects;
            std::vector<MetalMaterial> materials;
            uint32_t hitGroup{};
        } metal;
        struct {
            VulkanDrawable objects;
            std::vector<DielectricMaterial> materials;
            uint32_t hitGroup{};
        } dielectric;
    } triangles;
    int litBackGround = 1;
};

class RayTracingWeekendSeries : public VulkanRayTraceBaseApp {
public:
    explicit RayTracingWeekendSeries(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initUniforms();

    void initBindlessDescriptor();

    void createCheckerboardTexture();

    void loadScene();

    void loadDefaultScene();

    void loadPerlinNoiseScene();

    void loadLightScene();

    void loadTextureScene();

    void loadInOneWeekendScene();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void endFrame() override;

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void initCanvas();

    void createRayTracingPipeline();

    void rayTrace(VkCommandBuffer commandBuffer);

    void rayTraceToCanvasBarrier(VkCommandBuffer commandBuffer) const;

    void CanvasToRayTraceBarrier(VkCommandBuffer commandBuffer) const;

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

    void loadTextures();

    void computePerlinNoise();

    uint32_t nextHitGroup();

    std::vector<PipelineMetaData> pipelineMetaData();

    VulkanSampler createNoiseSampler();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VulkanDescriptorSetLayout instanceDescriptorSetLayout;
        VulkanDescriptorSetLayout vertexDescriptorSetLayout;
        VkDescriptorSet descriptorSet;
        VkDescriptorSet instanceDescriptorSet;
        VkDescriptorSet vertexDescriptorSet;
    } raytrace;

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    Canvas canvas{};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    std::map<std::string, VulkanDrawable> drawables;
    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } uniforms;

    struct {
        VulkanBuffer diffuse;
        VulkanBuffer metal;
        VulkanBuffer dielectric;
    } materials;
    struct {
        VulkanBuffer diffuse;
        VulkanBuffer metal;
        VulkanBuffer dielectric;
    } triangleMaterials;
    std::vector<DiffuseMaterial> mattes;
    std::vector<MetalMaterial> metals;
    std::vector<DielectricMaterial> dielectrics;
    VulkanBuffer diffuseSpheres;
    VulkanBuffer diffuseMotion;
    VulkanBuffer metalSpheres;
    VulkanBuffer metalMotion;
    VulkanBuffer dielectricSpheres;
    VulkanBuffer dielectricMotion;
    Texture noise;
    Texture checkerboard;
    Texture earthTexture;
    Texture perlinNoise;
    std::unique_ptr<ComputePipelines> compute;
    std::vector<Scene> scenes;
    int currentScene{1};
    std::vector<const char*> sceneLabels;
    bool sceneUpdated{};
    uint32_t nextInstance{};
    phong::VulkanDrawableInfo info{};
};