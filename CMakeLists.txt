cmake_minimum_required(VERSION 3.22)

project(Vortex3D
    VERSION 0.1.0
    DESCRIPTION "Vortex3D native-first modeling engine"
    LANGUAGES CXX
)

option(VORTEX_BUILD_TESTS "Build Vortex3D native tests" ON)
option(VORTEX_BUILD_BENCHMARKS "Build Vortex3D native benchmarks" OFF)
option(VORTEX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(VORTEX_EXPECT_POINTER_BITS "" CACHE STRING "Expected target pointer width for portability validation")

if(VORTEX_EXPECT_POINTER_BITS)
    math(EXPR VORTEX_POINTER_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
    if(NOT VORTEX_POINTER_BITS EQUAL VORTEX_EXPECT_POINTER_BITS)
        message(FATAL_ERROR
            "Vortex3D target pointer width mismatch: expected ${VORTEX_EXPECT_POINTER_BITS}, got ${VORTEX_POINTER_BITS}")
    endif()
endif()

add_library(vortex_core STATIC
    src/core/command.cpp
    src/core/document.cpp
    src/mesh/editable_mesh.cpp
    src/mesh/extrude.cpp
    src/mesh/command.cpp
)

add_library(Vortex3D::core ALIAS vortex_core)

target_include_directories(vortex_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(vortex_core PUBLIC cxx_std_20)
set_target_properties(vortex_core PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(vortex_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(vortex_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(VORTEX_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(vortex_core PRIVATE /WX)
    else()
        target_compile_options(vortex_core PRIVATE -Werror)
    endif()
endif()

if(VORTEX_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(vortex_core_smoke tests/core_smoke.cpp)
    target_link_libraries(vortex_core_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_core_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.core.smoke COMMAND vortex_core_smoke)

    add_executable(vortex_document_history_smoke tests/document_history_smoke.cpp)
    target_link_libraries(vortex_document_history_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_document_history_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.document.history.smoke COMMAND vortex_document_history_smoke)

    add_executable(vortex_mesh_kernel_smoke tests/mesh_kernel_smoke.cpp)
    target_link_libraries(vortex_mesh_kernel_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_kernel_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.kernel.smoke COMMAND vortex_mesh_kernel_smoke)

    add_executable(vortex_mesh_mutation_smoke tests/mesh_mutation_smoke.cpp)
    target_link_libraries(vortex_mesh_mutation_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_mutation_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.mutation.smoke COMMAND vortex_mesh_mutation_smoke)

    add_executable(vortex_mesh_extrude_smoke tests/mesh_extrude_smoke.cpp)
    target_link_libraries(vortex_mesh_extrude_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_extrude_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.extrude.smoke COMMAND vortex_mesh_extrude_smoke)

    add_executable(vortex_mesh_randomized_smoke tests/mesh_randomized_smoke.cpp)
    target_link_libraries(vortex_mesh_randomized_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_randomized_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.randomized.smoke COMMAND vortex_mesh_randomized_smoke)

    add_executable(vortex_donor_contract_smoke tests/donor_contract_smoke.cpp)
    target_link_libraries(vortex_donor_contract_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_donor_contract_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.donor.contract.smoke COMMAND vortex_donor_contract_smoke)

    add_executable(vortex_mesh_command_smoke tests/mesh_command_smoke.cpp)
    target_link_libraries(vortex_mesh_command_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_command_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.command.smoke COMMAND vortex_mesh_command_smoke)

    add_executable(vortex_mesh_topology_history_smoke tests/mesh_topology_history_smoke.cpp)
    target_link_libraries(vortex_mesh_topology_history_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_topology_history_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.topology.history.smoke COMMAND vortex_mesh_topology_history_smoke)

    add_executable(vortex_document_mesh_integration_smoke tests/document_mesh_integration_smoke.cpp)
    target_link_libraries(vortex_document_mesh_integration_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_document_mesh_integration_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.document.mesh.integration.smoke COMMAND vortex_document_mesh_integration_smoke)
endif()

if(VORTEX_BUILD_BENCHMARKS)
    add_executable(vortex_mesh_bench benchmarks/mesh_bench.cpp)
    target_link_libraries(vortex_mesh_bench PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_bench PRIVATE cxx_std_20)
    set_target_properties(vortex_mesh_bench PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(vortex_mesh_bench PRIVATE /W4 /permissive-)
        if(VORTEX_WARNINGS_AS_ERRORS)
            target_compile_options(vortex_mesh_bench PRIVATE /WX)
        endif()
    else()
        target_compile_options(vortex_mesh_bench PRIVATE -Wall -Wextra -Wpedantic)
        if(VORTEX_WARNINGS_AS_ERRORS)
            target_compile_options(vortex_mesh_bench PRIVATE -Werror)
        endif()
    endif()
endif()
