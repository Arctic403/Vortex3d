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

if errors:
    print("Project truth check failed:", file=sys.stderr)
    for error in errors:
        print(f"- {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"Project truth check passed (schema v{schema}; CTest owns suite count)")
