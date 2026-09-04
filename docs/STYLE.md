# Vortex3D Native Code Policy

## C++ baseline

- C++20 is the project language baseline.
- Portable core code must compile under both GCC and Clang.
- CI treats compiler warnings as errors for the core target.
- Platform APIs must not leak into `include/vortex`, `src/core`, or `src/mesh`.

## Formatting

`.clang-format` is the canonical formatting configuration. New C++ code should be formatted with that file before merge.

## Static analysis

`.clang-tidy` defines the baseline bug-prone, performance, and portability checks. The initial policy is intentionally conservative while the core API is young; checks can become stricter as false positives are resolved.

## Safety gates

Host CI includes:

- GCC debug build + tests,
- Clang debug build + tests,
- warnings-as-errors,
- AddressSanitizer,
- UndefinedBehaviorSanitizer,
- portable-core boundary scanning.

## Architecture boundary

The portable modeling core must remain independent from Android/JNI, Vulkan, browser/WebView, Emscripten/WASM, Three.js, and platform-specific headers. Platform and rendering integrations consume the core; the core never depends on them.

## 32-bit rule

Code must not use pointer size as persistent identity. Persistent IDs remain explicit 64-bit values on both ARMv7 and ARM64. Memory-heavy algorithms should be reviewed for 32-bit address-space cost before they become foundational APIs.
