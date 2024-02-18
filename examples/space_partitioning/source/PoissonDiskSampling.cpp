#include "PoissonDiskSampling.hpp"

#include <glm/gtc/constants.hpp>
#include <spdlog/spdlog.h>
#include <glm_format.h>

std::vector<glm::vec2> PoissonDiskSampling::generate(const Domain& domain, float r, uint32_t k, uint32_t seed) {
    const auto cellSize = r * INV_SQRT2;
    const auto dim = domain.upper - domain.lower;
    const float w = glm::floor(dim.x/cellSize);
    const float h = glm::floor(dim.y/cellSize);
    const float wOffset = glm::round((w + 1) * .5f) * glm::abs(glm::sign(domain.lower.x));
    const float hOffset = glm::round((h + 1) * .5f) * glm::abs(glm::sign(domain.lower.y));
    const auto rr = r*r;
    std::vector<int> grid(size_t(w * h), -1);
    std::vector<int> activeList{};
    std::vector<glm::vec2> samples{};

    const auto sampleDomain = [&]{
        static std::uniform_real_distribution<float> x{domain.lower.x, domain.upper.x};
        static std::uniform_real_distribution<float> y{domain.lower.y, domain.upper.y};

        static std::default_random_engine engine{seed};
        return glm::vec2( x(engine), y(engine));
    };

    const auto randomPointOnSphericalAnnulus = [&](const glm::vec2& x) {
        static std::uniform_real_distribution<float> dist{0, glm::two_pi<float>()};
        static std::uniform_real_distribution<float> rDist{r, 2 * r};
        static std::default_random_engine engine{seed + 1024};

        auto angle = dist(engine);
        glm::vec2 d(glm::cos(angle), glm::sin(angle));

        auto t = rDist(engine);
//        spdlog::info("t: {}", glm::length(d));

        return x + t * d;
    };

    const auto randomIndex = [&](size_t size){
        static std::uniform_real_distribution<float> dist{0, 0.999999999999};
        static std::default_random_engine engine{seed + 2048};

        return int(glm::floor(float(size) * dist(engine)));
    };

    const auto gridIndex = [&](const glm::vec2& x){
        return glm::ivec2(int(glm::floor(x.x/cellSize) + wOffset), glm::floor(x.y/cellSize) + hOffset);
    };

    const auto flatGridIndex = [&](const glm::vec2& x){
        const auto index = gridIndex(x);
        return index.y * int(w) + index.x;
    };

    const auto insertIntoGrid = [&](const glm::vec2& x) {

        const auto gIndex = flatGridIndex(x);
        const int index = int(samples.size());

        grid[gIndex] = index;
        samples.push_back(x);

        return index;
    };


    auto x = sampleDomain();
    activeList.push_back(insertIntoGrid(x));

    std::vector<glm::vec2> kPoints(k);

    while(!activeList.empty()){
        auto alIndex = randomIndex(activeList.size());
        x = samples[activeList[alIndex]];

        std::generate(kPoints.begin(), kPoints.end(), [&, x]{ return randomPointOnSphericalAnnulus(x); });

        std::vector<glm::vec2> newSamples;
        for(auto& p : kPoints) {

            auto pi = gridIndex(p);
            if(pi.x < 0 || pi.x >= int(w) || pi.y < 0 || pi.y >= int(h)) continue;

            bool farEnough = true;
            for(int i = -1; i <= 1; i++){
                for(int j = -1; j <= 1; j++){
                    int ni = pi.y + i;
                    int nj = pi.x + j;
                    int gIndex = ni * int(w) + nj;
                    auto index = (nj < 0 || nj >= int(w) || ni < 0 || ni >= int(h)) ? -1 : grid[gIndex];
                    if(index != -1){
                        auto nx = samples[index];
                        auto d = p - nx;
                        if(glm::dot(d, d) < rr){
                            farEnough = false;
                        }
                    }
                }
            }
            if(farEnough) {
                newSamples.push_back(p);
                break;
            }
        }
        if(newSamples.empty()){
            std::swap(activeList[alIndex], activeList[activeList.size() - 1]);
            activeList.pop_back();
        }else {
            for(auto& sample : newSamples){
                activeList.push_back(insertIntoGrid(sample));
            }
        }

    }

    return samples;
}