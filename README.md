# Vortex3D Native

Vortex3D is a native-first, mobile-first 3D modeling engine and editor.

This repository is the clean foundation for the new Vortex3D architecture. The previous `Vortex3dGm` project remains a donor/reference implementation; browser-specific infrastructure is not the foundation here.

## Core goals

- Native C++20 modeling core with no UI, browser, renderer, or Android dependency.
- Durable document/scene architecture designed for authoring rather than a game-runtime ECS.
- Editable polygon mesh kernel with stable topology identities.
- Generic attribute layers on vertex, edge, face, and corner domains.
- Commands + transactions + undo/redo as the only supported mutation path.
- Non-destructive evaluation graph for modifiers and procedural operations.
- Vulkan viewport that consumes evaluated geometry rather than owning authoring data.
- Android host with both `armeabi-v7a` (32-bit ARM) and `arm64-v8a` support.
- Portable project format with migrations, autosave, and crash recovery.
- Tool/command API designed so touch UI, keyboard, tests, plugins, and future AI all call the same operations.

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
Evaluation Graph + Modifiers
        |
Evaluated Mesh
        |
Renderer Backend (Vulkan first)
```

Persistent authoring state never depends on Vulkan, Android, WebView, Three.js, WASM, OPFS, or DOM APIs.

## Repository layout

```text
include/vortex/      Public C++ engine headers
src/                 Engine implementation
tests/               Native tests
docs/                Architecture, roadmap, decisions, research
android/              Android host (introduced after the core gates pass)
```

## Current phase

**Phase 0 — Native Core Bootstrap**

The first gate is intentionally boring: build the C++ library on a normal host, create a document, allocate stable IDs, run deterministic tests, and prove the engine has zero platform dependencies.

See:

- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Foundation Rules](docs/FOUNDATION_RULES.md)
- [Donor Audit](docs/DONOR_AUDIT.md)
- [Android / 32-bit Support](docs/ANDROID_SUPPORT.md)
- [Research Sources](docs/RESEARCH_SOURCES.md)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

MIT. See [LICENSE](LICENSE).
