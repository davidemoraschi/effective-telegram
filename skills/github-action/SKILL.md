---
name: github-action
description: Standards and required workflow for GitHub Actions CI in this repository. Use this skill whenever a task involves adding, modifying, or troubleshooting GitHub Actions workflows, CI triggers, build matrices, artifact sharing between jobs, or test execution in CI.
---

# effective-telegram GitHub Actions standard

## Goal
Validate that the codebase compiles on Linux under all supported compiler/preset combinations, and that the release binary passes all tool tests — without compiling more than once per combination.

## Workflow file
`.github/workflows/linux-compile.yml`

There is only one workflow file. It contains two jobs.

## Triggers
- `push` to `main`
- `pull_request` (all branches)

## Job: `build-linux`

Runs a matrix of 4 combinations in parallel:

| `matrix.compiler` | `matrix.preset` |
|---|---|
| gcc | linux-debug |
| gcc | linux-release |
| clang | linux-debug |
| clang | linux-release |

`fail-fast: false` — all legs run even if one fails.

Steps per leg:
1. Checkout source
2. `apt-get install ninja-build clang`
3. `cmake --preset <preset>` with `CC=<compiler>`
4. `cmake --build --preset <preset> --parallel`
5. Upload artifact `linux-release-bins` — **only on `gcc / linux-release`**

The artifact contains the contents of `out/build/linux-release/bin/`.

## Job: `test-linux`

Runs after `build-linux` completes (`needs: build-linux`). Does not checkout or build.

Steps:
1. Download artifact `linux-release-bins` into `bin/`
2. `chmod +x` both binaries
3. Run: `bin/sqlite-mcp-tool-tester bin/sqlite-mcp /tmp/test.db`

The tester creates `/tmp/test.db` if it does not exist. No external database file is required in CI.

## Artifact sharing rules
- Only one artifact is uploaded per workflow run, from the `gcc / linux-release` leg.
- Artifact name: `linux-release-bins`
- The test job consumes this artifact directly — no second build.
- Artifact actions are pinned to Node 24-compatible versions to avoid deprecation warnings:
  - `actions/upload-artifact@v4.6.2`
  - `actions/download-artifact@v4.3.0`

## Action version policy
- Always pin actions to an explicit version tag (e.g. `@v4.6.2`), never to a floating major tag (e.g. `@v4`).
- Floating major tags can silently resolve to older patch releases that target deprecated Node runtimes.
- When GitHub warns about a deprecated Node version on an action, bump that action to the latest patch that targets the current Node LTS used by runners.

## Preset and output path conventions
Defined in `CMakePresets.json`. CI uses only Linux presets:

| Preset | Build type | Output dir |
|---|---|---|
| `linux-debug` | Debug | `out/build/linux-debug/bin/` |
| `linux-release` | Release | `out/build/linux-release/bin/` |

See the `cross-compilation` skill for the full preset matrix including Windows.

## Acceptance checklist for workflow changes
- [ ] Both jobs are in the single workflow file `linux-compile.yml`.
- [ ] Matrix covers `gcc` and `clang` × `linux-debug` and `linux-release`.
- [ ] Artifact upload is conditional on `gcc / linux-release` only.
- [ ] `test-linux` has `needs: build-linux` and does not re-build.
- [ ] All action versions are pinned to explicit patch tags targeting Node 24+.
- [ ] Tester uses `/tmp/test.db` (not a local path) as the database argument.
