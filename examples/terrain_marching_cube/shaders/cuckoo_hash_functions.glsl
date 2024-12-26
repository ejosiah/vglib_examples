#ifndef CUCKOO_HAS_FUNCTIONS_GLSL
#define CUCKOO_HAS_FUNCTIONS_GLSL

#include "cuckoo_hash_common.glsl"

layout(set = 1, binding = 7) buffer HashSetSsbo {
    uint table[];
};

layout(set = 1, binding = 8) buffer TableInfoSsbo {
    uint tableSize;
    uint numItems;
};

uint get_entry(uint location) {
    return table[location];
}

bool block_already_processed(uint key) {

    uint location1 = hash1(key);
    uint location2 = hash2(key);
    uint location3 = hash3(key);
    uint location4 = hash4(key);

    uint entry = get_entry(location1);
    if(entry != key) {
        entry = get_entry(location2);

        if(entry != key) {
            entry = get_entry(location3);

            if(entry != key) {
                entry = get_entry(location4);

                if(entry != key) {
                    return false;
                }
            }
        }
    }
    return true;
}

void remove_from_block_set(uint key) {
    uint location = hash1(key);

    if(get_entry(location) != key) {
        location = hash2(key);

        if(get_entry(location) != key) {
            location = hash3(key);

            if(get_entry(location) != key) {
                location = hash4(key);

                if(get_entry(location) != key) {
                    return;
                }
            }
        }
    }
    table[location] = KEY_EMPTY;
}
#endif // CUCKOO_HAS_FUNCTIONS_GLSL