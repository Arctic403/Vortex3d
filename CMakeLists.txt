cmake_minimum_required(VERSION 3.22)

project(Vortex3D
    VERSION 0.1.0
    DESCRIPTION "Vortex3D native-first modeling engine"
    LANGUAGES CXX
)

option(VORTEX_BUILD_TESTS "Build Vortex3D native tests" ON)
option(VORTEX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

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

    add_executable(vortex_core_smoke
        tests/core_smoke.cpp
    )
    target_link_libraries(vortex_core_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_core_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.core.smoke COMMAND vortex_core_smoke)

    add_executable(vortex_mesh_kernel_smoke
        tests/mesh_kernel_smoke.cpp
    )
    target_link_libraries(vortex_mesh_kernel_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_kernel_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.mesh.kernel.smoke COMMAND vortex_mesh_kernel_smoke)

    add_executable(vortex_mesh_mutation_smoke
        tests/mesh_mutation_smoke.cpp
    )
    target_link_libraries(vortex_mesh_mutation_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_mutation_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.mesh.mutation.smoke COMMAND vortex_mesh_mutation_smoke)

    add_executable(vortex_mesh_extrude_smoke
        tests/mesh_extrude_smoke.cpp
    )
    target_link_libraries(vortex_mesh_extrude_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_extrude_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.mesh.extrude.smoke COMMAND vortex_mesh_extrude_smoke)

    add_executable(vortex_mesh_randomized_smoke
        tests/mesh_randomized_smoke.cpp
    )
    target_link_libraries(vortex_mesh_randomized_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_randomized_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.mesh.randomized.smoke COMMAND vortex_mesh_randomized_smoke)

    add_executable(vortex_donor_contract_smoke
        tests/donor_contract_smoke.cpp
    )
    target_link_libraries(vortex_donor_contract_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_donor_contract_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.donor.contract.smoke COMMAND vortex_donor_contract_smoke)

    add_executable(vortex_mesh_command_smoke
        tests/mesh_command_smoke.cpp
    )
    target_link_libraries(vortex_mesh_command_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_command_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.mesh.command.smoke COMMAND vortex_mesh_command_smoke)

    add_executable(vortex_mesh_topology_history_smoke
        tests/mesh_topology_history_smoke.cpp
    )
    target_link_libraries(vortex_mesh_topology_history_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_topology_history_smoke PRIVATE cxx_std_20)

    add_test(NAME vortex.mesh.topology.history.smoke COMMAND vortex_mesh_topology_history_smoke)
endif()
