# Vortex3D Native Code Policy

## C++ baseline

- C++20 is the project language baseline.
- C++ extensions are disabled for `vortex_core`.
- Portable core code must compile under both GCC and Clang.
- CI treats compiler warnings as errors for the core target.
- Platform APIs must not leak into `include/vortex`, `src/core`, or `src/mesh`.

## Formatting

`.clang-format` is the canonical formatting configuration. New C++ code should be formatted with that file before merge.

## Static analysis

`.clang-tidy` defines the bug-prone, performance, and portability baseline. CI runs it against the portable C++ implementation using the generated compile database.

The baseline intentionally disables `bugprone-easily-swappable-parameters` because topology APIs naturally contain same-typed endpoint/ID arguments and the check is noisy without improving those contracts. Other checks should be disabled only with a documented reason; new warnings should normally be fixed rather than ignored.

Do not enable broad `modernize-*` or `readability-*` families merely to generate churn. Adopt individual checks when they improve correctness or maintainability.

## Safety gates

Host/portable CI includes:

- GCC debug build + all native tests,
- Clang debug build + all native tests,
- warnings-as-errors,
- AddressSanitizer,
- UndefinedBehaviorSanitizer,
- clang-tidy static analysis,
- portable-core dependency scanning,
- Android NDK cross-compiles for `armeabi-v7a` and `arm64-v8a`.

## Architecture boundary

The portable modeling core remains independent from Android/JNI, Vulkan, browser/WebView, Emscripten/WASM, Three.js, and platform-specific headers. Platform and rendering integrations consume the core; the core never depends on them.

## 32-bit rule

`armeabi-v7a` is a first-class build target. CI explicitly verifies that its NDK toolchain produces a 32-bit target and that `arm64-v8a` produces a 64-bit target.

Code must not use pointer size as persistent identity. Persistent IDs remain explicit 64-bit values on both ARMv7 and ARM64. `size_t` is for in-process sizes/indices only and must not define serialized identity. Memory-heavy algorithms are reviewed and benchmarked before they become foundational APIs.
