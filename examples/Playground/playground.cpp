#include <openvdb/openvdb.h>
#include <openvdb/tools/RayIntersector.h>
#include <openvdb/tools/RayTracer.h>
#include <filesystem>
#include <iostream>
#include "render_opts.hpp"

constexpr auto maxDepth = 20UL;
constexpr auto iniMaxDepth = maxDepth;

const char* gProgName = "";

namespace fs = std::filesystem;

template<typename GridType>
void
render(const GridType& grid, const std::string& imgFilename, const RenderOpts& opts)
{
    using namespace openvdb;

    const bool isLevelSet = (grid.getGridClass() == GRID_LEVEL_SET);

    tools::Film film(opts.width, opts.height);

    std::unique_ptr<tools::BaseCamera> camera;
    if (openvdb::string::starts_with(opts.camera, "persp")) {
        camera.reset(new tools::PerspectiveCamera(film, opts.rotate, opts.translate,
                                                  opts.focal, opts.aperture, opts.znear, opts.zfar));
    } else if (openvdb::string::starts_with(opts.camera, "ortho")) {
        camera.reset(new tools::OrthographicCamera(film, opts.rotate, opts.translate,
                                                   opts.frame, opts.znear, opts.zfar));
    } else {
        OPENVDB_THROW(ValueError,
                      "expected perspective or orthographic camera, got \"" << opts.camera << "\"");
    }
    if (opts.lookat) camera->lookAt(opts.target, opts.up);

    // Define the shader for level set rendering.  The default shader is a diffuse shader.
    std::unique_ptr<tools::BaseShader> shader;
    if (opts.shader == "matte") {
        if (opts.colorgrid) {
            shader.reset(new tools::MatteShader<openvdb::Vec3SGrid>(*opts.colorgrid));
        } else {
            shader.reset(new tools::MatteShader<>());
        }
    } else if (opts.shader == "normal") {
        if (opts.colorgrid) {
            shader.reset(new tools::NormalShader<Vec3SGrid>(*opts.colorgrid));
        } else {
            shader.reset(new tools::NormalShader<>());
        }
    } else if (opts.shader == "position") {
        const CoordBBox bbox = grid.evalActiveVoxelBoundingBox();
        const math::BBox<Vec3d> bboxIndex(bbox.min().asVec3d(), bbox.max().asVec3d());
        const math::BBox<Vec3R> bboxWorld = bboxIndex.applyMap(*(grid.transform().baseMap()));
        if (opts.colorgrid) {
            shader.reset(new tools::PositionShader<Vec3SGrid>(bboxWorld, *opts.colorgrid));
        } else {
            shader.reset(new tools::PositionShader<>(bboxWorld));
        }
    } else /* if (opts.shader == "diffuse") */ { // default
        if (opts.colorgrid) {
            shader.reset(new tools::DiffuseShader<Vec3SGrid>(*opts.colorgrid));
        } else {
            shader.reset(new tools::DiffuseShader<>());
        }
    }

    if (opts.verbose) {
        std::cout << gProgName << ": ray-tracing";
        const std::string gridName = grid.getName();
        if (!gridName.empty()) std::cout << " " << gridName;
        std::cout << "..." << std::endl;
    }

    if (isLevelSet) {
        tools::LevelSetRayIntersector<GridType> intersector(
                grid, static_cast<typename GridType::ValueType>(opts.isovalue));
        tools::rayTrace(grid, intersector, *shader, *camera, opts.samples,
                /*seed=*/0, (opts.threads != 1));
    } else {
        using IntersectorType = tools::VolumeRayIntersector<GridType>;
        IntersectorType intersector(grid);

        tools::VolumeRender<IntersectorType> renderer(intersector, *camera);
        renderer.setLightDir(opts.light[0], opts.light[1], opts.light[2]);
        renderer.setLightColor(opts.light[3], opts.light[4], opts.light[5]);
        renderer.setPrimaryStep(opts.step[0]);
        renderer.setShadowStep(opts.step[1]);
        renderer.setScattering(opts.scatter[0], opts.scatter[1], opts.scatter[2]);
        renderer.setAbsorption(opts.absorb[0], opts.absorb[1], opts.absorb[2]);
        renderer.setLightGain(opts.gain);
        renderer.setCutOff(opts.cutoff);

        renderer.render(false);
    }



    if (openvdb::string::ends_with(imgFilename, ".ppm")) {
        // Save as PPM (fast, but large file size).
        std::string filename = imgFilename;
        filename.erase(filename.size() - 4); // strip .ppm extension
        film.savePPM(filename);
    } else if (openvdb::string::ends_with(imgFilename, ".exr")) {
        // Save as EXR (slow, but small file size).
//        saveEXR(imgFilename, film, opts);
    } else if (openvdb::string::ends_with(imgFilename, ".png")) {
//        PngWriter png;
//        png.write(imgFilename, film);
    } else {
        OPENVDB_THROW(ValueError, "unsupported image file format (" + imgFilename + ")");
    }
}

int main() {
    fs::current_path("../../../../examples/");

    openvdb::initialize();

    openvdb::io::File file{"data/cloud_v100_0.10.vdb"};

    file.open();

    auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(file.beginName().gridName()));

    render(*grid, "out.ppm", RenderOpts{});
    file.close();
}
