#ifndef CUCKOO_HAS_FUNCTIONS_GLSL
#define CUCKOO_HAS_FUNCTIONS_GLSL

#include "cuckoo_hash_common.glsl"

layout(set = 1, binding = 7) buffer HashSetSsbo {
    uint table[];
} processed;

layout(set = 1, binding = 8) buffer TableInfoSsbo {
    uint tableSize;
    uint numItems;
} processed_metadata;


bool block_already_processed(uint key) {

    uint location1 = hash1(key, processed_metadata);
    uint location2 = hash2(key, processed_metadata);
    uint location3 = hash3(key, processed_metadata);
    uint location4 = hash4(key, processed_metadata);

    uint entry = processed.table[location1];
    if(entry != key) {
        entry = processed.table[location2];

        if(entry != key) {
            entry = processed.table[location3];

            if(entry != key) {
                entry = processed.table[location4];

                if(entry != key) {
                    return false;
                }
            }
        }
    }
    return true;
}

void remove_from_block_set(uint key) {
    uint location = hash1(key, processed_metadata);

    if(processed.table[location] != key) {
        location = hash2(key, processed_metadata);

        if(processed.table[location] != key) {
            location = hash3(key, processed_metadata);

            if(processed.table[location] != key) {
                location = hash4(key, processed_metadata);

                if(processed.table[location] != key) {
                    return;
                }
            }
        }
    }
    processed.table[location] = KEY_EMPTY;
}
#endif // CUCKOO_HAS_FUNCTIONS_GLSL