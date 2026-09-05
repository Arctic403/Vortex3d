# Vortex3D Native

Vortex3D is a native-first, mobile-first 3D modeling engine and editor.

This repository is the clean C++ foundation for Vortex3D Native. The previous `Vortex3dGm` project remains a donor/reference implementation; browser-specific infrastructure is not the engine foundation.

## Core direction

- C++20 portable modeling core with no UI, browser, renderer, or Android framework dependency.
- Durable document/scene architecture designed for authoring rather than a game-runtime ECS.
- Stable typed 64-bit IDs for persistent document and topology identity.
- Editable polygon mesh kernel with Vertex / Edge / Face / Corner topology and radial connectivity.
- Generic vertex, edge, face, and corner attribute layers.
- Command/delta based undo with explicit memory budgets.
- Document-owned data blocks; objects reference meshes by stable `MeshId`; authored mesh ownership is unique and Make Unique performs the deliberate deep copy.
- Non-destructive authored -> evaluated geometry boundary with ordered modifiers and derived shading data.
- Byte-budgeted immutable evaluated-result reuse suitable for constrained mobile memory.
- Vulkan viewport downstream from evaluated geometry rather than authoring truth.
- Android host targeting both `armeabi-v7a` (32-bit ARM) and `arm64-v8a`.

## Architecture

```text
Android / Desktop Host
        |
EditorContext + Tools + Operators
        |
Unified EditorHistory
        |
Document + EditableMesh authored truth
        |
GeometryOperations
        |
DependencyGraph / GeometryGraph
        |
MeshEvaluator + Modifiers
        |
Derived Corner Normals + Evaluation Cache
        |
RenderExtractor
        |
Vulkan Viewport Backend
```

Persistent authoring state never depends on Vulkan, Android, WebView, Three.js, WASM, OPFS, DOM APIs, platform event loops, or filesystem UI.

## Current foundation status

The native bootstrap, mesh kernel, command/history layer, evaluation pipeline, application architecture, and first production-hardening passes are complete enough to build upward from without rewriting the foundation.

Current engine capabilities include:

- Scene / Collection / Object / Mesh document data blocks.
- shared mesh instances and explicit Make Unique.
- move-only `Document` ownership and uniquely owned authored mesh payloads.
- bounded Document delta history instead of whole-scene snapshots for ordinary editor commands.
- bounded mesh history with exact stable-ID replay for vertex movement and face extrusion.
- reversible edge/face shading-state commands.
- edge split, face extrusion, manifold/non-manifold radial topology, n-gons, and typed attributes.
- structured topology validation plus deliberate corruption fixtures.
- deterministic randomized mutation tests.
- authored `EditableMesh` -> packed `EvaluatedMesh` conversion with source-ID mappings.
- ordered Transform, Mirror/Weld, Triangulate, and Simple Deform Twist evaluation paths.
- byte-budgeted deterministic LRU evaluation cache.
- final derived Corner normals with flat/smooth policy, angle-weighted smooth fans, and sharp/manifold boundaries.
- editor context, operators/tools, dependency graph, procedural geometry graph, project codec, and extension registries.
- persistent object translation/rotation/scale with parent-composed world matrices.
- chronological object-transform undo/redo through the unified `EditorHistory`.
- project schema v2 transform persistence with explicit schema-v1 identity migration.
- GCC + Clang warnings-as-errors builds.
- ASan + UBSan.
- clang-tidy with actionable bugprone/performance/portability checks.
- Android NDK cross-compilation for ARMv7 32-bit and ARM64 with explicit pointer-width validation.
- separate core and evaluation performance/memory benchmarks.

## Current Android viewport

`android/app` contains the live Vortex-owned Vulkan viewport. Through Phase 6 it provides:

- Vulkan instance/device/surface/swapchain lifecycle,
- persistent native `Document + EditorHistory + EditorContext` viewport session,
- evaluated engine geometry through `MeshEvaluator -> RenderExtractor`,
- indexed drawing and depth buffering,
- XZ grid plus XYZ axes,
- native camera matrices,
- one-finger orbit with tap-vs-drag arbitration,
- coherent two-finger pan,
- symmetric filtered pinch zoom,
- multiple persistent visible objects,
- nearest-hit CPU ray/triangle object picking,
- stable `ObjectId + source FaceId` mapping back to editor state,
- engine-derived object world matrices shared by drawing, picking, selection outlines, and gizmos,
- interactive axis-constrained Move / Rotate / Scale gizmo tools,
- transient transform previews that do not mutate authored state on every touch move,
- exactly one `SetObjectTransformCommand` undo step per completed transform drag,
- cancel-safe transform gestures across multitouch, tool changes, lifecycle interruption, and `ACTION_CANCEL`,
- native Undo / Redo controls for committed object transforms,
- split ARMv7/ARM64 debug APKs plus a universal APK,
- ABI packaging verification in CI.

Phase 6 is complete, merged, CI-green, and verified on the 32-bit Android target. Phase 6A established authored transform state and persistence, Phase 6B connected engine-derived world matrices to the renderer, and Phase 6C delivered working Move / Rotate / Scale manipulation with command-based commit and undo/redo while preserving the established camera and selection behavior.

## Repository layout

```text
include/vortex/      Public portable C++ headers
src/core/            Document / command implementation
src/mesh/            Modeling kernel implementation
src/eval/            Evaluated geometry, modifiers, cache, derived shading
src/editor/          Editor context and operators
src/geometry/        Reusable geometry operations
src/graph/           Dependency graph infrastructure
src/procedural/      Geometry-node composition
src/project/         Native authored-project codec
src/viewport/        Evaluated -> viewport extraction
src/ext/             Registries and extension infrastructure
src/tool/            Interactive tool sessions
tests/               Native correctness and corruption tests
benchmarks/          Core + evaluation performance measurement harnesses
docs/                Architecture, decisions, roadmap, research
scripts/             Portability/tooling checks
android/             Android APK/JNI/Vulkan host; platform code stays outside the engine
```

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DVORTEX_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The current test build registers 26 native suites, including v0.2 architecture and project-format round-trip coverage.

## Benchmarks

```bash
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DVORTEX_BUILD_TESTS=OFF \
  -DVORTEX_BUILD_BENCHMARKS=ON

cmake --build build-bench --target vortex_mesh_bench vortex_eval_bench
./build-bench/vortex_mesh_bench --scale 10000
./build-bench/vortex_eval_bench --scale 10000
```

GitHub Actions provides benchmark smoke coverage and retains benchmark output as workflow artifacts. Nonlinear setup/topology-heavy cases report their capped workload rather than hiding current scaling limits.

## Engineering documents

- [Architecture](docs/ARCHITECTURE.md)
- [v0.2 Architecture Stack](docs/V02_ARCHITECTURE.md)
- [v0.2 Deep Hardening Audit](docs/V02_HARDENING_AUDIT.md)
- [Roadmap](docs/ROADMAP.md)
- [Current Status](docs/STATUS.md)
- [Phase 5 Selection](docs/PHASE5_SELECTION.md)
- [Phase 6 Transforms](docs/PHASE6_TRANSFORMS.md)
- [Pre-Phase 6 Readiness Audit](docs/PRE_PHASE6_AUDIT.md)
- [Foundation Rules](docs/FOUNDATION_RULES.md)
- [Ownership](docs/OWNERSHIP.md)
- [Commands and Undo](docs/COMMANDS_UNDO.md)
- [Mesh Kernel](docs/MESH_KERNEL.md)
- [Evaluated Geometry](docs/EVALUATION.md)
- [Derived Shading Normals](docs/SHADING_NORMALS.md)
- [Mesh Storage](docs/MESH_STORAGE.md)
- [Validation](docs/VALIDATION.md)
- [Performance](docs/PERFORMANCE.md)
- [Android / 32-bit Support](docs/ANDROID_SUPPORT.md)
- [Project Format](docs/PROJECT_FORMAT.md)
- [Donor Audit](docs/DONOR_AUDIT.md)
- [Repository Protection](docs/REPOSITORY_PROTECTION.md)
- [Research Sources](docs/RESEARCH_SOURCES.md)

## License

MIT. See [LICENSE](LICENSE).
