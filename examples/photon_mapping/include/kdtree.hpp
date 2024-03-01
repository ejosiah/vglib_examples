#pragma once

#include "photon.h"

#include <algorithm>
#include <numeric>
#include <functional>
#include <span>
#include <algorithm>
#include <fmt/ranges.h>
#include <stack>
#include <spdlog/spdlog.h>

namespace kdtree {
    constexpr auto ALL_POINT = std::numeric_limits<uint32_t>::max();
    constexpr auto LEFT_SIDE = 0;
    constexpr auto RIGHT_SIDE = 1;

    auto pointComp(const Photon* a, const Photon* b){
        return a->delta2 < b->delta2;
    };

    class Queue {
    public:
        Queue(size_t capacity)
        : mCapacity{capacity}
        , mPoints(capacity == ALL_POINT ? 0 : capacity)
        {}

        void insert(Photon* photon) {
            if(mCapacity == ALL_POINT){
                mPoints.push_back(photon);
                return;
            }

            if(mMode == Mode::Normal) {
                mPoints[mSize++] = photon;
                if(mSize == mCapacity) {
                    std::make_heap(mPoints.begin(), mPoints.end(), pointComp);
                    mMode = Mode::Heap;
                }
            }else  {
                assert(mSize == mCapacity);
                std::pop_heap(mPoints.begin(), mPoints.end(), pointComp);
                mPoints[mSize - 1] = photon;
                std::push_heap(mPoints.begin(), mPoints.end(), pointComp);
            }
        }

        [[nodiscard]]
        auto size() const {
            if(mCapacity == ALL_POINT){
                return mPoints.size();
            }
            return mSize;
        }

        [[nodiscard]]
        auto photons() const {
            if(mCapacity == ALL_POINT){
                return mPoints;
            }
            if(full()) {
                return mPoints;
            }else {
                return std::vector<Photon*>{mPoints.begin(), std::next(mPoints.begin(), mSize)};
            }
        }

        [[nodiscard]]
        auto front() const {
            assert(mSize > 0 || !mPoints.empty());
            return mPoints[0];
        }

        [[nodiscard]]
        bool full() const {
            if(mCapacity == ALL_POINT){
                return false;
            }
            return mSize == mCapacity;
        }

    private:
        enum class Mode{ Normal, Heap };
        const size_t mCapacity;
        std::vector<Photon*> mPoints;
        size_t mSize{};
        Mode mMode{ Mode::Normal};
    };


    enum Side { Left, Right};


    inline int leftChild(int node){ return 2 * node + 1; }
    inline int rightChild(int node){ return 2 * node + 2; }

    bool isLeaf(const std::span<int> tree, int node){
        auto size = tree.size();
        auto left = leftChild(node);
        auto right = rightChild(node);
        return (left >= size && right >= size) || (tree[left] == -1 && tree[right] == -1);
    }

    std::vector<Photon*> search(const std::span<int> tree, std::span<Photon> photons, const glm::vec3& position, float radius, uint32_t numPoints = ALL_POINT) {
        Queue result{ numPoints };
        auto x = position;
        auto d2 = radius * radius;

        std::vector<int> order;


        std::function<void(int)> traverse = [&](int node){
            auto pIdx = tree[node];
            if(pIdx == -1 ) return;
            auto& photon = photons[pIdx];
            auto axis = photon.axis;

            bool isLeafNode = axis == -1;

            if(!isLeafNode && rightChild(node) < tree.size()){
                const auto delta = x[axis] - photon.position[axis];
                const auto delta2 = delta * delta;

                if(delta < 0){
                    traverse(leftChild(node));
                    if(delta2 < d2){
                        traverse(rightChild(node));
                    }
                }else {
                    traverse(rightChild(node));
                    if(delta2 < d2){
                        traverse(leftChild(node));
                    }
                }
            }

            auto d = x - photon.position.xyz();
            const auto delta2 = glm::dot(d, d);
            photon.delta2 = delta2;

            if(delta2 < d2) {
                if(result.full()){
                    if(delta2 < result.front()->delta2) {
                        d2 = result.front()->delta2;
                        result.insert(&photon);
                    }
                }else {
                    result.insert(&photon);
                }
            }
            order.push_back(node + 1);
        };

        traverse(0);

//        spdlog::info("traversal order: {}", order);

        return result.photons();
    }

}