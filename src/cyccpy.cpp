#include "misacpy.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <immintrin.h>

namespace misacpy
{
    namespace
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
        static const size_t BENCH_RB = 512;     // Unconditionally use prefix-building for dis in [32, BENCH_RB]
        static const size_t NT_STORE_ENABLE = size_t(1) << (5 + 20);    // We start using NT stores when n >= 32 MB
    }

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

        if (n < NT_STORE_ENABLE and dis > BENCH_RB)
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
        else
        {
            size_t prefix_required = std::max(size_t(32), dis) * 4;

            size_t i = 0;

            if(dis >= size_t(32))
            {
                // Build the prefix
                for (; i + size_t(31) < n and i + dis < prefix_required;
                    i += size_t(32))
                {
                    __m256i reg = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(src + (i + dis)), reg);
                }
                for (; i + size_t(15) < n and i + dis < prefix_required;
                    i += size_t(16))
                {
                    __m128i reg = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(src + (i + dis)), reg);
                }
                for (; i + size_t(3) < n and i + dis < prefix_required;
                    i += size_t(4))
                {
                    uint32_t reg = loadu4(src + i);
                    storeu4(src + (i + dis), reg);
                }
                for (; i < n and i + dis < prefix_required; ++i)
                    src[i + dis] = src[i];
            }
            else
            {
                size_t prefix = dis;
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

            }

            while(uintptr_t(src + (i + dis)) % 32)
            {
                src[i + dis] = src[i];
                ++ i;
            }

            // We now have every required prefix_required byte window in [src, src +
            // prefix_required)
            size_t offset1 = ((i + dis) % dis);

            size_t j = i + 32;
            size_t offset2 = ((j + dis) % dis);

            size_t add_64 = (size_t(64) % dis);

            if(n >= NT_STORE_ENABLE)
            {
                while (i + size_t(63) < n)
                {
                    __m256i reg1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + offset1));
                    __m256i reg2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + (dis << 1) + offset2));

                    // NT stores, switch to this when the data is cold, writes will be streamed directly to DRAM 
                    _mm256_stream_si256(reinterpret_cast<__m256i*>(src + (i + dis)), reg1);
                    _mm256_stream_si256(reinterpret_cast<__m256i*>(src + (j + dis)), reg2);

                    i += size_t(64);
                    j += size_t(64);

                    offset1 += add_64;
                    offset2 += add_64;

                    offset1 -= (offset1 >= dis ? dis : size_t(0));
                    offset2 -= (offset2 >= dis ? dis : size_t(0));
                }
                // NECESSARY WHEN USING NT-STORES!!!
                _mm_sfence();
            }
            else
            {
                while (i + size_t(63) < n)
                {
                    __m256i reg1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + offset1));
                    __m256i reg2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + (dis << 1) + offset2));

                    // Non-NT stores
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(src + (i + dis)), reg1);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(src + (j + dis)), reg2);

                    i += size_t(64);
                    j += size_t(64);

                    offset1 += add_64;
                    offset2 += add_64;

                    offset1 -= (offset1 >= dis ? dis : size_t(0));
                    offset2 -= (offset2 >= dis ? dis : size_t(0));
                }
            }

            for (; i < n; i++)
                src[i + dis] = src[i];
        }
    }

} // namespace misacpy
