add_library(exotic STATIC
    memory/arena.cpp
    memory/tracking_allocator.cpp
)

add_library(exotic::exotic ALIAS exotic)

target_include_directories(exotic
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(exotic PUBLIC Threads::Threads)

target_compile_features(exotic PUBLIC cxx_std_20)

exotic_set_warnings(exotic)
exotic_enable_sanitizers(exotic)
