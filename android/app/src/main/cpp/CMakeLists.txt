cmake_minimum_required(VERSION 3.22)
project(Vortex3DAndroid LANGUAGES CXX)

get_filename_component(VORTEX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(VORTEX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VORTEX_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory("${VORTEX_ROOT}" "${CMAKE_BINARY_DIR}/vortex-engine")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(VORTEX_GLSLC_HINTS)
if(DEFINED CMAKE_ANDROID_NDK)
    list(APPEND VORTEX_GLSLC_HINTS "${CMAKE_ANDROID_NDK}/shader-tools")
endif()
if(DEFINED ANDROID_NDK)
    list(APPEND VORTEX_GLSLC_HINTS "${ANDROID_NDK}/shader-tools")
endif()
if(DEFINED ENV{ANDROID_NDK_HOME})
    list(APPEND VORTEX_GLSLC_HINTS "$ENV{ANDROID_NDK_HOME}/shader-tools")
endif()
find_program(VORTEX_GLSLC NAMES glslc glslc.exe HINTS ${VORTEX_GLSLC_HINTS})
if(NOT VORTEX_GLSLC)
    message(FATAL_ERROR "glslc was not found. Install Android NDK shader-tools or expose glslc on PATH.")
endif()

set(VORTEX_SHADER_DIR "${CMAKE_CURRENT_LIST_DIR}/../shaders")
set(VORTEX_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(VORTEX_STAGE1_VERT_SPV "${VORTEX_GENERATED_DIR}/viewport_stage1.vert.spv")
set(VORTEX_STAGE1_FRAG_SPV "${VORTEX_GENERATED_DIR}/viewport_stage1.frag.spv")
set(VORTEX_STAGE1_HEADER "${VORTEX_GENERATED_DIR}/ViewportStage1ShadersGenerated.hpp")

add_custom_command(
    OUTPUT "${VORTEX_STAGE1_VERT_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VORTEX_GENERATED_DIR}"
    COMMAND "${VORTEX_GLSLC}" -O "${VORTEX_SHADER_DIR}/viewport_stage1.vert" -o "${VORTEX_STAGE1_VERT_SPV}"
    DEPENDS "${VORTEX_SHADER_DIR}/viewport_stage1.vert"
    VERBATIM
)
add_custom_command(
    OUTPUT "${VORTEX_STAGE1_FRAG_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VORTEX_GENERATED_DIR}"
    COMMAND "${VORTEX_GLSLC}" -O "${VORTEX_SHADER_DIR}/viewport_stage1.frag" -o "${VORTEX_STAGE1_FRAG_SPV}"
    DEPENDS "${VORTEX_SHADER_DIR}/viewport_stage1.frag"
    VERBATIM
)
add_custom_command(
    OUTPUT "${VORTEX_STAGE1_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/embed_stage1_spirv.py"
            "${VORTEX_STAGE1_VERT_SPV}" "${VORTEX_STAGE1_FRAG_SPV}" "${VORTEX_STAGE1_HEADER}"
    DEPENDS "${VORTEX_STAGE1_VERT_SPV}" "${VORTEX_STAGE1_FRAG_SPV}" "${CMAKE_CURRENT_LIST_DIR}/embed_stage1_spirv.py"
    VERBATIM
)
add_custom_target(vortex_stage1_shaders DEPENDS "${VORTEX_STAGE1_HEADER}")

add_library(vortex_android SHARED
    native_bridge.cpp
    viewport_host.cpp
    vulkan_camera.cpp
    vulkan_device.cpp
    vulkan_geometry.cpp
    vulkan_swapchain.cpp
    vulkan_viewport.cpp
)
add_dependencies(vortex_android vortex_stage1_shaders)
target_include_directories(vortex_android PRIVATE "${VORTEX_GENERATED_DIR}")
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
