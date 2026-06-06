#include "misacpy.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace misacpy
{
    // Helpers
    uint32_t loadu4(const uint8_t* ptr)
    {
        uint32_t v;
        memcpy(&v, ptr, 4);
        return v;
    }
    void storeu4(uint8_t* ptr, const uint32_t val)
    {
        memcpy(ptr, &val, 4);
    }
    uint64_t loadu8(const uint8_t* ptr)
    {
        uint64_t v;
        memcpy(&v, ptr, 8);
        return v;
    }
    void storeu8(uint8_t* ptr, const uint64_t val)
    {
        memcpy(ptr, &val, 8);
    }

    static const size_t TINY = 128;
    static const size_t BENCH_RB = 512;

    // Performs `for(int i = 0; i < n; i ++) src[i + dis] = src[i]` but fast (^_^)
    // Requires positive `dis`
    void cyccpy(uint8_t* src, const size_t dis, const size_t n)
    {
        if (n + dis <= TINY) [[gnu::likely]]
        {
            for (size_t i = 0; i < n; i++)
                src[i + dis] = src[i];
            return;
        }

        if (dis > BENCH_RB)
        {
            size_t i = 0;
            for (; i + size_t(31) < n; i += size_t(32))
            {
                __m256i reg = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(src + (i + dis)), reg);
            }
            for (; i + size_t(15) < n; i += size_t(16))
            {
                __m128i reg = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(src + (i + dis)), reg);
            }
            for (; i + size_t(3) < n; i += size_t(4))
            {
                uint32_t reg = loadu4(src + i);
                storeu4(src + (i + dis), reg);
            }
            for (; i < n; ++i)
                src[i + dis] = src[i];
        }
        else if (dis >= size_t(32))
        {
            size_t prefix_required = dis * 2;

            size_t i = 0;
            size_t prefix = dis;

            // Build the prefix
            for (; i + size_t(31) < n and prefix < prefix_required;
                 i += size_t(32), prefix += size_t(32))
            {
                __m256i reg = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(src + (i + dis)), reg);
            }
            for (; i + size_t(15) < n and prefix < prefix_required;
                 i += size_t(16), prefix += size_t(16))
            {
                __m128i reg = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(src + (i + dis)), reg);
            }
            for (; i + size_t(3) < n and prefix < prefix_required;
                 i += size_t(4), prefix += size_t(4))
            {
                uint32_t reg = loadu4(src + i);
                storeu4(src + (i + dis), reg);
            }
            for (; i < n and prefix < prefix_required; ++i, prefix++)
                src[i + dis] = src[i];

            // We now have every required prefix_required byte window in [src, src +
            // prefix_required)
            size_t offset = (prefix % dis);
            size_t add_32 = (size_t(32) % dis);
            while (i + size_t(31) < n)
            {
                __m256i reg = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + offset));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(src + (i + dis)), reg);

                i += size_t(32);

                offset += add_32;
                offset -= (offset >= dis ? dis : size_t(0));
            }
            for (; i < n; i++)
                src[i + dis] = src[i];
        }
        else
        {
            // Required prefix length before we can use vectorization (2 * avx register
            // width)
            static constexpr size_t prefix_required = 32 + 32;

            size_t prefix = dis;
            size_t i = 0;

            if (dis >= size_t(4))
            {
                for (; prefix < prefix_required and i + size_t(3) < n;
                     prefix += size_t(4), i += size_t(4))
                {
                    uint32_t reg = loadu4(src + i);
                    storeu4(src + (i + dis), reg);
                }
            }
            for (; prefix < prefix_required and i < n; ++i, ++prefix)
                src[i + dis] = src[i];

            if (i == n)
                return;

            // We now have every required prefix_required byte window in [src, src +
            // prefix_required)
            size_t offset = (prefix % dis);
            size_t add_32 = (size_t(32) % dis);

            while (i + size_t(31) < n)
            {
                __m256i reg = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + offset));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(src + (i + dis)), reg);

                i += size_t(32);

                offset += add_32;
                offset -= (offset >= dis ? dis : size_t(0));
            }
            for (; i < n; i++)
                src[i + dis] = src[i];
        }
    }

} // namespace misacpy
