#include "Volume.hpp"

Volume::Type valueOf(openvdb::GridClass gridClass) {
    if(gridClass == openvdb::v12_0::GRID_LEVEL_SET) return Volume::Type::LEVEL_SET;
    if(gridClass == openvdb::v12_0::GRID_FOG_VOLUME) return Volume::Type::FOG;
    throw std::runtime_error{"unsupported volume type"};
}

Volume Volume::loadFromVdb(const std::filesystem::path& path) {
    openvdb::io::File file(path.string());
    file.open();

    auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(file.beginName().gridName()));
    auto numVoxels = grid->activeVoxelCount();
    auto bounds = grid->evalActiveVoxelBoundingBox();
    auto dim = grid->evalActiveVoxelDim();
    auto bMin = grid->indexToWorld(bounds.min());
    auto bMax = grid->indexToWorld(bounds.max());
    auto iBoxMin = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();

    auto volume = Volume{};
    volume.dim = {dim.x(), dim.y(), dim.z() };
    volume.bounds.min = { bMin.x(), bMin.y(), bMin.z() };
    volume.bounds.max = { bMax.x(), bMax.y(), bMax.z() };
    volume.voxelSize = grid->voxelSize().x();
    volume.numVoxels = numVoxels;
    volume.type = valueOf(grid->getGridClass());
    volume.data = std::vector<float>(dim.x() * dim.y() * dim.z(), grid->background());

    auto value = grid->beginValueOn();
    do {
        auto coord = value.getCoord();
        auto x = coord.x() + std::abs(iBoxMin.x());
        auto y = coord.y() + std::abs(iBoxMin.y());
        auto z = coord.z() + std::abs(iBoxMin.z());

        auto index = (z * dim.y() + y) * dim.x() + x;
        volume.data[index] = *value;
    } while (value.next());

    file.close();

    auto invMaxAxis = 1.f/(volume.bounds.max - volume.bounds.min);
    auto model = glm::mat4{1};

    model = glm::scale(model, invMaxAxis);
    model = glm::translate(model, -volume.bounds.min);

    volume.worldToVoxelTransform = model;
    volume.voxelToWordTransform = glm::inverse(model);

    const auto tmin = (volume.worldToVoxelTransform * glm::vec4(volume.bounds.min, 1)).xyz();
    const auto tmax = (volume.worldToVoxelTransform * glm::vec4(volume.bounds.max, 1)).xyz();

    assert(glm::all(glm::epsilonEqual(tmin, glm::vec3(0), 0.0001f)));
    assert(glm::all(glm::epsilonEqual(tmax, glm::vec3(1), 0.0001f)));

    return volume;
}