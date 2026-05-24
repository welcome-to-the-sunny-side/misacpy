#pragma once

#include <cstdint>
#include <cstddef>

namespace misacpy
{
    static_assert(sizeof(size_t) == 8);
    
    void cyccpy(uint8_t* src, const size_t dis, const size_t n);
}