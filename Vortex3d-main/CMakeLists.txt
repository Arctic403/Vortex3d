cmake_minimum_required(VERSION 3.22)

project(Vortex3D
    VERSION 0.2.0
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
    src/core/editor_history.cpp
    src/mesh/editable_mesh.cpp
    src/mesh/validation.cpp
    src/mesh/extrude.cpp
    src/mesh/command.cpp
    src/geometry/operations.cpp
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

add_library(vortex_eval STATIC
    src/eval/evaluation_cache.cpp
    src/eval/evaluator.cpp
    src/eval/modifier.cpp
    src/eval/normals.cpp
    src/eval/triangulate.cpp
    src/eval/validator.cpp
)

add_library(Vortex3D::eval ALIAS vortex_eval)

target_link_libraries(vortex_eval PUBLIC Vortex3D::core)
target_include_directories(vortex_eval
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(vortex_eval PUBLIC cxx_std_20)
set_target_properties(vortex_eval PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(vortex_eval PRIVATE /W4 /permissive-)
else()
    target_compile_options(vortex_eval PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(VORTEX_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(vortex_eval PRIVATE /WX)
    else()
        target_compile_options(vortex_eval PRIVATE -Werror)
    endif()
endif()

add_library(vortex_engine STATIC
    src/editor/context.cpp
    src/editor/operator.cpp
    src/tool/tool_session.cpp
    src/graph/dependency_graph.cpp
    src/project/project_io.cpp
    src/viewport/render_extract.cpp
    src/procedural/geometry_graph.cpp
    src/ext/registry.cpp
)

add_library(Vortex3D::engine ALIAS vortex_engine)
target_link_libraries(vortex_engine PUBLIC Vortex3D::eval)
target_include_directories(vortex_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(vortex_engine PUBLIC cxx_std_20)
set_target_properties(vortex_engine PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(vortex_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(vortex_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(VORTEX_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(vortex_engine PRIVATE /WX)
    else()
        target_compile_options(vortex_engine PRIVATE -Werror)
    endif()
endif()

if(VORTEX_BUILD_TESTS)
    include(CTest)
    enable_testing()

    # Test builds expose narrow friend hooks for deliberate corruption fixtures.
    # This definition is absent from production/Android/benchmark-only builds.
    target_compile_definitions(vortex_core PUBLIC VORTEX_ENABLE_TEST_HOOKS=1)

    add_executable(vortex_core_smoke tests/core_smoke.cpp)
    target_link_libraries(vortex_core_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_core_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.core.smoke COMMAND vortex_core_smoke)

    add_executable(vortex_document_history_smoke tests/document_history_smoke.cpp)
    target_link_libraries(vortex_document_history_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_document_history_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.document.history.smoke COMMAND vortex_document_history_smoke)

    add_executable(vortex_editor_history_smoke tests/editor_history_smoke.cpp)
    target_link_libraries(vortex_editor_history_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_editor_history_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.editor.history.smoke COMMAND vortex_editor_history_smoke)

    add_executable(vortex_change_journal_smoke tests/change_journal_smoke.cpp)
    target_link_libraries(vortex_change_journal_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_change_journal_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.document.change.journal.smoke COMMAND vortex_change_journal_smoke)

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

    add_executable(vortex_mesh_revision_semantics_smoke tests/mesh_revision_semantics_smoke.cpp)
    target_link_libraries(vortex_mesh_revision_semantics_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_mesh_revision_semantics_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.revision.semantics.smoke COMMAND vortex_mesh_revision_semantics_smoke)

    add_executable(vortex_mesh_validation_corruption_smoke tests/mesh_validation_corruption_smoke.cpp)
    target_link_libraries(vortex_mesh_validation_corruption_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_validation_corruption_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.validation.corruption.smoke COMMAND vortex_mesh_validation_corruption_smoke)

    add_executable(vortex_mesh_validation_storage_smoke tests/mesh_validation_storage_smoke.cpp)
    target_link_libraries(vortex_mesh_validation_storage_smoke PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_validation_storage_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.mesh.validation.storage.smoke COMMAND vortex_mesh_validation_storage_smoke)

    add_executable(vortex_evaluator_smoke tests/evaluator_smoke.cpp)
    target_link_libraries(vortex_evaluator_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_evaluator_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.smoke COMMAND vortex_evaluator_smoke)

    add_executable(vortex_evaluation_cache_smoke tests/evaluation_cache_smoke.cpp)
    target_link_libraries(vortex_evaluation_cache_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_evaluation_cache_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.cache.smoke COMMAND vortex_evaluation_cache_smoke)

    add_executable(vortex_modifier_smoke tests/modifier_smoke.cpp)
    target_link_libraries(vortex_modifier_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_modifier_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.modifier.smoke COMMAND vortex_modifier_smoke)

    add_executable(vortex_mirror_modifier_smoke tests/mirror_modifier_smoke.cpp)
    target_link_libraries(vortex_mirror_modifier_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_mirror_modifier_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.mirror.smoke COMMAND vortex_mirror_modifier_smoke)

    add_executable(vortex_mirror_weld_smoke tests/mirror_weld_smoke.cpp)
    target_link_libraries(vortex_mirror_weld_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_mirror_weld_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.mirror.weld.smoke COMMAND vortex_mirror_weld_smoke)

    add_executable(vortex_triangulate_modifier_smoke tests/triangulate_modifier_smoke.cpp)
    target_link_libraries(vortex_triangulate_modifier_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_triangulate_modifier_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.triangulate.smoke COMMAND vortex_triangulate_modifier_smoke)

    add_executable(vortex_derived_normals_smoke tests/derived_normals_smoke.cpp)
    target_link_libraries(vortex_derived_normals_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_derived_normals_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.normals.smoke COMMAND vortex_derived_normals_smoke)

    add_executable(vortex_evaluated_mesh_validation_smoke tests/evaluated_mesh_validation_smoke.cpp)
    target_link_libraries(vortex_evaluated_mesh_validation_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_evaluated_mesh_validation_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.validation.smoke COMMAND vortex_evaluated_mesh_validation_smoke)

    add_executable(vortex_evaluation_randomized_smoke tests/evaluation_randomized_smoke.cpp)
    target_link_libraries(vortex_evaluation_randomized_smoke PRIVATE Vortex3D::eval)
    target_compile_features(vortex_evaluation_randomized_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.eval.randomized.smoke COMMAND vortex_evaluation_randomized_smoke)

    add_executable(vortex_architecture_v02_smoke tests/architecture_v02_smoke.cpp)
    target_link_libraries(vortex_architecture_v02_smoke PRIVATE Vortex3D::engine)
    target_compile_features(vortex_architecture_v02_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.architecture.v02.smoke COMMAND vortex_architecture_v02_smoke)

    add_executable(vortex_project_format_v01_smoke tests/project_format_v01_smoke.cpp)
    target_link_libraries(vortex_project_format_v01_smoke PRIVATE Vortex3D::engine)
    target_compile_features(vortex_project_format_v01_smoke PRIVATE cxx_std_20)
    add_test(NAME vortex.project.format.v01.smoke COMMAND vortex_project_format_v01_smoke)
endif()

if(VORTEX_BUILD_BENCHMARKS)
    add_executable(vortex_mesh_bench benchmarks/mesh_bench.cpp)
    target_link_libraries(vortex_mesh_bench PRIVATE Vortex3D::core)
    target_compile_features(vortex_mesh_bench PRIVATE cxx_std_20)
    set_target_properties(vortex_mesh_bench PROPERTIES CXX_EXTENSIONS OFF)

    add_executable(vortex_eval_bench benchmarks/eval_bench.cpp)
    target_link_libraries(vortex_eval_bench PRIVATE Vortex3D::eval)
    target_compile_features(vortex_eval_bench PRIVATE cxx_std_20)
    set_target_properties(vortex_eval_bench PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(vortex_mesh_bench PRIVATE /W4 /permissive-)
        target_compile_options(vortex_eval_bench PRIVATE /W4 /permissive-)
        if(VORTEX_WARNINGS_AS_ERRORS)
            target_compile_options(vortex_mesh_bench PRIVATE /WX)
            target_compile_options(vortex_eval_bench PRIVATE /WX)
        endif()
    else()
        target_compile_options(vortex_mesh_bench PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(vortex_eval_bench PRIVATE -Wall -Wextra -Wpedantic)
        if(VORTEX_WARNINGS_AS_ERRORS)
            target_compile_options(vortex_mesh_bench PRIVATE -Werror)
            target_compile_options(vortex_eval_bench PRIVATE -Werror)
        endif()
    endif()
endif()
