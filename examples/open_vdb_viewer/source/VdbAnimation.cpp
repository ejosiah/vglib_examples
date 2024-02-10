#include "VdbAnimation.hpp"

#include <VulkanInitializers.h>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <openvdb/openvdb.h>
#include <string>


VdbAnimation::VdbAnimation(VulkanDevice *device, int frameRate)
: device_{device}
, framePeriod_{1000/frameRate }
{}

void VdbAnimation::init() {
    createDescriptorPool();
    createDescriptorSetLayout();
}

void VdbAnimation::createDescriptorPool() {
    std::vector<VkDescriptorPoolSize> poolSize{ {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1} };

    descriptorPool_ = device_->createDescriptorPool(1, poolSize
            , VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT );
}

void VdbAnimation::createDescriptorSetLayout() {
    descriptorPool_.reset();

    descriptorSetLayout_ =
        device_->descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();
}

void VdbAnimation::updateDescriptorSet() {
    glm::uvec3 dimensions{bounds_.max - bounds_.min};

    auto size = dimensions.x * dimensions.y * dimensions.z * sizeof(float);
    if(size == 0){
        dimensions = glm::vec3(1);
        size = sizeof(float);
    }
    staging_ = device_->createStagingBuffer(size);
    textures::create(*device_, stagingTexture_, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT,
                     dimensions, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float));

    descriptorSet_ = descriptorPool_.allocate({ descriptorSetLayout_}).front();

    auto writes = initializers::writeDescriptorSets();
    writes[0].dstSet = descriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ stagingTexture_.sampler.handle, stagingTexture_.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &imageInfo;
    
    device_->updateDescriptorSets(writes);
}

void VdbAnimation::load(const std::filesystem::path &vdbPath) {
    loadVolumeFlow_.clear();

    frames_.clear();
    ready_ = false;

    auto A = loadVolumeFlow_.emplace([&, vdbPath](tf::Subflow& subflow) {
        std::vector<tf::Task> tasks;

        fs::path path{vdbPath};
        int numVolumeFrames = 0;
        if(fs::is_directory(path)){

            size_t queueId = 0;
            static std::array<std::vector<fs::path>, 8> loadQueues{};
            for(const auto& entry : fs::directory_iterator{path}) {
                if(numVolumeFrames >= MaxFrames) break;

                auto& queue = loadQueues[queueId];
                queue.push_back(entry.path());
                queueId += (queue.size() / PathsPerQueue);
                numVolumeFrames++;
            }

            frames_.resize(numVolumeFrames);

            size_t offset = 0;
            int taskId = 0;
            for(auto& queue : loadQueues) {
                if(!queue.empty()) {
                    tasks.push_back(subflow.emplace([&, offset, queue, id=taskId]() mutable {
                        spdlog::info("task:{} loading volumes", id);
                        std::vector<Frame> tFrames{};
                        for (auto& path : queue) {
                            auto frame = loadFrame(path);
                            tFrames.push_back(frame);
                        }
                        for(auto i = 0; i < tFrames.size(); i++) {
                            frames_[i + offset] = tFrames[i];
                        }
                        spdlog::info("task:{} loaded {} volumes", id, tFrames.size());

                    }));

                    spdlog::info("offset: {}", offset);
                    offset += queue.size();
                    taskId++;
                }
            }
        }else {
            tasks.push_back(subflow.emplace([&]{
                auto frame = loadFrame(vdbPath);
                frames_.push_back(frame);
            }));
        }

        auto sortTask = subflow.emplace([&](){
            std::sort(frames_.begin(), frames_.end(), [](const auto& a, const auto& b) {
                return a.volume.id < b.volume.id;
            });
        });

        auto resizeVolumesTask = subflow.emplace([&](){
            bounds_.min = glm::vec3{MAX_FLOAT};
            bounds_.max = glm::vec3{MIN_FLOAT};

            for(const auto& frame : frames_) {
                bounds_.min = glm::min(bounds_.min, frame.volume.bounds.min);
                bounds_.max  = glm::max(bounds_.max , frame.volume.bounds.max);
            }

            for(auto& frame : frames_) {
                frame.volume.bounds.min = bounds_.min;
                frame.volume.bounds.max = bounds_.max ;

                if(std::filesystem::is_directory(vdbPath)) {
                    frame.durationMS = framePeriod_;
                }
            }
        });



        for(const auto& task : tasks) {
            sortTask.succeed(task);
        }
        resizeVolumesTask.succeed(sortTask);

    });

    auto B = loadVolumeFlow_.emplace([&]() {
        ready_ = true;
        shouldAdvanceFrame_ = true;
        frameIndex_ = 0;
        framesReady_();
        spdlog::info("frames ready");
    });

    A.precede(B);
    B.succeed(A);

    loadVolumeRequest_ = executor_.run(loadVolumeFlow_);

}

Frame VdbAnimation::loadFrame(fs::path path) {
    static auto to_glm_vec3 = [](openvdb::Vec3i v) {
        return glm::vec3(v.x(), v.y(), v.z());
    };

    auto getId = [](auto str) {
        auto start = str.find_last_of("_") + 1;
        auto id = atoi(str.substr(start, 4).c_str());
        return id;
    };

    try {
        spdlog::debug("loading volume grid from {}", fs::path(path).filename().string());
        openvdb::io::File file(path.string());

        file.open();

        std::vector<std::string> gridNames;
        for(auto itr = file.beginName(); itr != file.endName(); ++itr){
            gridNames.push_back(itr.gridName());
        }
        spdlog::info("{} contains grids: {}", path.string(), gridNames);

        auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(file.beginName().gridName()));
        openvdb::Vec3i boxMin = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();
        openvdb::Vec3i boxMax = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_max")->value();
        int64_t numVoxels = grid->getMetadata<openvdb::Int64Metadata>("file_voxel_count")->value();

        Volume volume{};
        volume.id = getId(path.string());
        auto grids = file.getGrids();
        volume.grids.insert(volume.grids.end(), grids->begin(), grids->end());

        if(numVoxels <= 0) {
            spdlog::info("{} contains no voxels", path.string());
            return {.volume = volume};
        }


        volume.bounds.min = to_glm_vec3(boxMin);
        volume.bounds.max = to_glm_vec3(boxMax);
        file.close();
        spdlog::info("loading volume grid {} successfully loaded", grid->getName());

        return {.volume = volume};
    }catch(...){
        frameLoadFailed_();
        loadVolumeRequest_.cancel();
        return {};
    }
}

void VdbAnimation::setFrameRate(int value) {
    framePeriod_ = 1000/value;
}

const Frame &VdbAnimation::frame(uint32_t index) const {
    return frames_[index];
}

const Frame& VdbAnimation::currentFrame() const {
    return frames_[frameIndex_];
}

bool VdbAnimation::advanceFrame(VkDescriptorSet dstSet, uint32_t dstBinding) const {
    if(frames_.empty() || !shouldAdvanceFrame_) return false;

    prepareFrame(frameIndex_);

    device_->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = stagingTexture_.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{ 0, 0, 0};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = stagingTexture_.spec.extent;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        vkCmdCopyBufferToImage(commandBuffer, staging_.buffer, stagingTexture_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    });

    VkCopyDescriptorSet copySet{ VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET };
    copySet.srcSet = descriptorSet_;
    copySet.srcBinding = 0;
    copySet.dstSet = dstSet;
    copySet.dstBinding = dstBinding;
    copySet.descriptorCount = 1;

    device_->updateDescriptorSets({}, { copySet });
    shouldAdvanceFrame_ = false;

    return true;
}

void VdbAnimation::prepareFrame(uint32_t index) const {
    static auto to_uvw = [](const openvdb::Coord coord, const Bounds& b){
        glm::vec3 d{b.max - b.min};
        d -= 1.0f;

        glm::vec3 x{coord.x(), coord.y(), coord.z()};
        return  glm::ivec3(remap(x, b.min, b.max, glm::vec3(0), d));
    };

    auto stagingArea = reinterpret_cast<float*>(staging_.map());

    std::memset(stagingArea, 0, staging_.size);

    const auto& frame = frames_[index];

    const auto iSize = glm::ivec3(bounds_.max - bounds_.min);

    auto grid = openvdb::gridConstPtrCast<openvdb::FloatGrid>(frame.volume.grids.front());
    int64_t numVoxels = grid->getMetadata<openvdb::Int64Metadata>("file_voxel_count")->value();

    if(numVoxels != 0){
        openvdb::Vec3i boxMin = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();
        openvdb::Vec3i boxMax = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_max")->value();


        openvdb::Coord xyz;
        auto accessor = grid->getAccessor();

        auto &z = xyz.z();
        auto &y = xyz.y();
        auto &x = xyz.x();

        for (z = boxMin.z(); z <= boxMax.z(); z++) {
            for (y = boxMin.y(); y <= boxMax.y(); y++) {
                for (x = boxMin.x(); x <= boxMax.x(); x++) {
                    auto vid = to_uvw(xyz, frame.volume.bounds);
                    int loc = (vid.z * iSize.y + vid.y) * iSize.x + vid.x;
                    stagingArea[loc] = accessor.getValue(xyz);
                }
            }
        }
    }
}

void VdbAnimation::update(float timeSeconds) {
    if(frames_.empty() || !ready_) return;
    auto& currentFrame = frames_[frameIndex_];
    currentFrame.elapsedMS += timeSeconds * 1000;

    if(currentFrame.elapsedMS >= currentFrame.durationMS) {
        currentFrame.elapsedMS = 0;
        frameIndex_++;
        frameIndex_ %= frames_.size();
        shouldAdvanceFrame_ = true;
    }
}

void VdbAnimation::onFramesReady(FramesReady &&framesReady) {
    framesReady_ = framesReady;
}

void VdbAnimation::onFrameLoadFailed(FrameLoadFailed&& framesLoadFailed) {
    frameLoadFailed_ = framesLoadFailed;
}

glm::vec3 VdbAnimation::minBounds() const {
    return currentFrame().volume.bounds.min;
}

glm::vec3 VdbAnimation::maxBounds() const {
    return currentFrame().volume.bounds.max;
}

float VdbAnimation::inverseMaxDensity() const {
    return currentFrame().volume.invMaxDensity;
}