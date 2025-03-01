#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <glm/glm.hpp>

static constexpr int kNumSphere = 64;
static constexpr int kNumCylinder = 64;

template<size_t M, size_t N, typename T>
using Table = std::array<std::array<T, N>, N>;

constexpr float m = 1;
constexpr float m2 = m * m;
constexpr float km = 1000 * m;
constexpr float EarthRadius = 6360.0 * km;
constexpr float AtmosphereRadius = 6420.0 * km;

// The height of the atmosphere if its density was uniform.
constexpr float RayleighScaleHeight = 8000.0 * m;

// The height of the Mie particle layer if its density was uniform.
constexpr float MieScaleHeight = 1200.0 * m;

// The Angstrom alpha coefficient for the Mie optical depth.
constexpr double MieAngstromAlpha = 0.8;

// The Angstrom beta coefficient for the Mie optical depth.
constexpr double MieAngstromBeta = 0.04;

// The g parameter of the Cornette-Shanks phase function used for Mie particles.
constexpr double MiePhaseFunctionG = 0.7;

Table<kNumSphere, kNumCylinder, float>  rayleigh_optical_length;
Table<kNumSphere, kNumCylinder, float>  rayleigh_opposite_optical_length;
Table<kNumSphere, kNumCylinder, float>  mie_optical_length;
Table<kNumSphere, kNumCylinder, float>  mie_opposite_optical_length;

float GetSphereRadius(int sphere_index);
float GetCylinderRadius(int cylinder_index);
float DistanceToSphere(float r, float rmu, float sphere_radius);

void Nishita93_Generate_LUT() {
    // Precomputes the optical float lookup tables.
    for (int i = 0; i < kNumSphere; ++i) {
        float r = GetSphereRadius(i);
        float h = r - EarthRadius;

        for (int j = 0; j < kNumCylinder; ++j) {
            float c = std::min(GetCylinderRadius(j), r);
            float rmu = sqrt(r * r - c * c);
            float rayleigh_length = 0.0 * m;
            float mie_length = 0.0 * m;
            float previous_rayleigh_density = exp(-h / RayleighScaleHeight);
            float previous_mie_density = exp(-h / MieScaleHeight);
            float distance_to_previous_sphere = 0.0 * m;
            for (int k = i + 1; k < kNumSphere; ++k) {
                float r_k = GetSphereRadius(k);
                float h_k = r_k - EarthRadius;
                float distance_to_sphere = DistanceToSphere(r, rmu, r_k);
                float rayleigh_density = exp(-h_k / RayleighScaleHeight);
                float mie_density = exp(-h_k / MieScaleHeight);
                float segment_length =
                        distance_to_sphere - distance_to_previous_sphere;
                rayleigh_length += (rayleigh_density + previous_rayleigh_density) / 2 *
                                   segment_length;
                mie_length += (mie_density + previous_mie_density) / 2 * segment_length;
                previous_rayleigh_density = rayleigh_density;
                previous_mie_density = mie_density;
                distance_to_previous_sphere = distance_to_sphere;
            }
            rayleigh_optical_length[i][j] =  rayleigh_length;
            mie_optical_length[i][j] = mie_length;

            rmu = -rmu;
            rayleigh_length = 0.0 * m;
            mie_length = 0.0 * m;
            previous_rayleigh_density = exp(-h / RayleighScaleHeight);
            previous_mie_density = exp(-h / MieScaleHeight);
            distance_to_previous_sphere = 0.0 * m;
            for (int k = i - 1; k > -kNumSphere; --k) {
                float r_k = GetSphereRadius(std::abs(k));
                float h_k = r_k - EarthRadius;
                float distance_to_sphere = DistanceToSphere(r, rmu, r_k);
                if (distance_to_sphere == 0.0 * m) {
                    continue;
                }
                float rayleigh_density = exp(-h_k / RayleighScaleHeight);
                float mie_density = exp(-h_k / MieScaleHeight);
                float segment_length =
                        distance_to_sphere - distance_to_previous_sphere;
                rayleigh_length += (rayleigh_density + previous_rayleigh_density) / 2 *
                                   segment_length;
                mie_length += (mie_density + previous_mie_density) / 2 * segment_length;
                previous_rayleigh_density = rayleigh_density;
                previous_mie_density = mie_density;
                distance_to_previous_sphere = distance_to_sphere;
            }
            rayleigh_opposite_optical_length[i][j] =  rayleigh_length;
            mie_opposite_optical_length[i][j] = mie_length;
        }
    }
}

float GetSphereRadius(int sphere_index) {
    const float a =
            exp(-(AtmosphereRadius - EarthRadius) / RayleighScaleHeight) - 1.0;
    double x = sphere_index / static_cast<double>(kNumSphere - 1);
    return EarthRadius - RayleighScaleHeight * log(a * x + 1.0);
}

float GetCylinderRadius(int cylinder_index) {
    double x = cylinder_index / static_cast<double>(kNumCylinder - 1);
    return AtmosphereRadius * x;
}

float DistanceToSphere(float r, float rmu, float sphere_radius) {
    float delta_sq = sphere_radius * sphere_radius - r * r + rmu * rmu;
    return delta_sq < 0.0 * m2 ? 0.0 * m :
           (r < sphere_radius ? -rmu + sqrt(delta_sq) : -rmu - sqrt(delta_sq));
}
