#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <vector>

class CpuFluidSolver {
public:
    struct Obstacle {
        glm::vec2 position{0.0f};
        float radius{0.15};
        float smoke{1.0f};
    };

    struct Params {
        Obstacle obstacle;
        int32_t numX{};
        int32_t numY{};
        int32_t numIterations{40};
        float overRelaxation{1.9f};
        float density{1000.0f};
        float gravity{-9.81f};
        float h{1.0f};
        float dt{1.0f / 60.0f};
    };

    enum class Field {
        U,
        V,
        Smoke
    };

    CpuFluidSolver() = default;

    explicit CpuFluidSolver(const Params& pm)
        : u(cellCount(pm))
        , v(cellCount(pm))
        , newU(cellCount(pm))
        , newV(cellCount(pm))
        , P(cellCount(pm))
        , s(cellCount(pm))
        , m(cellCount(pm), 1.0f)
        , newM(cellCount(pm), 1.0f)
        , obstacle(pm.obstacle)
        , gridSize(pm.numX + 2, pm.numY + 2)
        , numCells(cellCount(pm))
        , numIterations(pm.numIterations)
        , overRelaxation(pm.overRelaxation)
        , density(pm.density)
        , gravity(pm.gravity)
        , h(pm.h)
        , dt(pm.dt) {
    }

    void simulate() {
        integrate();

        std::fill(P.begin(), P.end(), 0.0f);
        solveIncompressibility();

        extrapolate();
        advectVel();
        advectSmoke();
    }

    void integrate() {
        const auto n = gridSize.y;
        for (auto i = 1; i < gridSize.x; ++i) {
            for (auto j = 1; j < gridSize.y - 1; ++j) {
                if (s[i * n + j] != 0.0f && s[i * n + j - 1] != 0.0f) {
                    v[i * n + j] += gravity * dt;
                }
            }
        }
    }

    void solveIncompressibility() {
        const auto n = gridSize.y;
        const auto cp = density * h / dt;
        const auto numX = gridSize.x;
        const auto numY = gridSize.y;

        for (auto iter = 0; iter < numIterations; ++iter) {
            for (auto i = 1; i < numX - 1; ++i) {
                for (auto j = 1; j < numY - 1; ++j) {
                    const auto center = i * n + j;
                    if (s[center] == 0.0f) {
                        continue;
                    }

                    const auto sx0 = s[(i - 1) * n + j];
                    const auto sx1 = s[(i + 1) * n + j];
                    const auto sy0 = s[i * n + j - 1];
                    const auto sy1 = s[i * n + j + 1];
                    const auto fluidNeighborCount = sx0 + sx1 + sy0 + sy1;
                    if (fluidNeighborCount == 0.0f) {
                        continue;
                    }

                    const auto div = u[(i + 1) * n + j] - u[i * n + j]
                        + v[i * n + j + 1] - v[i * n + j];

                    const auto pressureCorrection = -div / fluidNeighborCount * overRelaxation;
                    P[center] += cp * pressureCorrection;

                    u[i * n + j] -= sx0 * pressureCorrection;
                    u[(i + 1) * n + j] += sx1 * pressureCorrection;
                    v[i * n + j] -= sy0 * pressureCorrection;
                    v[i * n + j + 1] += sy1 * pressureCorrection;
                }
            }
        }
    }

    void extrapolate() {
        const auto n = gridSize.y;
        for (auto i = 0; i < gridSize.x; ++i) {
            u[i * n] = u[i * n + 1];
            u[i * n + gridSize.y - 1] = u[i * n + gridSize.y - 2];
        }

        for (auto j = 0; j < gridSize.y; ++j) {
            v[j] = v[n + j];
            v[(gridSize.x - 1) * n + j] = v[(gridSize.x - 2) * n + j];
        }
    }

    float sampleField(float x, float y, Field field) const {
        const auto n = gridSize.y;
        const auto h1 = 1.0f / h;
        const auto h2 = 0.5f * h;

        x = std::clamp(x, h, static_cast<float>(gridSize.x) * h);
        y = std::clamp(y, h, static_cast<float>(gridSize.y) * h);

        auto dx = 0.0f;
        auto dy = 0.0f;
        const std::vector<float>* f = &m;

        switch (field) {
            case Field::U:
                f = &u;
                dy = h2;
                break;
            case Field::V:
                f = &v;
                dx = h2;
                break;
            case Field::Smoke:
                f = &m;
                dx = h2;
                dy = h2;
                break;
        }

        const auto x0 = std::min(static_cast<int32_t>(std::floor((x - dx) * h1)), gridSize.x - 1);
        const auto tx = ((x - dx) - static_cast<float>(x0) * h) * h1;
        const auto x1 = std::min(x0 + 1, gridSize.x - 1);

        const auto y0 = std::min(static_cast<int32_t>(std::floor((y - dy) * h1)), gridSize.y - 1);
        const auto ty = ((y - dy) - static_cast<float>(y0) * h) * h1;
        const auto y1 = std::min(y0 + 1, gridSize.y - 1);

        const auto sx = 1.0f - tx;
        const auto sy = 1.0f - ty;

        return sx * sy * (*f)[x0 * n + y0]
            + tx * sy * (*f)[x1 * n + y0]
            + tx * ty * (*f)[x1 * n + y1]
            + sx * ty * (*f)[x0 * n + y1];
    }

    float avgU(int32_t i, int32_t j) const {
        const auto n = gridSize.y;
        return (u[i * n + j - 1] + u[i * n + j]
            + u[(i + 1) * n + j - 1] + u[(i + 1) * n + j]) * 0.25f;
    }

    float avgV(int32_t i, int32_t j) const {
        const auto n = gridSize.y;
        return (v[(i - 1) * n + j] + v[i * n + j]
            + v[(i - 1) * n + j + 1] + v[i * n + j + 1]) * 0.25f;
    }

    void advectVel() {
        newU = u;
        newV = v;

        const auto n = gridSize.y;
        const auto h2 = 0.5f * h;

        for (auto i = 1; i < gridSize.x; ++i) {
            for (auto j = 1; j < gridSize.y; ++j) {
                if (s[i * n + j] != 0.0f && s[(i - 1) * n + j] != 0.0f && j < gridSize.y - 1) {
                    auto x = static_cast<float>(i) * h;
                    auto y = static_cast<float>(j) * h + h2;
                    const auto uFace = u[i * n + j];
                    const auto vFace = avgV(i, j);
                    x -= dt * uFace;
                    y -= dt * vFace;
                    newU[i * n + j] = sampleField(x, y, Field::U);
                }

                if (s[i * n + j] != 0.0f && s[i * n + j - 1] != 0.0f && i < gridSize.x - 1) {
                    auto x = static_cast<float>(i) * h + h2;
                    auto y = static_cast<float>(j) * h;
                    const auto uFace = avgU(i, j);
                    const auto vFace = v[i * n + j];
                    x -= dt * uFace;
                    y -= dt * vFace;
                    newV[i * n + j] = sampleField(x, y, Field::V);
                }
            }
        }

        u = newU;
        v = newV;
    }

    void advectSmoke() {
        newM = m;

        const auto n = gridSize.y;
        const auto h2 = 0.5f * h;

        for (auto i = 1; i < gridSize.x - 1; ++i) {
            for (auto j = 1; j < gridSize.y - 1; ++j) {
                if (s[i * n + j] == 0.0f) {
                    continue;
                }

                const auto uCell = (u[i * n + j] + u[(i + 1) * n + j]) * 0.5f;
                const auto vCell = (v[i * n + j] + v[i * n + j + 1]) * 0.5f;
                const auto x = static_cast<float>(i) * h + h2 - dt * uCell;
                const auto y = static_cast<float>(j) * h + h2 - dt * vCell;

                newM[i * n + j] = sampleField(x, y, Field::Smoke);
            }
        }

        m = newM;
    }

    [[nodiscard]] std::size_t index(int32_t i, int32_t j) const {
        return static_cast<std::size_t>(i * gridSize.y + j);
    }

    [[nodiscard]] glm::ivec2 size() const {
        return gridSize;
    }

    std::vector<float>& uField() { return u; }
    std::vector<float>& vField() { return v; }
    std::vector<float>& pressureField() { return P; }
    std::vector<float>& fluidMask() { return s; }
    std::vector<float>& smokeField() { return m; }

    [[nodiscard]] const std::vector<float>& uField() const { return u; }
    [[nodiscard]] const std::vector<float>& vField() const { return v; }
    [[nodiscard]] const std::vector<float>& pressureField() const { return P; }
    [[nodiscard]] const std::vector<float>& fluidMask() const { return s; }
    [[nodiscard]] const std::vector<float>& smokeField() const { return m; }
    [[nodiscard]] Obstacle getObstacle() const { return obstacle; }

    void updateObstacle(glm::vec2 position, bool reset = false) {
        const auto n = gridSize.y;

        glm::vec2 obstacleVelocity{0.0f};
        if (!reset && dt > 0.0f) {
            obstacleVelocity = (position - obstacle.position) / dt;
        }

        obstacle.position = position;
        const auto radius2 = obstacle.radius * obstacle.radius;

        for (auto i = 1; i < gridSize.x - 2; ++i) {
            for (auto j = 1; j < gridSize.y - 2; ++j) {
                const auto center = i * n + j;
                s[center] = 1.0f;

                const auto cellCenter = (glm::vec2{i - 1, j - 1} + glm::vec2{0.5f}) * h;
                const auto d = cellCenter - obstacle.position;
                if (glm::dot(d, d) >= radius2) {
                    continue;
                }

                s[center] = 0.0f;
                m[center] = obstacle.smoke;

                u[i * n + j] = obstacleVelocity.x;
                u[(i + 1) * n + j] = obstacleVelocity.x;
                v[i * n + j] = obstacleVelocity.y;
                v[i * n + j + 1] = obstacleVelocity.y;
            }
        }

    }

private:
    static std::size_t cellCount(const Params& pm) {
        return static_cast<std::size_t>((pm.numX + 2) * (pm.numY + 2));
    }

    std::vector<float> u;
    std::vector<float> v;
    std::vector<float> newU;
    std::vector<float> newV;
    std::vector<float> P;
    std::vector<float> s;
    std::vector<float> m;
    std::vector<float> newM;
    Obstacle obstacle;
    glm::ivec2 gridSize{};
    std::size_t numCells{};
    int32_t numIterations{};
    float overRelaxation{};
    float density{};
    float gravity{};
    float h{};
    float dt{};
};
