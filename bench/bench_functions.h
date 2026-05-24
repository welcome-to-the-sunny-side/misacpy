#pragma once

#include <cstddef>
#include <cstdint>

extern "C" void bench_cyccpy(uint8_t* src, size_t dis, size_t n);
extern "C" void bench_naive_loop(uint8_t* src, size_t dis, size_t n);
