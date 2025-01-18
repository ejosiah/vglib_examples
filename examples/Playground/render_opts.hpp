#pragma once
#include <openvdb/openvdb.h>
#include <string>

const double LIGHT_DEFAULTS[] = { 0.3, 0.3, 0.0, 0.7, 0.7, 0.7 };

struct RenderOpts
{
    std::string shader;
    std::string color;
    openvdb::Vec3SGrid::Ptr colorgrid;
    std::string camera;
    float aperture, focal, frame, znear, zfar;
    double isovalue;
    openvdb::Vec3d rotate;
    openvdb::Vec3d translate;
    openvdb::Vec3d target;
    openvdb::Vec3d up;
    bool lookat;
    size_t samples;
    openvdb::Vec3d absorb;
    std::vector<double> light;
    openvdb::Vec3d scatter;
    double cutoff, gain;
    openvdb::Vec2d step;
    size_t width, height;
    std::string compression;
    int threads;
    bool verbose;

    RenderOpts():
            shader("diffuse"),
            camera("perspective"),
            aperture(41.2136f),
            focal(50.0f),
            frame(1.0f),
            znear(1.0e-3f),
            zfar(std::numeric_limits<float>::max()),
            isovalue(0.0),
            rotate(0.0),
            translate(0.0),
            target(0.0),
            up(0.0, 1.0, 0.0),
            lookat(false),
            samples(1),
            absorb(0.1),
            light(LIGHT_DEFAULTS, LIGHT_DEFAULTS + 6),
            scatter(1.5),
            cutoff(0.005),
            gain(0.2),
            step(1.0, 3.0),
            width(512),
            height(512),
            compression("zip"),
            threads(0),
            verbose(false)
    {}

    std::string validate() const
    {
        if (shader != "diffuse" && shader != "matte" && shader != "normal" && shader != "position"){
            return "expected diffuse, matte, normal or position shader, got \"" + shader + "\"";
        }
        if (!openvdb::string::starts_with(camera, "ortho") && !openvdb::string::starts_with(camera, "persp")) {
            return "expected perspective or orthographic camera, got \"" + camera + "\"";
        }
        if (compression != "none" && compression != "rle" && compression != "zip") {
            return "expected none, rle or zip compression, got \"" + compression + "\"";
        }
        if (width < 1 || height < 1) {
            std::ostringstream ostr;
            ostr << "expected width > 0 and height > 0, got " << width << "x" << height;
            return ostr.str();
        }
        return "";
    }

    std::ostream& put(std::ostream& os) const
    {
        os << " -absorb " << absorb[0] << "," << absorb[1] << "," << absorb[2]
           << " -aperture " << aperture
           << " -camera " << camera;
        if (!color.empty()) os << " -color '" << color << "'";
        os << " -compression " << compression
           << " -cpus " << threads
           << " -cutoff " << cutoff
           << " -far " << zfar
           << " -focal " << focal
           << " -frame " << frame
           << " -gain " << gain
           << " -isovalue " << isovalue
           << " -light " << light[0] << "," << light[1] << "," << light[2]
           << "," << light[3] << "," << light[4] << "," << light[5];
        if (lookat) os << " -lookat " << target[0] << "," << target[1] << "," << target[2];
        os << " -near " << znear
           << " -res " << width << "x" << height;
        if (!lookat) os << " -rotate " << rotate[0] << "," << rotate[1] << "," << rotate[2];
        os << " -shader " << shader
           << " -samples " << samples
           << " -scatter " << scatter[0] << "," << scatter[1] << "," << scatter[2]
           << " -shadowstep " << step[1]
           << " -step " << step[0]
           << " -translate " << translate[0] << "," << translate[1] << "," << translate[2];
        if (lookat) os << " -up " << up[0] << "," << up[1] << "," << up[2];
        if (verbose) os << " -v";
        return os;
    }
};

std::ostream& operator<<(std::ostream& os, const RenderOpts& opts) { return opts.put(os); }