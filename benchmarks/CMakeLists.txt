# Micro-benchmarks (docs/architecture/memory-and-performance.md#benchmarking-policy): Catch2 BENCHMARK programs,
# built with TESERRA_BUILD_BENCHMARKS=ON in Release (preset windows-msvc-bench). They are not tests: run them
# by hand or in the non-blocking bench workflow, and compare numbers only from the same machine.
find_package(Catch2 3 CONFIG REQUIRED)

add_executable(teserra_ecs_bench ecs/ecs_bench.cpp)
target_link_libraries(teserra_ecs_bench PRIVATE teserra_ecs Catch2::Catch2WithMain)
target_compile_features(teserra_ecs_bench PRIVATE cxx_std_20)
teserra_set_warnings(teserra_ecs_bench)

add_executable(teserra_core_bench core/linear_arena_bench.cpp)
target_link_libraries(teserra_core_bench PRIVATE teserra_core Catch2::Catch2WithMain)
target_compile_features(teserra_core_bench PRIVATE cxx_std_20)
teserra_set_warnings(teserra_core_bench)
