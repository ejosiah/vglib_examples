#pragma once

#include <cinttypes>

uint32_t find_msb(uint32_t x);
uint32_t find_msb_64(uint64_t x);
int32_t round_up_power2(uint32_t v);
uint32_t countbits(uint32_t i);
uint32_t countbits(uint64_t i);