#pragma once

#define shared
#define NUM_NEIGHBOURS 1000

#define EXTERNAL_QUEUE_MEMORY
#include "photon.h"

shared PhotonComp queueData[NUM_NEIGHBOURS];

#include "queue.h"
#include "data_structure.h"

#include <span>

int leftChild(int node){ return 2 * node + 1; }
int rightChild(int node){ return 2 * node + 2; }

using namespace glm;

void  nearestNeighbourSearch (std::span<int> tree, std::span<Photon> photons, vec3 hitPosition, float radius, std::span<int> searchResults, int& numNeighbours) {
    const auto treeSize = tree.size();
    Queue result = createQueue(NUM_NEIGHBOURS);
    vec3 x = hitPosition;
    float d2 = radius * radius;

    Stack stack = createStack();
    Set visited = createSet();

    int node = 0;
    push(stack, node);
    int count = 0;
    do {

        if (contains(visited, node)){
            node = pop(stack);
            continue;
        }
//        spdlog::info("node: {}", node);
        if (tree[node] == -1) {
            insert(visited, node);
            continue;
        }

        int pIndex = tree[node];

        PhotonComp photon = convert(photons[pIndex], pIndex);
        int axis = photon.axis;

        bool isLeafNode = axis == -1;

        // traverse kd tree
        if (rightChild(node) < treeSize && !isLeafNode){
            push(stack, node);

            const float delta = x[axis] - photon.position[axis];
            const float delta2 = delta * delta;

            if (delta < 0) {
                int right = rightChild(node);
                int left = leftChild(node);
                if (!contains(visited, right) && delta2 < d2){
                    push(stack, right);
                } else {
                    remove(visited, right);
                }

                if (!contains(visited, left)) {
                    node = left;
                    continue;
                } else {
                    remove(visited, left);
                }
            } else {
                int right = rightChild(node);
                int left = leftChild(node);
                if (!contains(visited, left) && delta2 < d2){
                    push(stack, left);
                } else {
                    remove(visited, left);
                }
                if (!contains(visited, right)) {
                    node = right;
                    continue;
                } else {
                    remove(visited, right);
                }
            }
        }

        vec3 d = x - photon.position.xyz;
        const float delta2 = dot(d, d);
        photon.delta2 = delta2;

        if (delta2 < d2) {
            if (isFull(result)){
                if (delta2 < front(result).delta2) {
                    d2 = front(result).delta2;
                    push(result, photon);
                }
            } else {
                push(result, photon);
            }
        }

        insert(visited, node);
        count++;
    } while (!empty(stack));

    for(int i = 0; i < result.size; ++i){
        searchResults[i] = get(result, i).index;
    }

    numNeighbours = result.size;
    searchResults[result.size] = -1;
}