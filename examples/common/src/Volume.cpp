#include "Volume.hpp"

Volume::Type valueOf(openvdb::GridClass gridClass) {
    if(gridClass == openvdb::v12_0::GRID_LEVEL_SET) return Volume::Type::LEVEL_SET;
    return Volume::Type::FOG;
}


Volume load(auto grid) {
    auto volume = Volume{};
    volume.numVoxels = grid->activeVoxelCount();

    if(volume.numVoxels == 0) {
        volume.data.push_back(0);
        volume.dim = glm::vec3(1);
        return volume;
    }

    auto bounds = grid->evalActiveVoxelBoundingBox();
    auto dim = grid->evalActiveVoxelDim();
    auto bMin = grid->indexToWorld(bounds.min());
    auto bMax = grid->indexToWorld(bounds.max());
    auto iBoxMin = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();

    volume.dim = {dim.x(), dim.y(), dim.z() };
    volume.bounds.min = { bMin.x(), bMin.y(), bMin.z() };
    volume.bounds.max = { bMax.x(), bMax.y(), bMax.z() };
    volume.voxelSize = grid->voxelSize().x();
    volume.type = valueOf(grid->getGridClass());
    volume.data = std::vector<float>(dim.x() * dim.y() * dim.z(), grid->background());

    auto value = grid->beginValueOn();
    do {
        auto coord = value.getCoord();
        auto x = coord.x() - iBoxMin.x();
        auto y = coord.y() - iBoxMin.y();
        auto z = coord.z() - iBoxMin.z();

        auto index = (z * dim.y() + y) * dim.x() + x;
        volume.data[index] = *value;
    } while (value.next());



    auto invMaxAxis = 1.f/(volume.bounds.max - volume.bounds.min);
    auto model = glm::mat4{1};

    model = glm::scale(model, invMaxAxis);
    model = glm::translate(model, -volume.bounds.min);

    volume.worldToVoxelTransform = model;
    volume.voxelToWorldTransform = glm::inverse(model);

    const auto tmin = (volume.worldToVoxelTransform * glm::vec4(volume.bounds.min, 1)).xyz();
    const auto tmax = (volume.worldToVoxelTransform * glm::vec4(volume.bounds.max, 1)).xyz();

    assert(glm::all(glm::epsilonEqual(tmin, glm::vec3(0), 0.0001f)));
    assert(glm::all(glm::epsilonEqual(tmax, glm::vec3(1), 0.0001f)));

    return volume;
}

std::map<std::string, Volume> Volume::loadFromVdb(const std::filesystem::path& path) {
    openvdb::io::File file(path.string());
    file.open();
    auto volumes = std::map<std::string, Volume>{};

    for( auto nameItr = file.beginName(); nameItr != file.endName(); ++nameItr) {
        auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(nameItr.gridName()));
        auto volume = load(grid);
        volumes.insert({ grid->getName(), volume});
    }
    file.close();

    return volumes;
}