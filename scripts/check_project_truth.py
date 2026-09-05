#!/usr/bin/env python3
"""Fail CI when current-facing project documentation drifts from code-owned truths."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "include/vortex/project/project_io.hpp").read_text(encoding="utf-8")
match = re.search(r"schemaVersion\s*=\s*(\d+)\s*;", header)
if not match:
    raise SystemExit("Unable to locate ProjectCodec::schemaVersion")
schema = match.group(1)

checks = {
    "README.md": f"project schema v{schema}",
    "docs/STATUS.md": f"Project schema v{schema}",
    "docs/PROJECT_FORMAT.md": f"Current implemented schema: v{schema}",
}
errors: list[str] = []
for relative, expected in checks.items():
    text = (ROOT / relative).read_text(encoding="utf-8")
    if expected not in text:
        errors.append(f"{relative}: missing current schema marker {expected!r}")

for relative in ("README.md", "docs/STATUS.md"):
    text = (ROOT / relative).read_text(encoding="utf-8")
    if re.search(r"current test (?:build|suite).*?\b\d+\b", text, flags=re.IGNORECASE):
        errors.append(f"{relative}: hard-coded current test count found; CTest must remain the source of truth")

default_orientation_checks = {
    "include/vortex/editor/gizmo.hpp": r"GizmoConstraint\s+final\s*\{.*?orientation\s*=\s*TransformOrientation::Global\s*;",
    "android/app/src/main/cpp/viewport_host.hpp": r"transformOrientation_\s*=\s*TransformOrientation::Global\s*;",
    "android/app/src/main/cpp/vulkan_viewport.hpp": r"gizmoOrientation_\s*=\s*TransformOrientation::Global\s*;",
    "android/app/src/main/java/com/vortex3d/app/MainActivity.java": r"transformOrientation\s*=\s*ORIENTATION_GLOBAL\s*;",
}
for relative, pattern in default_orientation_checks.items():
    text = (ROOT / relative).read_text(encoding="utf-8")
    if not re.search(pattern, text, flags=re.DOTALL):
        errors.append(f"{relative}: standard gizmo orientation must default to Global")

scale_orientation_checks = {
    "android/app/src/main/cpp/viewport_host.hpp": (
        r"mode\s*==\s*TransformToolMode::Scale.*?"
        r"setGizmoOrientation\(TransformOrientation::Local\)"
    ),
    "android/app/src/main/java/com/vortex3d/app/MainActivity.java": (
        r"tool\s*==\s*TOOL_SCALE.*?"
        r"transformOrientation\s*=\s*ORIENTATION_LOCAL"
    ),
}
for relative, pattern in scale_orientation_checks.items():
    text = (ROOT / relative).read_text(encoding="utf-8")
    if not re.search(pattern, text, flags=re.DOTALL):
        errors.append(f"{relative}: strict-TRS Scale must select the Local frame")

root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
portable_sources = {path.relative_to(ROOT).as_posix() for path in (ROOT / "src").rglob("*.cpp")}
declared_sources = set(re.findall(r"\bsrc/[A-Za-z0-9_./-]+\.cpp\b", root_cmake))
portable_tests = {path.relative_to(ROOT).as_posix() for path in (ROOT / "tests").glob("*.cpp")}
declared_tests = set(re.findall(r"\btests/[A-Za-z0-9_./-]+\.cpp\b", root_cmake))

android_cpp = ROOT / "android/app/src/main/cpp"
android_cmake = (android_cpp / "CMakeLists.txt").read_text(encoding="utf-8")
android_sources = {path.name for path in android_cpp.glob("*.cpp")}
declared_android_sources = set(re.findall(r"\b[A-Za-z0-9_-]+\.cpp\b", android_cmake))

for label, missing in (
    ("portable source missing from root CMake", portable_sources - declared_sources),
    ("root CMake source missing on disk", declared_sources - portable_sources),
    ("test missing from root CMake", portable_tests - declared_tests),
    ("root CMake test missing on disk", declared_tests - portable_tests),
    ("Android source missing from Android CMake", android_sources - declared_android_sources),
    ("Android CMake source missing on disk", declared_android_sources - android_sources),
):
    for path in sorted(missing):
        errors.append(f"{label}: {path}")

if errors:
    print("Project truth check failed:", file=sys.stderr)
    for error in errors:
        print(f"- {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"Project truth check passed (schema v{schema}; CTest owns suite count)")
