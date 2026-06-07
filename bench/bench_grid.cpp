// Grid benchmark driver: runs a cartesian dis x n grid through the same timing
// harness as bench.cpp and emits one CSV row per cell (to stdout). Progress goes
// to stderr. Correctness is checked per cell (same check as bench.cpp); a failing
// cell is logged and skipped rather than aborting the whole sweep.
//
//   ./scripts/quiet-run.sh ./bench/build/bench_grid > tmp/grid.csv
//
// The dis / n grids are defined at the top of main(); edit them there.

#include "bench_functions.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using BenchFn = void (*)(uint8_t*, size_t, size_t);

    struct Case
    {
        size_t dis;
        size_t n;
    };

    struct Timing
    {
        double min_ns_per_call;
        double avg_ns_per_call;
        double max_ns_per_call;
        uint64_t checksum;
    };

    // Kept identical to bench.cpp so the numbers are directly comparable.
    constexpr size_t kMaxBlocks = 65536;
    constexpr size_t kMemoryCap = 128ull * 1024ull * 1024ull;
    constexpr size_t kTargetBytes = 16ull * 1024ull * 1024ull;
    constexpr size_t kMinSmallCalls = 20000;
    constexpr size_t kMaxCalls = 500000;
    constexpr int kWarmupSamples = 3;
    constexpr int kSamples = 20;

    volatile uint64_t g_sink = 0;

    size_t align_up(const size_t value, const size_t alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    uint8_t pattern(const size_t block, const size_t index)
    {
        uint64_t x = (block + 1) * 0x9e3779b97f4a7c15ull + index * 0xbf58476d1ce4e5b9ull;
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebull;
        x ^= x >> 31;
        return static_cast<uint8_t>(x);
    }

    void fill_arena(std::vector<uint8_t>& arena, const size_t stride, const size_t active_len)
    {
        const size_t blocks = arena.size() / stride;
        for (size_t block = 0; block < blocks; ++block)
        {
            uint8_t* ptr = arena.data() + block * stride;
            for (size_t i = 0; i < active_len; ++i)
            {
                ptr[i] = pattern(block, i);
            }
            std::memset(ptr + active_len, 0xa5, stride - active_len);
        }
    }

    uint64_t checksum_arena(const std::vector<uint8_t>& arena, const size_t stride, const size_t active_len)
    {
        const size_t blocks = arena.size() / stride;
        uint64_t hash = 1469598103934665603ull;
        for (size_t block = 0; block < blocks; ++block)
        {
            const uint8_t* ptr = arena.data() + block * stride;
            for (size_t i = 0; i < active_len; ++i)
            {
                hash ^= ptr[i];
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    void check_case(const Case& c)
    {
        const size_t len = c.n + c.dis;
        std::vector<uint8_t> expected(len);
        std::vector<uint8_t> actual(len);

        for (size_t i = 0; i < len; ++i)
        {
            expected[i] = pattern(0, i);
        }
        actual = expected;

        bench_naive_loop(expected.data(), c.dis, c.n);
        bench_cyccpy(actual.data(), c.dis, c.n);

        if (expected != actual)
        {
            throw std::runtime_error("correctness check failed for dis=" + std::to_string(c.dis)
                                     + " n=" + std::to_string(c.n));
        }
    }

    Timing run_one(const Case& c, const BenchFn fn)
    {
        const size_t active_len = c.n + c.dis;
        const size_t stride = align_up(active_len + 64, 64);
        const size_t blocks_by_memory = std::max<size_t>(1, kMemoryCap / stride);
        const size_t target_calls_by_bytes = std::max<size_t>(1, kTargetBytes / std::max<size_t>(c.n, 1));
        const size_t min_calls = c.n <= 128 ? kMinSmallCalls : 1;
        const size_t target_calls = std::clamp(target_calls_by_bytes, min_calls, kMaxCalls);
        const size_t blocks = std::min({kMaxBlocks, blocks_by_memory, target_calls});
        const size_t passes = (target_calls + blocks - 1) / blocks;
        const size_t calls = blocks * passes;

        std::vector<uint8_t> arena(blocks * stride);
        std::vector<double> samples;
        samples.reserve(kSamples);

        // Reset by re-filling the single arena in place, exactly like bench.cpp.
        // Do NOT reset from a separate template buffer: that doubles the cache
        // footprint of the reset and evicts the arena from L3, so the timed call
        // would measure DRAM-cold data instead of the L3-resident arena.
        for (int sample = 0; sample < kWarmupSamples; ++sample)
        {
            fill_arena(arena, stride, active_len);
            for (size_t pass = 0; pass < passes; ++pass)
                for (size_t block = 0; block < blocks; ++block)
                    fn(arena.data() + block * stride, c.dis, c.n);
        }

        for (int sample = 0; sample < kSamples; ++sample)
        {
            fill_arena(arena, stride, active_len);

            const auto begin = std::chrono::steady_clock::now();
            for (size_t pass = 0; pass < passes; ++pass)
                for (size_t block = 0; block < blocks; ++block)
                    fn(arena.data() + block * stride, c.dis, c.n);
            const auto end = std::chrono::steady_clock::now();

            const double elapsed_ns =
                static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
            samples.push_back(elapsed_ns / static_cast<double>(calls));
        }

        // Checksum once, after all samples (outside timing), for the naive-vs-cyccpy compare.
        const uint64_t final_checksum = checksum_arena(arena, stride, active_len);
        g_sink ^= final_checksum;

        const auto [min_it, max_it] = std::minmax_element(samples.begin(), samples.end());
        double total = 0.0;
        for (const double sample : samples)
            total += sample;

        return Timing{*min_it, total / static_cast<double>(samples.size()), *max_it, final_checksum};
    }
} // namespace

int main()
{
    // --- The grid. Edit these two lists to reshape the sweep. ---
    const std::vector<size_t> dis_values = {
        4, 8, 16, 24,            // < 32: naive can't use 256-bit AVX
        32,                      // vectorization threshold / add_32 == 0 special case
        33, 48, 63,              // store-to-load-forwarding band
        64, 128, 256, 512,       // post-SLF cyclic band
        1024, 4096, 16384, 65536 // large-dis: plain streaming + cyclic-NT route
    };
    const std::vector<size_t> n_values = {
        size_t(1) << 14,  // 16 KB   (L1)
        size_t(1) << 16,  // 64 KB
        size_t(1) << 18,  // 256 KB  (L2)
        size_t(1) << 20,  // 1 MB
        size_t(1) << 22,  // 4 MB
        size_t(1) << 23,  // 8 MB    (L3)
        size_t(1) << 24,  // 16 MB
        size_t(1) << 25,  // 32 MB   (NT threshold)
        size_t(1) << 26,  // 64 MB   (DRAM)
        size_t(1) << 27,  // 128 MB
    };

    // CSV header on stdout.
    std::printf("dis,n,naive_min_ns,naive_avg_ns,naive_max_ns,naive_gbps,"
                "cyc_min_ns,cyc_avg_ns,cyc_max_ns,cyc_gbps,speedup\n");
    std::fflush(stdout);

    const size_t total = dis_values.size() * n_values.size();
    size_t done = 0;

    for (const size_t n : n_values)
    {
        for (const size_t dis : dis_values)
        {
            const Case c{dis, n};
            ++done;
            std::fprintf(stderr, "[%zu/%zu] dis=%zu n=%zu ... ", done, total, dis, n);
            std::fflush(stderr);

            try
            {
                check_case(c);
            }
            catch (const std::exception& e)
            {
                std::fprintf(stderr, "CORRECTNESS FAIL: %s (skipped)\n", e.what());
                continue;
            }

            const Timing naive = run_one(c, bench_naive_loop);
            const Timing cyc = run_one(c, bench_cyccpy);

            if (naive.checksum != cyc.checksum)
            {
                std::fprintf(stderr, "CHECKSUM MISMATCH (skipped)\n");
                continue;
            }

            const double naive_gbps = static_cast<double>(n) / naive.min_ns_per_call;
            const double cyc_gbps = static_cast<double>(n) / cyc.min_ns_per_call;
            const double speedup = naive.min_ns_per_call / cyc.min_ns_per_call;

            std::printf("%zu,%zu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                        dis, n,
                        naive.min_ns_per_call, naive.avg_ns_per_call, naive.max_ns_per_call, naive_gbps,
                        cyc.min_ns_per_call, cyc.avg_ns_per_call, cyc.max_ns_per_call, cyc_gbps,
                        speedup);
            std::fflush(stdout);

            std::fprintf(stderr, "speedup=%.3f  (cyc %.1f GB/s, naive %.1f GB/s)\n",
                         speedup, cyc_gbps, naive_gbps);
        }
    }

    g_sink ^= 0;
    return 0;
}
