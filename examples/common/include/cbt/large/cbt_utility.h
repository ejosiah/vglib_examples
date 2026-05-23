#pragma once

// Project includes
#include "cbt/large/cbt.h"

namespace cbt_large {

// Available CBT types
enum class CBTType
{
    OCBT_128K = 0,
    OCBT_256K,
    OCBT_512K,
    OCBT_1M,
    Count
};

// List of the CBT types names
extern const char* g_CBTTypesNames[];
const char* cbt_type_to_string(CBTType type);

// Number of element for a given CBT type
uint32_t cbt_num_elements(CBTType type);

// Create a CBT from a type
CBT* create_cbt(CBTType type);

}

using CBTType = cbt_large::CBTType;
