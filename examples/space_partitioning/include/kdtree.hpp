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


    void balance(Point* start, Point* end, Point* tree, int depth, Bounds domain, int& count) {
        auto distance = std::distance(start, end);
        if(distance <= 0) {
            return;
        };
        if(distance == 1){
            tree[depth] = *start;
            count++;
            return;
        }


        glm::vec2 bMin{std::numeric_limits<float>::max()};
        glm::vec2 bMax{std::numeric_limits<float>::min()};

        std::for_each(start, end, [&](const Point& point){
            bMin = glm::min(point.position, bMin);
            bMax = glm::max(point.position, bMax);
        });

        auto dim = bMax - bMin;
        int axis = dim.x > dim.y ? 0 : 1;

        std::sort(start, end, [axis](const Point& a, const Point& b){
            return a.position[axis] < b.position[axis];
        });

        distance = std::distance(start, end);
        Point* middle = start + distance/2;
        middle->axis = axis;

        middle->start = middle->position;
        middle->start[1 - axis]  = domain.min[1 - axis];

        middle->end = middle->position;
        middle->end[1 - axis] = domain.max[1 - axis];

        tree[depth] = *middle;
        count++;

        balance(start, middle, tree, depth * 2 + 1, { domain.min, middle->end }, count);
        balance(middle + 1, end, tree, depth * 2 + 2, {middle->start, domain.max}, count);
    }

    int leftChild(int depth){ return 2 * depth + 1; }
    int rightChild(int depth){ return 2 * depth + 2; }

    std::vector<Point*> search(std::span<Point> points, const SearchArea& area, uint32_t numPoints = ALL_POINT) {
        std::vector<Point*> result;
        auto x = area.position;
        auto d2 = area.radius * area.radius;

        std::vector<int> order;

        auto comp = [&](Point* a, Point* b){
            return a->delta2 < b->delta2;
        };

        std::function<void(int)> traverse = [&](int depth){
            auto& point = points[depth];
            auto axis = point.axis;

            if(rightChild(depth) < points.size()){
                const auto delta = x[axis] - point.position[axis];
                const auto delta2 = delta * delta;

                if(delta < 0){
                    traverse(leftChild(depth));
                    if(delta2 < d2){
                        traverse(rightChild(depth));
                    }
                }else {
                    traverse(rightChild(depth));
                    if(delta2 < d2){
                        traverse(leftChild(depth));
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
                        result.pop_back();
                        result.push_back(&point);
                        std::push_heap(result.begin(), result.end(), comp);
                    }
                }else {
                    result.push_back(&point);
                }
            }
            if(result.size() == numPoints){
                std::make_heap(result.begin(), result.end(), comp);
            }
            order.push_back(depth + 1);
        };

        traverse(0);

//        spdlog::info("traversal order: {}", order);

        return result;
    }

    std::vector<Point*> search_loop(std::span<Point> points, const SearchArea& area, uint32_t numPoints = ALL_POINT){
        std::vector<Point*> result;
        auto x = area.position;
        auto d2 = area.radius * area.radius;

        auto comp = [&](Point* a, Point* b){
            return a->delta2 < b->delta2;
        };


        std::stack<int> stack;
        std::set<int> visited;
        stack.push(0);

        int depth = 0;
        size_t maxStackSize = 0;
        do{
            maxStackSize = std::max(maxStackSize, stack.size());
            if(visited.contains(depth)){
                depth = stack.top();
                stack.pop();
                continue;
            }
            auto& point = points[depth];
            auto axis = point.axis;

            // traverse kd tree
            if(rightChild(depth) < points.size()){
                stack.push(depth);
                const auto delta = x[axis] - point.position[axis];
                const auto delta2 = delta * delta;

                if(delta < 0) {
                    auto right = rightChild(depth);
                    auto left = leftChild(depth);
                    if(!visited.contains(right) && delta2 < d2){
                        stack.push(right);
                    }
                    if(!visited.contains(left)) {
                        depth = left;
                        continue;
                    }
                }else {
                    auto right = rightChild(depth);
                    auto left = leftChild(depth);
                    if(!visited.contains(left) && delta2 < d2){
                        stack.push(left);
                    }
                    if(!visited.contains(right)) {
                        depth = right;
                        continue;
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
                        result.pop_back();
                        result.push_back(&point);
                        std::push_heap(result.begin(), result.end(), comp);
                    }
                }else {
                    result.push_back(&point);
                }
            }
            if(result.size() == numPoints){
                std::make_heap(result.begin(), result.end(), comp);
            }
            visited.insert(depth);
        }while(!stack.empty());

        spdlog::info("visited : {}, max stack size: {}", visited.size(), maxStackSize);

        return result;
    }
}