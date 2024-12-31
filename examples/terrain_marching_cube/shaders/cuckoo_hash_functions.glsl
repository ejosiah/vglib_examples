#ifndef CUCKOO_HAS_FUNCTIONS_GLSL
#define CUCKOO_HAS_FUNCTIONS_GLSL

#include "cuckoo_hash_common.glsl"


#define DEFINE_HASH_TABLE_QUERY_FUNCTIONS(prefix, contains_name, remove_name, binding_offset ) \
layout(set = 1, binding = binding_offset + 1) buffer prefix##HashSetSsbo { \
    uint table[];   \
} prefix; \
    \
layout(set = 1, binding = binding_offset + 2) buffer prefix##TableInfoSsbo { \
    uint tableSize; \
    uint numItems;  \
} prefix##_metadata;    \
\
bool contains_name(uint key) {\
\
    uint location1 = hash1(key, prefix##_metadata);\
    uint location2 = hash2(key, prefix##_metadata);\
    uint location3 = hash3(key, prefix##_metadata);\
    uint location4 = hash4(key, prefix##_metadata);\
\
    uint entry = prefix.table[location1];\
    if(entry != key) {\
        entry = prefix.table[location2];\
\
        if(entry != key) {\
            entry = prefix.table[location3];\
\
            if(entry != key) {\
                entry = prefix.table[location4];\
\
                if(entry != key) {\
                    return false;\
                }\
            }\
        }\
    }\
    return true;\
}\
\
void remove_name(uint key) {\
    uint location = hash1(key, prefix##_metadata);\
\
    if(prefix.table[location] != key) {\
        location = hash2(key, prefix##_metadata);\
\
        if(prefix.table[location] != key) {\
            location = hash3(key, prefix##_metadata);\
\
            if(prefix.table[location] != key) {\
                location = hash4(key, prefix##_metadata);\
\
                if(prefix.table[location] != key) {\
                    return;\
                }\
            }\
        }\
    }\
    prefix.table[location] = KEY_EMPTY;\
}

DEFINE_HASH_TABLE_QUERY_FUNCTIONS(processed, block_already_processed, remove_from_block_set, 6 )
DEFINE_HASH_TABLE_QUERY_FUNCTIONS(empty_space, block_is_empty_space, remove_block_from_empty_space, 8 )

bool skip_block(uint key) {
    return block_already_processed(key) ||  block_is_empty_space(key);
}

#endif // CUCKOO_HAS_FUNCTIONS_GLSL