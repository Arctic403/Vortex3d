cmake_minimum_required(VERSION 3.22)
project(Vortex3DAndroid LANGUAGES CXX)

get_filename_component(VORTEX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(VORTEX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VORTEX_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory("${VORTEX_ROOT}" "${CMAKE_BINARY_DIR}/vortex-engine")

add_library(vortex_android SHARED
    native_bridge.cpp
    vulkan_device.cpp
    vulkan_swapchain.cpp
    vulkan_viewport.cpp
)
target_link_libraries(vortex_android PRIVATE Vortex3D::engine log android vulkan)
target_compile_features(vortex_android PRIVATE cxx_std_20)
target_compile_definitions(vortex_android PRIVATE VK_USE_PLATFORM_ANDROID_KHR=1)
set_target_properties(vortex_android PROPERTIES CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vortex_android PRIVATE -Wall -Wextra -Wpedantic)
    if(VORTEX_WARNINGS_AS_ERRORS)
        target_compile_options(vortex_android PRIVATE -Werror)
    endif()
endif()
