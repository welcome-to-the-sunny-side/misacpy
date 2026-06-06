#include <bits/stdc++.h>
#include "misacpy.h"

using namespace std;

struct random : std::mt19937
{
    using std::mt19937::mt19937;
    using std::mt19937::operator();
    static int64_t gen_seed()
    {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }
    random() : std::mt19937(gen_seed()) {}
    template <class Int>
    auto operator()(Int a, Int b)
        -> std::enable_if_t<std::is_integral_v<Int>, Int>
    {
        return std::uniform_int_distribution<Int>(a, b)(*this);
    }
    template <class Int>
    auto operator()(Int a) -> std::enable_if_t<std::is_integral_v<Int>, Int>
    {
        return std::uniform_int_distribution<Int>(0, a - 1)(*this);
    }
    template <class Real>
    auto operator()(Real a, Real b)
        -> std::enable_if_t<std::is_floating_point_v<Real>, Real>
    {
        return std::uniform_real_distribution<Real>(a, b)(*this);
    }
}rng;

void cpy(uint8_t *src, size_t dis, size_t n)
{
    for(int i = 0; i < n; i ++)
        src[i + dis] = src[i];
}

__attribute__((noinline, noipa))
void do_once(uint8_t *src, size_t dis, size_t n, auto fn)
{
    fn(src, dis, n);
}

void (* fn1) (uint8_t *src, size_t dis, size_t n) = cpy;
void (* fn2) (uint8_t *src, size_t dis, size_t n) = misacpy::cyccpy;

const int T = 20;

signed main()
{
    vector<array<size_t, 2>> cases =
    {
        {3, (1 << 20)},
        {5, (1 << 20)},
        {10, (1 << 20)},
        {20, (1 << 20)},
        {30, (1 << 20)},
        {32, (1 << 20)},
        {40, (1 << 20)},
        {50, (1 << 20)},
        {75, (1 << 20)},
        {100, (1 << 20)},
        {150, (1 << 20)},
        {200, (1 << 20)},
        {1000, (1 << 20)},
    };

    volatile uint8_t sink = 0;

    for(auto [dis, n] : cases)
    {
        uint8_t* arr = (uint8_t*)malloc(n + dis);

        uint64_t mn1 = UINT64_MAX;
        for(int i = 0; i < T; i ++)
        {
            for(int i = 0; i < dis; i ++)
                arr[i] = uint8_t(rng());
            auto l = std::chrono::steady_clock::now();
            do_once(arr, dis, n, fn1);
            auto r = std::chrono::steady_clock::now();
            mn1 = min(mn1, uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(r - l).count()));
            sink ^= arr[dis + n - 1];
        }

        uint64_t mn2 = UINT64_MAX;
        for(int i = 0; i < T; i ++)
        {
            for(int i = 0; i < dis; i ++)
                arr[i] = uint8_t(rng());
            auto l = std::chrono::steady_clock::now();
            do_once(arr, dis, n, fn2);
            auto r = std::chrono::steady_clock::now();
            mn2 = min(mn2, uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(r - l).count()));
            sink ^= arr[dis + n - 1];
        }

        cout << "[dis, n] = [" << dis << ", " << n << "] ; naive = " << mn1 << ", misa = " << mn2 << "; speedup = " << double(mn1)/double(mn2) << endl;
    }
}