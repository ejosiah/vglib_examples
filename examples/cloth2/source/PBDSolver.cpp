#include "PBDSolver.hpp"

#include <numeric>

PBDSolver::PBDSolver(VulkanDevice &device,
                           VulkanDescriptorPool &descriptorPool,
                           VulkanDescriptorSetLayout accStructDescriptorSetLayout,
                           VkDescriptorSet accStructDescriptorSet,
                           std::shared_ptr<Cloth> cloth,
                           std::shared_ptr<Geometry> geometry,
                           int fps)
        : Solver(device, descriptorPool, std::move(accStructDescriptorSetLayout), accStructDescriptorSet, std::move(cloth), std::move(geometry), fps)
{
    const auto numX = _cloth->numCells().x;
    const auto numY = _cloth->numCells().y;

    passSizes[0] = (numX + 1) * numY/2,
    passSizes[1] = (numX + 1) * numY/2,
    passSizes[2] = numX/2 * (numY + 1),
    passSizes[3] = numX/2 * (numY + 1),
    passSizes[4] = 2 * numX * numY + (numX + 1) * (numY - 1) + (numY + 1) * (numX - 1);
}

void PBDSolver::solve0(VkCommandBuffer commandBuffer) {
    uint32_t numIterations = 1;
    constants.timeStep = _fixedUpdate.period()/static_cast<float>(numIterations);

    for(auto i = 0; i < numIterations; i++) {
        integrate(commandBuffer);
        solveConstraints(commandBuffer);
        updateVelocity(commandBuffer);
    }
}

void PBDSolver::integrate(VkCommandBuffer commandBuffer) {
    const auto gx = uint32_t(_cloth->gridSize().x + wgSize - 1)/wgSize;
    const auto gy = uint32_t(_cloth->gridSize().y + wgSize - 1)/wgSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("integrate"));
    vkCmdPushConstants(commandBuffer, layout("integrate"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("integrate"), 0, 3, sets.data(), 0, nullptr);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void PBDSolver::solveConstraints(VkCommandBuffer commandBuffer) {
    if(combineSolvers){
        int offset{};
        for(auto pass = 0; pass < passSizes.size(); ++pass) {
            constants.numConstraints = passSizes[pass];
            if(independentPass[pass]) {
                solveConstraints(commandBuffer, SolverType::GRAPH_COLOR, offset);
            }else {
                jacobiSolve(commandBuffer, offset);
            }
            offset += constants.numConstraints;
        }
    }else {
        jacobiSolve(commandBuffer);
    }
}

void PBDSolver::jacobiSolve(VkCommandBuffer commandBuffer, int cOffset) {
    clear(commandBuffer, corrections);
    solveConstraints(commandBuffer, SolverType::JACOBI, cOffset);
    addCorrections(commandBuffer);
}

void PBDSolver::solveConstraints(VkCommandBuffer commandBuffer, int solver, int cOffset) {
    const auto gx = (numConstraints + 1023u) / 1024u;

    constants.solveType = solver;
    constants.constraintOffset = cOffset;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("solve_constraints"));
    vkCmdPushConstants(commandBuffer, layout("solve_constraints"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("solve_constraints"), 0, 1, &positionDescriptorSet[0], 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("solve_constraints"), 1, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void PBDSolver::addCorrections(VkCommandBuffer commandBuffer) {
    const auto gx = uint32_t(_cloth->gridSize().x + wgSize - 1)/wgSize;
    const auto gy = uint32_t(_cloth->gridSize().y + wgSize - 1)/wgSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("add_corrections"));
    vkCmdPushConstants(commandBuffer, layout("add_corrections"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("add_corrections"), 0, 1, &positionDescriptorSet[0], 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("add_corrections"), 1, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void PBDSolver::updateVelocity(VkCommandBuffer commandBuffer) {
    const auto gx = uint32_t(_cloth->gridSize().x + wgSize - 1)/wgSize;
    const auto gy = uint32_t(_cloth->gridSize().y + wgSize - 1)/wgSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("update_velocity"));
    vkCmdPushConstants(commandBuffer, layout("update_velocity"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("update_velocity"), 0, 3, sets.data(), 0, nullptr);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void PBDSolver::init0() {
    createBuffers();
    createDescriptorSetLayout();
    updateDescriptorSets();
}

void PBDSolver::createBuffers() {
    initPositions();
    generateConstraints();
    createVelocityBuffer();
    createMassBuffer();
    createCorrectionsBuffer();
}

void PBDSolver::createDescriptorSetLayout() {
    positionSetLayout =
        device->descriptorSetLayoutBuilder()
            .name("pbd_positions_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    descriptorSetLayout =
        device->descriptorSetLayoutBuilder()
            .name("pbd_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void PBDSolver::updateDescriptorSets() {
    auto localSets = _descriptorPool->allocate({ positionSetLayout, positionSetLayout, descriptorSetLayout });
    positionDescriptorSet[0] = localSets[0];
    positionDescriptorSet[1] = localSets[1];
    descriptorSet = localSets[2];

    device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("pbd_positions_0", positionDescriptorSet[0]);
    device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("pbd_positions_1", positionDescriptorSet[1]);


    auto writes = initializers::writeDescriptorSets<7>();

    writes[0].dstSet = positionDescriptorSet[0];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo pointsInfo0{ positions[0], 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &pointsInfo0;


    writes[1].dstSet = positionDescriptorSet[1];
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo pointsInfo1{ positions[1], 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &pointsInfo1;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo constIdsInfo{ constraintIDs, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &constIdsInfo;

    writes[3].dstSet = descriptorSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo restLengthInfo{ restLengths, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &restLengthInfo;

    writes[4].dstSet = descriptorSet;
    writes[4].dstBinding = 2;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo velocityInfo{ velocities, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &velocityInfo;

    writes[5].dstSet = descriptorSet;
    writes[5].dstBinding = 3;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo correctionInfo{ corrections, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &correctionInfo;

    writes[6].dstSet = descriptorSet;
    writes[6].dstBinding = 4;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo massInfo{ invMass, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &massInfo;

    device->updateDescriptorSets(writes);

    sets.push_back(positionDescriptorSet[0]);
    sets.push_back(positionDescriptorSet[1]);
    sets.push_back(descriptorSet);
    sets.push_back(_geometrySet);
    sets.push_back(_accStructDescriptorSet);
}

void PBDSolver::generateConstraints() {
    numConstraints = std::accumulate(passSizes.begin(), passSizes.end(), 0);

    const auto numX = _cloth->numCells().x;
    const auto numY = _cloth->numCells().y;

    const auto distConstIds = compileConstraintIds(numConstraints, numX, numY);

    constants.numConstraints = static_cast<int>(numConstraints);
    constraintIDs = device->createDeviceLocalBuffer(distConstIds.data(), BYTE_SIZE(distConstIds), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    device->setName<VK_OBJECT_TYPE_BUFFER>("constraint_ids", constraintIDs.buffer);
    restLengths = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, numConstraints * sizeof(float), "rest_lengths");

}

std::vector<int> PBDSolver::compileConstraintIds(size_t numConstraints, size_t numX, size_t numY) {
    std::vector<int> distConstIds(2 * numConstraints);
    int i = 0;
    const int strideY = static_cast<int>(numY + 1);
    // Structural constraints (vertical passes)
    for (int passNr = 0; passNr < 2; ++passNr) {
        for (int xi = 0; xi < numX + 1; ++xi) {
            for (int yi = 0; yi < numY / 2; ++yi) {
                distConstIds[2 * i]     = xi * strideY + 2 * yi + passNr;
                distConstIds[2 * i + 1] = xi * strideY + 2 * yi + passNr + 1;
                ++i;
            }
        }
    }

    // Structural constraints (horizontal passes)
    for (int passNr = 0; passNr < 2; ++passNr) {
        for (int xi = 0; xi < numX / 2; ++xi) {
            for (int yi = 0; yi < numY + 1; ++yi) {
                distConstIds[2 * i]     = (2 * xi + passNr) * strideY + yi;
                distConstIds[2 * i + 1] = (2 * xi + passNr + 1) * strideY + yi;
                ++i;
            }
        }
    }

    // Shear constraints (2 per cell)
    for (int xi = 0; xi < numX; ++xi) {
        for (int yi = 0; yi < numY; ++yi) {
            // diagonal (\)
            distConstIds[2 * i]     = xi * strideY + yi;
            distConstIds[2 * i + 1] = (xi + 1) * strideY + yi + 1;
            ++i;

            // diagonal (/)
            distConstIds[2 * i]     = (xi + 1) * strideY + yi;
            distConstIds[2 * i + 1] = xi * strideY + yi + 1;
            ++i;
        }
    }

    // Bending constraints (vertical)
    for (int xi = 0; xi < numX + 1; ++xi) {
        for (int yi = 0; yi < numY - 1; ++yi) {
            distConstIds[2 * i]     = xi * strideY + yi;
            distConstIds[2 * i + 1] = xi * strideY + yi + 2;
            ++i;
        }
    }

    // Bending constraints (horizontal)
    for (int xi = 0; xi < numX - 1; ++xi) {
        for (int yi = 0; yi < numY + 1; ++yi) {
            distConstIds[2 * i]     = xi * strideY + yi;
            distConstIds[2 * i + 1] = (xi + 2) * strideY + yi;
            ++i;
        }
    }

    return distConstIds;
}

void PBDSolver::initPositions() {
    positions[0] = _points;
    positions[1] = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, _points.size);
    device->graphicsCommandPool().oneTimeCommand([this](auto commandBuffer) {
        VkBufferCopy region{0, 0, _points.size};
        vkCmdCopyBuffer(commandBuffer, positions[0], positions[1], 1, &region);
        Barrier::transferWriteToComputeRead(commandBuffer);
    });
}

std::vector<PipelineMetaData> PBDSolver::pipelineMetaData0() {
    return {
            {
                    "compute_rest_lengths",
                    FileManager::resource("pbd_compute_rest_lengths.comp.spv"),
                    { &positionSetLayout, &descriptorSetLayout},
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
            },
            {
                    "integrate",
                    FileManager::resource("pbd_integrate.comp.spv"),
                    { &positionSetLayout, &positionSetLayout, &descriptorSetLayout},
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
            },
            {
                    "solve_constraints",
                    FileManager::resource("pbd_dist_constraints.comp.spv"),
                    { &positionSetLayout, &descriptorSetLayout},
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
            },
            {
                    "add_corrections",
                    FileManager::resource("pbd_add_corrections.comp.spv"),
                    { &positionSetLayout, &descriptorSetLayout},
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
            },
            {
                    "update_velocity",
                    FileManager::resource("pbd_update_velocity.comp.spv"),
                    { &positionSetLayout, &positionSetLayout, &descriptorSetLayout},
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
            },
    };}

void PBDSolver::postInit() {
    computeRestLengths();
    constants.solveType = SolverType::JACOBI;
}

void PBDSolver::computeRestLengths() {
    device->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        std::array<VkDescriptorSet, 2> local_sets{ positionDescriptorSet[0],  descriptorSet };
        auto gx = (numConstraints + 1023)/1024;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("compute_rest_lengths"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("compute_rest_lengths"), 0, COUNT(local_sets), local_sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
    });
}

void PBDSolver::createVelocityBuffer() {
    std::vector<glm::vec3> allocation(_cloth->numPoints());
    velocities = device->createDeviceLocalBuffer(allocation.data(), BYTE_SIZE(allocation), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    device->setName<VK_OBJECT_TYPE_BUFFER>("pbd_velocities", velocities.buffer);
}

void PBDSolver::createCorrectionsBuffer() {
    std::vector<glm::vec3> allocation(_cloth->numPoints());
    corrections = device->createDeviceLocalBuffer(allocation.data(), BYTE_SIZE(allocation), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    device->setName<VK_OBJECT_TYPE_BUFFER>("pbd_corrections", corrections.buffer);
}

void PBDSolver::createMassBuffer() {
    const auto numPoints = _cloth->numPoints();
    const auto width = static_cast<size_t>(_cloth->gridSize().x);

    std::vector<float> allocation(numPoints, 1.0f);
    allocation[numPoints - width] = 0; // pin top left corner;
    allocation[numPoints - 1] = 0; // pin top right corner;

    invMass = device->createDeviceLocalBuffer(allocation.data(), BYTE_SIZE(allocation), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    device->setName<VK_OBJECT_TYPE_BUFFER>("pbd_inverse_mass", invMass.buffer);
}

void PBDSolver::clear(VkCommandBuffer commandBuffer, const VulkanBuffer& buffer) {
    vkCmdFillBuffer(commandBuffer, buffer, 0, buffer.size, 0);
    Barrier::transferWriteToComputeRead(commandBuffer);
}
