#!/usr/bin/env python3
"""Fail CI on repository hygiene/security regressions relevant to Vortex3D."""

from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {".git", ".idea", ".vscode", "__pycache__"}
FORBIDDEN_DIR_PREFIXES = ("build", "cmake-build")
FORBIDDEN_SUFFIXES = {
    ".a", ".apk", ".class", ".dll", ".dylib", ".exe", ".jar", ".o", ".obj", ".so", ".zip"
}
MAX_FILE_BYTES = 5 * 1024 * 1024
CONFLICT_MARKERS = (b"<<<<<<< ", b"=======\n", b">>>>>>> ")
TEXT_SUFFIXES = {
    "", ".c", ".cc", ".cmake", ".cpp", ".h", ".hpp", ".md", ".py", ".txt", ".yml", ".yaml"
}


def iter_repository_files():
    for path in ROOT.rglob("*"):
        rel = path.relative_to(ROOT)
        if any(part in SKIP_DIRS or part.startswith(FORBIDDEN_DIR_PREFIXES) for part in rel.parts[:-1]):
            continue
        if path.is_file() or path.is_symlink():
            yield path, rel


def check_workflow_policy(errors: list[str]) -> None:
    workflow = ROOT / ".github" / "workflows" / "core-ci.yml"
    if not workflow.is_file():
        errors.append("missing .github/workflows/core-ci.yml")
        return
    text = workflow.read_text(encoding="utf-8")
    required = (
        "pull_request:",
        "branches: [main]",
        "permissions:\n  contents: read",
        "repository-policy:",
        "python3 scripts/check_repository_policy.py",
    )
    for token in required:
        if token not in text:
            errors.append(f"core-ci.yml missing required policy token: {token!r}")
    if "pull_request_target:" in text:
        errors.append("core-ci.yml must not use pull_request_target for untrusted contribution CI")


def check_codeowners(errors: list[str]) -> None:
    codeowners = ROOT / ".github" / "CODEOWNERS"
    if not codeowners.is_file():
        errors.append("missing .github/CODEOWNERS")
        return
    text = codeowners.read_text(encoding="utf-8")
    for token in ("* @Arctic403", "/.github/ @Arctic403", "/scripts/ @Arctic403"):
        if token not in text:
            errors.append(f"CODEOWNERS missing required ownership rule: {token}")


def main() -> int:
    errors: list[str] = []
    check_workflow_policy(errors)
    check_codeowners(errors)

    for path, rel in iter_repository_files():
        if path.is_symlink():
            try:
                resolved = path.resolve(strict=True)
            except FileNotFoundError:
                errors.append(f"broken symlink: {rel}")
                continue
            if ROOT not in resolved.parents and resolved != ROOT:
                errors.append(f"symlink escapes repository: {rel} -> {resolved}")
            continue

        if rel.suffix.lower() in FORBIDDEN_SUFFIXES:
            errors.append(f"generated/binary artifact must not be committed: {rel}")
        try:
            size = path.stat().st_size
        except OSError as exc:
            errors.append(f"cannot stat {rel}: {exc}")
            continue
        if size > MAX_FILE_BYTES:
            errors.append(f"file exceeds 5 MiB repository limit: {rel} ({size} bytes)")

        if rel.suffix.lower() in TEXT_SUFFIXES and size <= MAX_FILE_BYTES:
            try:
                data = path.read_bytes()
            except OSError as exc:
                errors.append(f"cannot read {rel}: {exc}")
                continue
            if all(marker in data for marker in CONFLICT_MARKERS):
                errors.append(f"unresolved merge-conflict markers: {rel}")

    if errors:
        print("Repository policy check FAILED:", file=sys.stderr)
        for error in errors:
            print(f" - {error}", file=sys.stderr)
        return 1

    print("Repository policy check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
