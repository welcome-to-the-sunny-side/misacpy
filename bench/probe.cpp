#include "bench_functions.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    volatile uint64_t g_sink = 0;
}

int main(int argc, char** argv)
{
    if (argc < 4 or argc > 5)
    {
        std::cerr << "usage: probe <naive|cyccpy> <dis> <n> [iters]\n"
                  << "  iters defaults to enough calls for ~4 GiB of work\n";
        return 1;
    }

    const std::string mode = argv[1];
    const size_t dis = std::strtoull(argv[2], nullptr, 10);
    const size_t n = std::strtoull(argv[3], nullptr, 10);

    if (n == 0)
    {
        std::cerr << "n must be > 0\n";
        return 1;
    }

    using ProbeFn = void (*)(uint8_t*, size_t, size_t);
    ProbeFn fn = nullptr;
    if (mode == "naive")
        fn = bench_naive_loop;
    else if (mode == "cyccpy")
        fn = bench_cyccpy;
    else
    {
        std::cerr << "mode must be 'naive' or 'cyccpy'\n";
        return 1;
    }

    const size_t default_iters = std::max<size_t>(1, (4ull * 1024ull * 1024ull * 1024ull) / n);
    const size_t iters = (argc == 5) ? std::strtoull(argv[4], nullptr, 10) : default_iters;

    std::vector<uint8_t> buf(n + dis + 64, 0xa5);

    for (size_t i = 0; i < iters; ++i)
    {
        fn(buf.data(), dis, n);
    }

    // Sink prevents dead-code elimination of the call loop.
    g_sink ^= buf[n + dis - 1];

    std::cerr << "mode=" << mode << " dis=" << dis << " n=" << n
              << " iters=" << iters << " bytes=" << (iters * n) << "\n";
    return 0;
}
