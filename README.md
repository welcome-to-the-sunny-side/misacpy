This repository provides a faster implementation of the following function (as compared to GCC and Clang's assembly for it at `-O3 -march=native`, which enables AVX2 on my machine):

```cpp
void f(uint8_t* src, size_t dis, size_t n)
{
    for(size_t i = 0; i < n; i ++)
        src[i + dis] = src[i];
}
```

My implementation (`misacpy::cyccpy`) outperforms the naive assembly by GCC and Clang (`-O3 -march=native`) at all `dis <= 200`, with diminishing returns for increasing `dis`.

Most of the performance gains are due to the following reasons:

- At `dis < 32`, both GCC and Clang are not able to vectorize the loop with 256 bit avx registers.
- At smaller distances, naive vectorization struggles because of Store-To-Load Forwarding stalls (prefixes/suffixes of the 32 byte window starting at `src + (i + dis)` are read soon after `sr[i + dis, i + dis + 32)` is written for smaller `dis`, and the latter may not have been flushed from the store-buffer to L1 by then). My implementation almost completely eliminates these from the hot loop.

Some results on my i7-14650HX CPU (benchmark script is at scripts/quiet-run.sh):

```
dis and n are bytes. Each entry is ns/call over 20 timed samples after 3 warmup samples. Speedup uses min naive / min cyccpy.

   dis         n |                            naive ns/call, GB/s |                           cyccpy ns/call, GB/s |  speedup
                 |          min         avg         max      GB/s |          min         avg         max      GB/s |      min
-----------------+------------------------------------------------+------------------------------------------------+---------
     5     10000 |      5479.68     5486.08     5517.20      1.82 |       645.44      652.28      690.30     15.49 |    8.490
    10     10000 |      3968.04     3979.46     3993.54      2.52 |       586.43      595.65      637.08     17.05 |    6.766
    20     10000 |      3217.90     3234.50     3258.41      3.11 |       496.20      508.07      543.72     20.15 |    6.485
    32     10000 |       874.61      883.72      914.05     11.43 |       548.03      566.32      642.76     18.25 |    1.596
    33     10000 |      2933.48     2939.27     2958.39      3.41 |       556.77      569.18      674.12     17.96 |    5.269
    36     10000 |      2833.05     2839.76     2861.35      3.53 |       558.77      581.96      793.44     17.90 |    5.070
    50     10000 |      2824.70     2831.24     2848.09      3.54 |       557.83      602.30      852.84     17.93 |    5.064
   100     10000 |       967.91      987.03     1028.14     10.33 |       557.61      565.53      596.08     17.93 |    1.736
   200     10000 |       674.22      690.70      765.03     14.83 |       561.76      568.70      593.27     17.80 |    1.200
   300     10000 |       590.12      637.93      880.94     16.95 |       560.74      576.11      692.24     17.83 |    1.052
   400     10000 |       546.87      584.00      673.90     18.29 |       501.42      517.19      530.97     19.94 |    1.091
   500     10000 |       572.42      603.58      682.06     17.47 |       589.19      604.63      703.09     16.97 |    0.972
  2000     10000 |       603.68      633.61      810.08     16.57 |       591.34      633.35      733.16     16.91 |    1.021
     5   1048576 |    575041.75   575438.82   577688.56      1.82 |     56781.19    57499.85    60510.81     18.47 |   10.127
    10   1048576 |    414700.19   419206.90   425001.81      2.53 |     56456.69    57743.68    61259.06     18.57 |    7.345
    20   1048576 |    322817.44   330661.65   339227.94      3.25 |     56502.19    57576.72    63854.62     18.56 |    5.713
    32   1048576 |    100323.62   100844.69   102128.62     10.45 |     56611.31    58522.50    72910.31     18.52 |    1.772
    33   1048576 |    297221.94   297882.21   299668.44      3.53 |     56890.38    60864.79    80879.88     18.43 |    5.224
    36   1048576 |    297259.00   297799.24   298832.50      3.53 |     56679.69    57410.10    60134.94     18.50 |    5.245
    50   1048576 |    297334.88   298143.86   299805.62      3.53 |     56464.06    57631.88    63401.50     18.57 |    5.266
   100   1048576 |    101384.75   102646.63   107833.44     10.34 |     56923.88    57702.68    61104.44     18.42 |    1.781
   200   1048576 |     63099.81    65738.74    74598.44     16.62 |     56932.75    62148.75    83892.19     18.42 |    1.108
   300   1048576 |     60700.06    74175.88   125486.44     17.27 |     59078.81    64006.69    79053.06     17.75 |    1.027
   400   1048576 |     61321.81    68407.52    86670.31     17.10 |     59768.44    61788.31    75038.06     17.54 |    1.026
   500   1048576 |     59892.12    64745.95    80831.00     17.51 |     59507.50    60379.54    62745.38     17.62 |    1.006
  2000   1048576 |     55173.56    58390.85    76878.38     19.01 |     53197.94    57090.43    66444.56     19.71 |    1.037
```

## Requirements

- Linux (for `scripts/quiet-run.sh`'s `/sys` paths)
- A C++17 compiler (GCC or Clang)
- CMake >= 3.16
- A CPU with AVX2 support
- `sudo` access (used by `scripts/quiet-run.sh` to lock cpufreq governor and turbo for stable benchmarking)

To benchmark the loop on your device, clone the repository, `cd` into it, and run the following commands:

```bash
cmake -S bench -B bench/build
cmake --build bench/build
# Uses sudo to lock cpufreq governor and turbo, then runs the bench pinned to one core.
./scripts/quiet-run.sh ./bench/build/misacpy_bench
```