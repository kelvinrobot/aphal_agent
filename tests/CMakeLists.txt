add_executable(exotic_tests
    memory/arena_test.cpp
    memory/object_pool_test.cpp
    memory/ref_counted_test.cpp
    memory/tracking_allocator_test.cpp
)

target_link_libraries(exotic_tests PRIVATE exotic::exotic Catch2::Catch2WithMain)

exotic_set_warnings(exotic_tests)
exotic_enable_sanitizers(exotic_tests)

include(CTest)
include(Catch)
catch_discover_tests(exotic_tests)
