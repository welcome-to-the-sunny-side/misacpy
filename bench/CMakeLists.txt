cmake_minimum_required(VERSION 3.16)

project(misacpy_bench LANGUAGES CXX)

option(MISACPY_BENCH_MARCH_NATIVE "Compile the benchmark with -march=native" ON)

add_executable(misacpy_bench
    bench.cpp
    bench_functions.cpp
    ../src/cyccpy.cpp
)

add_executable(probe
    probe.cpp
    bench_functions.cpp
    ../src/cyccpy.cpp
)

foreach(target misacpy_bench probe)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

    target_compile_features(${target} PRIVATE cxx_std_17)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -O3
            -g
            -fno-omit-frame-pointer
            -Wall
            -Wextra
            -Wpedantic
        )

        if(MISACPY_BENCH_MARCH_NATIVE)
            target_compile_options(${target} PRIVATE -march=native)
        else()
            target_compile_options(${target} PRIVATE -mavx)
        endif()
    endif()
endforeach()
