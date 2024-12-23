#include "CubeFractal.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"

CubeFractal::CubeFractal(const Settings& settings) : VulkanBaseApp("Cube fractal", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("./cube_fractal");
    fileManager().addSearchPathFront("./cube_fractal/data");
    fileManager().addSearchPathFront("./cube_fractal/spv");
    fileManager().addSearchPathFront("./cube_fractal/models");
    fileManager().addSearchPathFront("./cube_fractal/textures");
}

void CubeFractal::initApp() {
    initCamera();
    createCube();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void CubeFractal::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.acceleration = glm::vec3(0.05);
    cameraSettings.velocity = glm::vec3(0.2);
    cameraSettings.zNear = 0.001;

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void CubeFractal::createCube() {
    auto mesh = primitives::cube(randomColor(), glm::scale(glm::mat4{1}, glm::vec3(0.5)));
    cube.vertices = device.createDeviceLocalBuffer(mesh.vertices.data(), BYTE_SIZE(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cube.indices = device.createDeviceLocalBuffer(mesh.indices.data(), BYTE_SIZE(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    std::vector<glm::mat4> xforms;
    xforms.reserve(1 << 20);
    generateFractal(MaxDepth, xforms);
    instanceXforms = device.createCpuVisibleBuffer(xforms.data(), BYTE_SIZE(xforms), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    instanceCount = xforms.size();
    spdlog::info("depth: {}, instanceCount: {}", MaxDepth, instanceCount - 1);

    mesh = primitives::plane(3, 3, 1, 1, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0}), glm::vec4(0.8), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    plane.vertices = device.createDeviceLocalBuffer(mesh.vertices.data(), BYTE_SIZE(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    plane.indices = device.createDeviceLocalBuffer(mesh.indices.data(), BYTE_SIZE(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

struct Pose {
    glm::mat3 basis{1};
    glm::vec3 position{0};
};

void CubeFractal::generateFractal(int maxDepth, std::vector<glm::mat4>& xforms) {
    static constexpr float OneThird = 1.f/3.f;
    static constexpr uint32_t MiddleCell = 4;
    static constexpr auto _90 = glm::half_pi<float>();
    glm::vec3 N{0, 1, 0};


    std::array<glm::vec3, 9> baseGrid {{
           {-OneThird, -0.5, -OneThird}, {0, -0.5, -OneThird}, {OneThird, -0.5, -OneThird},
           {-OneThird, -0.5,         0}, {0, -0.5,         0}, {OneThird, -0.5,         0},
           {-OneThird, -0.5,  OneThird}, {0, -0.5,  OneThird}, {OneThird, -0.5, OneThird}
    }};

    std::array<std::tuple<int, float>, 5> rotations{};

    rotations[0] = std::make_tuple(0, 1);
    rotations[1] = std::make_tuple(0, -1);
    rotations[2] = std::make_tuple(1, 1);
    rotations[3] = std::make_tuple(2, 1);
    rotations[4] = std::make_tuple(2, -1);

    xforms.emplace_back(1);

    std::function<void(int, const Pose&, float)> loop = [&](int depth, const Pose& pose, float size) {
        if(depth >= maxDepth) return;

        // move to face
        auto normal = pose.basis[1];
        auto center = pose.position + normal * size * .5f;
        auto scale = glm::scale(glm::mat4{1}, glm::vec3(size));
        auto translate = glm::translate(glm::mat4{1}, center);

        auto grid = baseGrid;
        for(auto& cell : grid) {
            cell = translate * scale * glm::vec4(pose.basis * cell, 1);
        }

        for(auto i = 0; i < 9; i++) {
            auto cell =  grid[i];
            auto nSize = size * OneThird;
            Pose cPose = pose;
            if(i == MiddleCell){
                scale = glm::scale(glm::mat4{1}, glm::vec3(nSize));
                translate = glm::translate(glm::mat4{1}, cell + normal * (size + nSize) * .5f);
                auto xform = translate * scale;
                xforms.push_back(xform);
                for(auto [axis, direction] : rotations) {
                    auto rotation = glm::rotate(glm::mat4{1}, direction * _90, pose.basis[axis]);
                    cPose.basis = glm::mat3(rotation) * pose.basis;
                    cPose.position = (xform * glm::vec4(0, 0, 0, 1)).xyz();
                    loop(depth + 1, cPose, nSize);
                }
            }else {
                cPose.position =  cell + normal * nSize;
                loop(depth + 1, cPose, nSize);
            }
        }


    };

    Pose pose{.position = {0, -.5, 0}};
    loop(0, pose, 1.f);
}


void CubeFractal::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 12> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 100 * maxSets },
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void CubeFractal::createDescriptorSetLayouts() {
    setLayout =
        device.descriptorSetLayoutBuilder()
            .name("main_descriptor_set")
            .binding(0)
            .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_VERTEX_BIT)
        .createLayout();
}

void CubeFractal::updateDescriptorSets(){
    auto writes = initializers::writeDescriptorSets<1>();
    
    descriptorSet = descriptorPool.allocate({ setLayout }).front();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo instanceInfo{ instanceXforms, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &instanceInfo;

    device.updateDescriptorSets(writes);
}

void CubeFractal::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void CubeFractal::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void CubeFractal::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .rasterizationState()
                .shaderStage()
                    .vertexShader(resource("geom.vert.spv"))
                    .fragmentShader(resource("geom.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(setLayout)
                .name("render")
                .build(render.layout);
    //    @formatter:on
}


void CubeFractal::onSwapChainDispose() {
    dispose(render.pipeline);
}

void CubeFractal::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *CubeFractal::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkDeviceSize offset{};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, 0);
    camera->push(commandBuffer, render.layout);

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cube.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, cube.indices.sizeAs<uint32_t>(), instanceCount, 0, 0, 1);

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, plane.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, plane.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, plane.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);


    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void CubeFractal::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void CubeFractal::checkAppInputs() {
    camera->processInput();
}

void CubeFractal::cleanup() {
    VulkanBaseApp::cleanup();
}

void CubeFractal::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.depthTest = true;

        auto app = CubeFractal{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}