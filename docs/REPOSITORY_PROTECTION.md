# Vortex3D Repository Protection

## Purpose

The engine foundation should not depend on repository settings that are invisible to reviewers. Phase 6 therefore keeps the enforceable policy in-tree and documents the small GitHub-hosted portion that must be enabled on the repository itself.

## In-tree protection

`Core CI` runs `scripts/check_repository_policy.py` before merge. The gate rejects:

- committed build/output archives and native binary artifacts,
- files larger than 5 MiB in the source repository,
- unresolved merge-conflict markers in source/text files,
- broken or repository-escaping symlinks,
- removal of the required pull-request/main CI triggers,
- use of `pull_request_target` in the untrusted core CI path,
- removal of read-only workflow contents permissions,
- removal of CODEOWNERS coverage for the repository, CI, scripts, and CMake root.

Both normal workflows declare `permissions: contents: read`. A future job that needs write access must request only the narrow permission it needs at job scope and must be reviewed as a repository-security change.

## GitHub-hosted protection

Branch/ruleset enforcement cannot be encoded by a source ZIP. Configure the repository's `main` ruleset/branch protection to:

1. Require a pull request before merging.
2. Require approvals for CODEOWNERS-sensitive changes when more than one maintainer exists.
3. Dismiss stale approvals when new commits are pushed.
4. Require conversation resolution before merge.
5. Require branches to be up to date before merge where practical.
6. Require all Core CI checks, including `repository-policy`, both host compiler jobs, sanitizer/static-analysis, both Android ABI jobs, portable-boundary, and performance-smoke.
7. Block force pushes and branch deletion for `main`.
8. Do not allow bypass except an explicitly controlled emergency maintainer path.

Until those GitHub-hosted settings are enabled, the in-tree checks still run and fail unsafe changes, but GitHub administrators could technically merge around them. The repository settings are therefore part of the Phase 6 operational gate, while the code in this patch makes that gate explicit and auditable.

## Contribution rule

Generated APKs, archives, native objects/libraries, benchmark outputs, and build directories belong in GitHub Actions artifacts or releases, not source history. Source changes must leave both `scripts/check_repository_policy.py` and `scripts/check_core_portability.py` green.
