#pragma once

#include "domain.hpp"

#include <algorithm>
#include <numeric>
#include <functional>
#include <span>
#include <algorithm>
#include <fmt/ranges.h>
#include <stack>

namespace kdtree {
    constexpr auto ALL_POINT = std::numeric_limits<uint32_t>::max();
    constexpr auto LEFT_SIDE = 0;
    constexpr auto RIGHT_SIDE = 1;

    auto pointComp(Point* a, Point* b){
        return a->delta2 < b->delta2;
    };

    class Queue {
    public:
        Queue(size_t capacity)
        : mCapacity{capacity}
        , mPoints(capacity)
        {}

        void insert(Point* point) {
            if(mMode == Mode::Normal) {
                mPoints[mSize++] = point;
                if(mSize == mCapacity) {
                    spdlog::info("point queue full switching to heap mode");
                    std::make_heap(mPoints.begin(), mPoints.end(), pointComp);
                    mMode = Mode::Heap;
                }
            }else  {
                assert(mSize == mCapacity);
                std::pop_heap(mPoints.begin(), mPoints.end(), pointComp);
                mPoints[mSize - 1] = point;
                std::push_heap(mPoints.begin(), mPoints.end(), pointComp);
            }
        }

        [[nodiscard]]
        auto size() const {
            return mSize;
        }

        [[nodiscard]]
        auto points() const {
            if(full()) {
                return mPoints;
            }else {
                return std::vector<Point*>{mPoints.begin(), std::next(mPoints.begin(), mSize)};
            }
        }

        [[nodiscard]]
        auto front() const {
            assert(mSize > 0);
            return mPoints[0];
        }

//        Point& operator[](int& idx) {
//            assert(idx < mSize);
//            return *mPoints[idx];
//        }

        bool full() const {
            return mSize == mCapacity;
        }

    private:
        enum class Mode{ Normal, Heap };
        const size_t mCapacity;
        std::vector<Point*> mPoints;
        size_t mSize{};
        Mode mMode{ Mode::Normal};
    };

    const Point NullPoint{
        .color = glm::vec4(0),
        .position = glm::vec4(std::numeric_limits<float>::quiet_NaN()),
        .start = glm::vec2(std::numeric_limits<float>::quiet_NaN()),
        .end = glm::vec2(std::numeric_limits<float>::quiet_NaN())
    };

    enum Side { Left, Right};

    void balanceSubTree(std::vector<Point*>& tree, std::span<Point> points, Bounds domain, int parent, Side side, int numPoints, int& count) {
        auto index = (side == Side::Left) ? (2  * parent + 1) : (2 * parent + 2);
        if(points.empty()){
//            spdlog::info("index: {}", index);
            tree[index] = nullptr;
            return;
        }


        glm::vec2 bMin{std::numeric_limits<float>::max()};
        glm::vec2 bMax{std::numeric_limits<float>::min()};

        std::for_each(points.begin(), points.end(), [&](const Point& point){
            bMin = glm::min(point.position, bMin);
            bMax = glm::max(point.position, bMax);
        });

        auto dim = bMax - bMin;
        int axis = dim.x > dim.y ? 0 : 1;

        std::sort(points.begin(), points.end(), [axis](const Point& a, const Point& b){
            return a.position[axis] < b.position[axis];
        });

//        std::string pStr{};
//        for(auto& point : points) {
//            pStr += fmt::format("[{:.2f}, {:.2f}], ", point.position.x ,point.position.y);
//        }
//        spdlog::info("axis: {}, points: {}", axis, pStr);

        auto middleIndex = points.size()/2;
        Point& middle = points[middleIndex];
        middle.axis = middleIndex > 0 ? axis : -1;

        middle.start = middle.position;
        middle.start[1 - axis]  = domain.min[1 - axis];

        middle.end = middle.position;
        middle.end[1 - axis] = domain.max[1 - axis];

        tree[index] = &middle;
        count++;


        if(2 * index + 2 < numPoints){
            balanceSubTree(tree, {points.data(), middleIndex }, { domain.min, middle.end }, index, Side::Left, numPoints, count);
            auto offset = middleIndex + 1;
            balanceSubTree(tree, {points.data() + offset, points.size() - offset }, {middle.start, domain.max}, index, Side::Right, numPoints, count);
        }
    }

    std::vector<int> balance(std::vector<Point>& points, Bounds domain, int& count) {
        auto n = std::ceil(std::log2(points.size()));
        auto size = size_t(std::pow(2, n));
        std::vector<Point*> tree(size);
        balanceSubTree(tree, points, domain, -1, Side::Right, size, count);

        std::vector<int> treeIndex(size);

        for(int i = 0; i < size; i++){
            auto itr = std::find_if(points.begin(), points.end(), [&](auto& point){ return  tree[i] == &point; });
            treeIndex[i] = itr != points.end() ? std::distance(points.begin(), itr) : -1;
        }
        return treeIndex;;
    }

    inline int leftChild(int node){ return 2 * node + 1; }
    inline int rightChild(int node){ return 2 * node + 2; }

    bool isLeaf(const std::span<int> tree, int node){
        auto size = tree.size();
        auto left = leftChild(node);
        auto right = rightChild(node);
        return (left >= size && right >= size) || (tree[left] == -1 && tree[right] == -1);
    }

    std::vector<Point*> search(const std::vector<int>& tree, std::span<Point> points, const SearchArea& area, uint32_t numPoints = ALL_POINT) {
        std::vector<Point*> result;
        auto x = area.position;
        auto d2 = area.radius * area.radius;

        std::vector<int> order;

        auto comp = [&](Point* a, Point* b){
            return a->delta2 < b->delta2;
        };

        std::function<void(int)> traverse = [&](int node){
            auto pIdx = tree[node];
            if(pIdx == -1 ) return;
            auto& point = points[pIdx];
            auto axis = point.axis;

            if(rightChild(node) < tree.size()){
                const auto delta = x[axis] - point.position[axis];
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

            auto d = x - point.position;
            const auto delta2 = glm::dot(d, d);
            point.delta2 = delta2;

            if(delta2 < d2) {
                if(result.size() == numPoints){
                    if(delta2 < result.front()->delta2) {
                        d2 = result.front()->delta2;
                        std::pop_heap(result.begin(), result.end(), comp);
                        result[result.size() - 1] = &point;
                        std::push_heap(result.begin(), result.end(), comp);
                    }
                }else {
                    result.push_back(&point);
                }
            }
            if(result.size() == numPoints){
                std::make_heap(result.begin(), result.end(), comp);
            }
            order.push_back(node + 1);
        };

        traverse(0);

//        spdlog::info("traversal order: {}", order);

        return result;
    }

    std::vector<Point*> search_loop(const std::span<int> tree, std::span<Point> points, const SearchArea& area, uint32_t numPoints = ALL_POINT){
        Queue result{ numPoints };
        auto x = area.position;
        auto d2 = area.radius * area.radius;

        std::stack<int> stack;
        std::set<int> visited;
        stack.push(0);

        int node = 0;
        size_t maxStackSize = 0;
        do{
            if(visited.contains(node)){
                node = stack.top();
                stack.pop();
                continue;
            }

            if(tree[node] == -1) {
                visited.insert(node);
                continue;
            }

            auto& point = points[tree[node]];
            auto axis = point.axis;

            // traverse kd tree
            if(&point != &NullPoint && rightChild(node) < tree.size()){
                stack.push(node);
                assert(axis == 0 || axis == 1);
                const auto delta = x[axis] - point.position[axis];
                const auto delta2 = delta * delta;

                if(delta < 0) {
                    auto right = rightChild(node);
                    auto left = leftChild(node);
                    if(!visited.contains(right) && delta2 < d2){
                        stack.push(right);
                    }else {
                        visited.erase(right);
                    }

                    if(!visited.contains(left)) {
                        node = left;
                        continue;
                    }else{
                        visited.erase(left);
                    }
                }else {
                    auto right = rightChild(node);
                    auto left = leftChild(node);
                    if(!visited.contains(left) && delta2 < d2){
                        stack.push(left);
                    }else {
                        visited.erase(left);
                    }
                    if(!visited.contains(right)) {
                        node = right;
                        continue;
                    }else {
                        visited.erase(right);
                    }
                }
            }

            auto d = x - point.position;
            const auto delta2 = glm::dot(d, d);
            point.delta2 = delta2;

            if(delta2 < d2) {
                if(result.size() == numPoints){
                    if(delta2 < result.front()->delta2) {
                        d2 = result.front()->delta2;
                        result.insert(&point);
                    }
                }else {
                    result.insert(&point);
                }
            }

            visited.insert(node);
            maxStackSize = std::max(maxStackSize, stack.size());

        }while(!stack.empty());

        spdlog::info("visited : {}, max stack size: {}", visited.size(), maxStackSize);

        return result.points();
    }
}