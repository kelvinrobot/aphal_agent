add_executable(exotic_benchmarks
    memory_benchmarks.cpp
)

target_link_libraries(exotic_benchmarks PRIVATE exotic::exotic benchmark::benchmark)
exotic_set_warnings(exotic_benchmarks)
