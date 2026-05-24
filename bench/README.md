# misacpy benchmark

Configure and build from the repository root:

```sh
cmake -S bench -B bench/build -DCMAKE_BUILD_TYPE=Release
cmake --build bench/build -j
```

Run:

```sh
bench/build/misacpy_bench
```

The benchmark target is compiled with `-O3 -g -fno-omit-frame-pointer`. It also uses
`-march=native` by default so `src/cyccpy.cpp` can use its AVX implementation and GCC
can generate the best naive-loop version for the local CPU. To disable that and use
plain AVX instead:

```sh
cmake -S bench -B bench/build -DMISACPY_BENCH_MARCH_NATIVE=OFF
```

The two profiled entry points are `bench_cyccpy` and `bench_naive_loop`.

Methodology:

- Each `(dis, n)` case is checked for correctness against the naive loop before timing.
- Each function is measured for 20 samples.
- Before those measured samples, each function gets 3 unmeasured warmup samples.
- Each sample runs many calls over independent buffers, so overlapping writes from one
  call do not feed into the next call.
- Buffer reset and result checksums happen outside the timed region.
- The table reports min, average, and max ns/call. The speedup column uses the min
  timing: `naive_min / cyccpy_min`.
