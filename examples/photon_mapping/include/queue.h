#pragma once

#ifndef QUEUE_GLSL
#define QUEUE_GLSL

#ifdef EXTERNAL_QUEUE_MEMORY
#define QUEUE_DATA queueData
#else
#define QUEUE_DATA queue.data
#endif // EXTERNAL_QUEUE_MEMORY

#ifndef QUEUE_ENTRY_TYPE
#define QUEUE_ENTRY_TYPE int
#endif // QUEUE_ENTRY_TYPE

#ifndef QUEUE_ENTRY_COMP
#define QUEUE_ENTRY_COMP(a, b) a < b
#endif // QUEUE_ENTRY_COMP

#define QUEUE_CAPACITY 60
#define QUEUE_MODE_NORMAL 0
#define QUEUE_MODE_HEAP 1

#ifndef inout
#define inout
#endif // inout

struct Queue {
    QUEUE_ENTRY_TYPE data[QUEUE_CAPACITY];
    int size;
    int capacity;
    int mode;
};

Queue createQueue(int size) {
    Queue queue;
    queue.capacity = size;
    queue.size = 0;
    queue.mode = QUEUE_MODE_NORMAL;

    return queue;
}

void makeHeap(inout Queue& queue);
void popHeap(inout Queue& queue);
void pushHeap(inout Queue& queue);


QUEUE_ENTRY_TYPE get(Queue& queue, int index) {
    return QUEUE_DATA[index];
}

void push(inout Queue& queue, QUEUE_ENTRY_TYPE entry){
    if(queue.mode == QUEUE_MODE_NORMAL){
        QUEUE_DATA[queue.size++] = entry;
        if(queue.size == queue.capacity) {
            makeHeap(queue);
            queue.mode = QUEUE_MODE_HEAP;
        }
    }else {
        popHeap(queue);
        QUEUE_DATA[queue.size - 1] = entry;
        pushHeap(queue);
    }
}

bool isFull(Queue& queue) {
    return queue.size == queue.capacity;
}

QUEUE_ENTRY_TYPE front(Queue& queue) {
    return QUEUE_DATA[0];
}

void makeHeap(inout Queue& queue) {
    int bottom = queue.size;

    for(int hole = bottom >> 1; hole > 0;) {
        --hole;
        QUEUE_ENTRY_TYPE val = QUEUE_DATA[hole];
        const int top = hole;
        int index       = hole;

        const int Max_sequence_non_leaf = (bottom - 1) >> 1;
        while (index < Max_sequence_non_leaf) {
            index = 2 * index + 2;
            if (QUEUE_ENTRY_COMP(QUEUE_DATA[index], QUEUE_DATA[index - 1])) {
                --index;
            }
            QUEUE_DATA[hole] = QUEUE_DATA[index];
            hole             = index;
        }

        if (index == Max_sequence_non_leaf && bottom % 2 == 0) {
            QUEUE_DATA[hole] = QUEUE_DATA[bottom];
            hole             = bottom - 1;
        }

        for (index = (hole - 1) >> 1; top < hole && QUEUE_ENTRY_COMP(QUEUE_DATA[index], val); index = (hole - 1) >> 1) {
            QUEUE_DATA[hole] = QUEUE_DATA[index];
            hole             = index;
        }

        QUEUE_DATA[hole] = val;
    }

    queue.mode = QUEUE_MODE_HEAP;
}

void popHeap(inout Queue& queue) {
    int last = queue.size;
    if (2 <= last) {
        --last;
        QUEUE_ENTRY_TYPE val = QUEUE_DATA[last];

        QUEUE_DATA[last] = QUEUE_DATA[0];

        int bottom = last;
        int hole = 0;
        const int top = hole;
        int idx       = hole;


        const int maxSequenceNonLeaf = (bottom - 1) >> 1;
        while (idx < maxSequenceNonLeaf) {
            idx = 2 * idx + 2;
            if (QUEUE_ENTRY_COMP(QUEUE_DATA[idx], QUEUE_DATA[idx - 1])) {
                --idx;
            }
            QUEUE_DATA[hole] = QUEUE_DATA[idx];
            hole             = idx;
        }

        if (idx == maxSequenceNonLeaf && bottom % 2 == 0) {
            QUEUE_DATA[hole] = QUEUE_DATA[bottom - 1];
            hole             = bottom - 1;
        }

        for (idx = (hole - 1) >> 1; top < hole && QUEUE_ENTRY_COMP(QUEUE_DATA[idx], val); idx = (hole - 1) >> 1) {
            QUEUE_DATA[hole] = QUEUE_DATA[idx];
            hole             = idx;
        }

        QUEUE_DATA[hole] = val;
    }
}

void pushHeap(inout Queue& queue) {
    int hole = queue.size - 1;
    if (2 <= queue.size) {
        QUEUE_ENTRY_TYPE val = QUEUE_DATA[hole];

        for (int idx = (hole - 1) >> 1; hole > 0 && QUEUE_ENTRY_COMP(QUEUE_DATA[idx], val); idx = (hole - 1) >> 1) {
            // move _Hole up to parent
            QUEUE_DATA[hole] = QUEUE_DATA[idx];
            hole = idx;
        }

        QUEUE_DATA[hole] = val; // drop _Val into final hole
    }
}

#endif // QUEUE_GLSL