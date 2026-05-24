#include "bench_functions.h"

#include "misacpy.h"

#if defined(__GNUC__) || defined(__clang__)
#define BENCH_NOINLINE __attribute__((noinline))
#else
#define BENCH_NOINLINE
#endif

extern "C" BENCH_NOINLINE void bench_cyccpy(uint8_t* src, const size_t dis, const size_t n)
{
    misacpy::cyccpy(src, dis, n);
}

extern "C" BENCH_NOINLINE void bench_naive_loop(uint8_t* src, const size_t dis, const size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        src[i + dis] = src[i];
    }
}
