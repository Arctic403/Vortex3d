#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
# All public portable headers and every portable engine implementation source are scanned.
# Android/JNI/Vulkan code lives outside these roots under android/.
SCAN_ROOTS = [ROOT / "include" / "vortex", ROOT / "src"]
FORBIDDEN = {
    "jni.h": "JNI belongs in the Android host layer",
    "android/": "Android headers belong in the Android host layer",
    "vulkan/": "Vulkan headers belong in the renderer layer",
    "emscripten": "Emscripten belongs in an optional web bridge",
    "webview": "WebView must not enter the portable core",
    "three.js": "Three.js must not enter the portable core",
    "windows.h": "platform headers must not enter the portable core",
}

violations: list[str] = []
for root in SCAN_ROOTS:
    if not root.exists():
        continue
    for path in root.rglob("*"):
        if path.suffix not in {".h", ".hpp", ".c", ".cc", ".cpp", ".cxx"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace").lower()
        for needle, reason in FORBIDDEN.items():
            if needle.lower() in text:
                violations.append(f"{path.relative_to(ROOT)}: {needle} — {reason}")

if violations:
    print("Portable-core boundary violations found:")
    for violation in violations:
        print(f"  - {violation}")
    sys.exit(1)

print("Vortex3D portable-core boundary check passed")
