# Vortex3D v0.2 Deep Hardening Audit

## Purpose

This pass freezes the v0.2 architecture before Object Mode, Android project filing, and import/export work. It does not add product features; it hardens trust boundaries and renderer/project semantics that later layers will depend on.

## Document and mesh trust boundaries

`Document::validate()` now rejects allocator rewind/wrap hazards, impossible revisions, malformed collection ancestry, collection cycles, cross-scene ancestry, and non-strict authored meshes. `Document::createMesh()` also requires strict mesh validation. `EditableMesh::validateStrict()` rejects a serialized allocator at `UINT64_MAX`, preventing a hostile or corrupted file from causing immediate stable-ID wrap on the next allocation.

Serialized attribute replacement now verifies that both stored values and default values match the declared attribute type. This prevents a malformed layer from loading successfully and failing later when topology growth tries to materialize defaults.

## Project format hardening

Schema v1 remains the current project schema and the payload layout is unchanged. The decoder now preflights element byte ranges before allocating vectors, rejects duplicate persistent IDs and duplicate collection membership IDs, rejects non-canonical serialized booleans, and relies on the strengthened final Document/mesh validation before exposing loaded authored state.

The schema's raw scalar assumptions are now explicit compile-time requirements: little-endian, one-byte bool, IEEE-754 32-bit float, and tightly packed Vortex vector scalar layouts. Current Android ARMv7/ARM64 and host CI targets satisfy these requirements.

The documented FNV-1a 64-bit checksum now uses the standard offset basis for newly written files. The reader also accepts the earlier v0.2 schema-v1 legacy checksum offset so existing preview files remain readable.

### 32-bit save-memory path

Project encoding no longer constructs a complete payload buffer and then copies it into a second complete output buffer. It writes the header and payload into one output vector, computes the checksum over the payload span, then patches the fixed-size header fields in place.

Mesh serialization also reads topology and attribute storage through const views instead of building complete `EditableMeshSerializedState` and attribute-layer snapshots first. These changes preserve schema bytes while reducing avoidable peak host memory during saves, which is especially important for `armeabi-v7a`.

## Dependency/procedural graph correctness

Dirty propagation now always walks the full downstream dependency set even when an intermediate node is already dirty. Adding or removing an edge dirties the affected downstream computation because graph topology itself changed.

The v0.2 `GeometryGraph` is explicitly a linear modifier-chain abstraction. It now rejects fan-in/fan-out connections whose semantics are not implemented yet, and evaluation uses only the dependency ancestry of the selected output. Unrelated graph nodes therefore cannot silently affect an output merely because they appeared earlier in global topological order.

## Viewport extraction correctness

Renderer-facing extraction now preserves evaluated Corner normals. Render vertices are split only when the same evaluated position is used with a different Corner normal, so smooth shared vertices remain compact while hard/flat shading receives the necessary renderer-side vertex duplication.

A flat cube regression fixture now produces 24 renderer vertices and 12 triangles rather than collapsing its six face-normal groups onto eight incorrectly smoothed shared vertices. Invalid/missing/non-finite Corner normals fail extraction explicitly.

## Release/compiler cleanup

Editor/mesh history command paths move `MeshCommandResult` values to callers rather than copying nested optional topology-result vectors. Besides avoiding unnecessary copies, this keeps GCC Release `-O3 -Werror` builds free from a libstdc++ optional/vector maybe-uninitialized diagnostic without suppressing warnings.

The top-level CMake project version is now `0.2.0`, matching the engine architecture and Android application version metadata.

## Regression gates

The hardening additions extend the existing 26 native suites rather than creating a parallel test track. New checks cover allocator rewind/wrap, collection cycles, malformed attribute defaults, legacy checksum compatibility, dirty propagation after partial cleaning, dependency-edge invalidation, isolated procedural nodes, rejected unsupported graph branching, and split-normal viewport extraction.

Local validation for this audit includes Clang and GCC warnings-as-errors builds/tests, ASan + UBSan with leak detection, Release benchmark smoke, portable-boundary policy, and repository policy. `clang-tidy` and the Android NDK are not installed in the local audit environment; GitHub CI remains the authoritative static-analysis and ARMv7/ARM64 cross-compile gate.
