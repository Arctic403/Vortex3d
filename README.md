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
Editor + Tool Controllers
        |
Commands + Transactions
        |
Document + Scene DAG
        |
Editable Mesh Kernel
        |
Mesh Evaluator
        |
Transform -> Mirror/Weld -> Triangulate
        |
Derived Corner Normals
        |
Bounded Evaluation Cache
        |
future render-vertex packing / Vulkan backend
```

Persistent authoring state never depends on Vulkan, Android, WebView, Three.js, WASM, OPFS, DOM APIs, platform event loops, or filesystem UI.

## Current foundation status

The native bootstrap, mesh kernel, command/history layer, evaluation pipeline, and first production-hardening passes are complete enough to build upward from without rewriting the foundation.

Current capabilities include:

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
- ordered Transform, Mirror/Weld, and concave-safe Triangulate modifiers.
- byte-budgeted deterministic LRU evaluation cache.
- final derived Corner normals with flat/smooth policy, angle-weighted smooth fans, and sharp/manifold boundaries.
- GCC + Clang warnings-as-errors builds.
- ASan + UBSan.
- clang-tidy with actionable bugprone/performance/portability checks.
- Android NDK cross-compilation for ARMv7 32-bit and ARM64 with explicit pointer-width validation.
- separate core and evaluation performance/memory benchmarks with 10k / 100k / 1M requested profiles where practical.

The next architectural feature is a **narrow renderer-facing immutable snapshot/upload contract**. Storage/allocator changes remain evidence-driven and are not being performed simply because a newer container pattern exists.

## Repository layout

```text
include/vortex/      Public portable C++ headers
src/core/            Document / command implementation
src/mesh/            Modeling kernel implementation
src/eval/            Evaluated geometry, modifiers, cache, derived shading
tests/               Native correctness and corruption tests
benchmarks/          Core + evaluation performance measurement harnesses
docs/                Architecture, decisions, roadmap, research
scripts/             Portability/tooling checks
android/             Future Android host; platform code stays outside the core
```

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DVORTEX_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The current test build registers 18 native suites on the Derived Shading Normals patch.

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

GitHub Actions provides explicit 10k, 100k, and 1M requested benchmark profiles. Nonlinear setup/topology-heavy cases are transparently capped and report `capped: true` rather than hiding current scaling limits.

## Engineering documents

- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Current Status](docs/STATUS.md)
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
- [Research Sources](docs/RESEARCH_SOURCES.md)

## License

MIT. See [LICENSE](LICENSE).
