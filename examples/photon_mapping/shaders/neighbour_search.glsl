#ifndef NEIGHBOUR_SEARCH_GLSL
#define NEIGHBOUR_SEARCH_GLSL

#include "queue.glsl"
#include "data_structure.glsl"

int leftChild(int node){ return 2 * node + 1; }
int rightChild(int node){ return 2 * node + 2; }

/**
    required global variables
     - Photon photons[]
     - int tree[]
     - int searchResults[] should be >= (result.capacity + 1)

*/
void nearestNeighbourSearch(inout Queue result, vec3 position, float radius, out int numNeighboursFound) {
    vec3 x = position;
    float d2 = radius * radius;

    Stack stack = createStack();
    Set visited = createSet();

    int node = 0;
    push(stack, node);
    do {
        if (contains(visited, node)){
            node = pop(stack);
            continue;
        }

        int pIndex = tree[node];
        if (pIndex == -1) {
            insert(visited, node);
            continue;
        }

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
    } while (!empty(stack));

    for(int i = 0; i < result.size; ++i){
        searchResults[i] = get(result, i).index;
    }

    numNeighboursFound = result.size;
    searchResults[result.size] = -1;
}

#endif // NEIGHBOUR_SEARCH_GLSL