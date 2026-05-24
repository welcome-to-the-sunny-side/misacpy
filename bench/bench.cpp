#include "bench_functions.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
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
        uint64_t final_checksum = 0;

        for (int sample = 0; sample < kWarmupSamples; ++sample)
        {
            fill_arena(arena, stride, active_len);

            for (size_t pass = 0; pass < passes; ++pass)
            {
                for (size_t block = 0; block < blocks; ++block)
                {
                    fn(arena.data() + block * stride, c.dis, c.n);
                }
            }

            g_sink ^= checksum_arena(arena, stride, active_len);
        }

        for (int sample = 0; sample < kSamples; ++sample)
        {
            fill_arena(arena, stride, active_len);

            const auto begin = std::chrono::steady_clock::now();
            for (size_t pass = 0; pass < passes; ++pass)
            {
                for (size_t block = 0; block < blocks; ++block)
                {
                    fn(arena.data() + block * stride, c.dis, c.n);
                }
            }
            const auto end = std::chrono::steady_clock::now();

            final_checksum = checksum_arena(arena, stride, active_len);
            g_sink ^= final_checksum;

            const double elapsed_ns =
                static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
            samples.push_back(elapsed_ns / static_cast<double>(calls));
        }

        const auto [min_it, max_it] = std::minmax_element(samples.begin(), samples.end());
        double total = 0.0;
        for (const double sample : samples)
        {
            total += sample;
        }

        return Timing{*min_it, total / static_cast<double>(samples.size()), *max_it, final_checksum};
    }

    void print_result(const Case& c, const Timing& naive, const Timing& custom)
    {
        const double speedup = naive.min_ns_per_call / custom.min_ns_per_call;
        // bytes / nanoseconds == GB/s (SI). Uses min time, like the speedup.
        const double naive_gbps = static_cast<double>(c.n) / naive.min_ns_per_call;
        const double custom_gbps = static_cast<double>(c.n) / custom.min_ns_per_call;

        std::cout << std::setw(6) << c.dis << std::setw(10) << c.n << " | "
                  << std::setw(12) << std::fixed << std::setprecision(2) << naive.min_ns_per_call
                  << std::setw(12) << naive.avg_ns_per_call
                  << std::setw(12) << naive.max_ns_per_call
                  << std::setw(10) << std::setprecision(2) << naive_gbps << " | "
                  << std::setw(12) << std::setprecision(2) << custom.min_ns_per_call
                  << std::setw(12) << custom.avg_ns_per_call
                  << std::setw(12) << custom.max_ns_per_call
                  << std::setw(10) << std::setprecision(2) << custom_gbps << " | "
                  << std::setw(8) << std::setprecision(3) << speedup << '\n';
    }
} // namespace

int main()
{
    const std::vector<Case> cases = {
        {3, 16},
        {3, 1024},
        {3, 1048576},
        
        {8, 16},
        {8, 1024},
        {8, 1048576},

        {11, 16},
        {11, 1024},
        {11, 1048576},

        {16, 16},
        {16, 1024},
        {16, 1048576},

        {31, 64},
        {31, 16384},

        {32, 64},
        {32, 16384},

        {422, 1024},
        {422, 1048576},

        {3453, 1048576},

        {90828, 1048576},
    };

    std::cout << "dis and n are bytes. Each entry is ns/call over " << kSamples
              << " timed samples after " << kWarmupSamples
              << " warmup samples. Speedup uses min naive / min cyccpy.\n";
    std::cout << "Each timed sample runs many calls over independent buffers; buffer reset and checksums are outside "
                 "the timed region.\n\n";
    std::cout << std::setw(6) << "dis" << std::setw(10) << "n" << " | "
              << std::setw(46) << "naive ns/call, GB/s" << " | "
              << std::setw(46) << "cyccpy ns/call, GB/s" << " | "
              << std::setw(8) << "speedup" << '\n';
    std::cout << std::setw(16) << " " << " | "
              << std::setw(12) << "min" << std::setw(12) << "avg" << std::setw(12) << "max"
              << std::setw(10) << "GB/s" << " | "
              << std::setw(12) << "min" << std::setw(12) << "avg" << std::setw(12) << "max"
              << std::setw(10) << "GB/s" << " | "
              << std::setw(8) << "min" << '\n';
    std::cout << std::string(16, '-') << "-+-" << std::string(46, '-') << "-+-"
              << std::string(46, '-') << "-+-" << std::string(8, '-') << '\n';

    std::vector<std::pair<Timing, Timing>> ncs;
    for (const Case& c : cases)
    {
        check_case(c);
        const Timing naive = run_one(c, bench_naive_loop);
        const Timing custom = run_one(c, bench_cyccpy);
        ncs.push_back(std::make_pair(naive, custom));
    }

    for(int i = 0; i < int(cases.size()); i ++)
    {
        auto c = cases[i];
        auto [naive, custom] = ncs[i];
        if (naive.checksum != custom.checksum)
        {
            throw std::runtime_error("benchmark checksum mismatch for dis=" + std::to_string(c.dis)
                                     + " n=" + std::to_string(c.n));
        }
        print_result(c, naive, custom);
    }

    g_sink ^= 0;
    return static_cast<int>(g_sink == 0x12345678ull);
}
