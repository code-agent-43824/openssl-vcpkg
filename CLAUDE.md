# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

The line above imports the owner's shared rules from `code-agent-43824/coding-rules`; nothing from
them is restated here. `AGENTS.md` is a byte-for-byte copy — refresh it with that repository's
`install.sh`, never edit it here.

## What this repository is for

Windows builds of OpenSSL compiled by MSVC through vcpkg on GitHub-hosted runners, smoke-tested
and published as GitHub Releases. The product, its requirements and stages are in
`docs/ROADMAP.md`; what works today is in `docs/STATUS.md`. OpenSSL sources are not kept here —
vcpkg fetches and builds them, so this repository holds CI configuration, vcpkg setup, the smoke
test and the release script.

## Settled decisions

Owner's requirements, 2026-10-17 (§8). The reason for each: producing exactly these builds is why
the repository exists.

- **vcpkg, not OpenSSL's own `Configure` + `nmake`.** The OpenSSL build is vcpkg's `openssl` port.
- **GitHub-hosted runners only**, for builds and tests alike — no self-hosted runners.
- **Smoke tests are separate jobs**, not steps appended to a build job. A separate job shares no
  filesystem with the build, so binaries reach it as an uploaded workflow artifact.
- **Fully static linkage: the `<arch>-windows-static` triplets** — static libraries and static CRT,
  so nothing but Windows system DLLs is needed. Not `-static-md`, not DLLs. Consequence: the
  port's `fips` feature is unavailable (`"supports": "!static"` in its `vcpkg.json`).
- **Both Debug and Release must build; how does not matter** — one `vcpkg install` producing both
  or separate jobs are equally fine.
- **CI publishes the releases**, recording the OpenSSL version and hashes. One release per
  `openssl` port version, tagged `openssl-<version>-port<N>`; a later run of the same port version
  finds the release and skips. The version and hashes are read from the vcpkg SBOM inside the
  artifacts, never typed into the repository.

## Pitfalls

- **Nothing builds or runs in the agent container.** It is Linux; MSVC and the Windows triplets
  exist only on the runners. A change is verified by the CI run on the pushed commit — read its
  job logs and artifacts, not just the status (§4).
- **"x32" means 32-bit x86**, i.e. the `x86-windows*` triplets, not the Linux x32 ABI.
- **A passing run does not prove the architecture.** arm64 code needs an arm64 runner
  (`windows-11-arm`), but that runner also runs x64 code under emulation, and x64 runners run x86
  under WoW64 — so `smoke/run.ps1` checks the executable's machine type explicitly.
- **One `vcpkg install` builds both Debug and Release by default**; debug output lands under
  `installed/<triplet>/debug/`. A triplet that sets `VCPKG_BUILD_TYPE` builds only one.
- **Only `x64-windows-static` is a built-in triplet**; `x86-windows-static` and
  `arm64-windows-static` live in vcpkg's `triplets/community/`, and vcpkg warns about that —
  expected, not an error.
- **The consumer's link picks the CRT.** OpenSSL's static build compiles with `/MT` (Release) or
  `/MTd` (Debug) plus `/Zl`, so the libraries carry no `/DEFAULTLIB` directive. A consumer that
  must not depend on CRT DLLs, smoke tests included, compiles with `/MT` / `/MTd` itself.
- **Linking by hand needs the system libraries in `$systemLibs` of `smoke/run.ps1`** (from
  OpenSSL's `NOTES-WINDOWS.md`). The port's CMake wrapper for `find_package(OpenSSL)` adds only
  `crypt32` and `ws2_32`, leaving the rest to CMake's default MSVC libraries.
- **The flags a library was built with are embedded in it** as OpenSSL's `compiler: ...` string,
  next to `OPENSSLDIR:`; `grep -a` on the `.lib` shows them without a Windows machine.
- **Workflow artifacts expire** (90 days by default); releases are the durable output.
- **Prefer `pwsh` steps and `.ps1` scripts over `.cmd`/`.bat`.** Files are LF (§9), and cmd.exe can
  mis-parse labels in LF-only batch files.

## Commands

Nothing Windows-specific builds in the container (see Pitfalls); CI is the build.

- **Lint before pushing:** `actionlint .github/workflows/ci.yml` and
  `shellcheck release/prepare.sh`. Neither is preinstalled; `pip install actionlint-py
  shellcheck-py` in a venv provides both, and actionlint then also checks the bash `run:` blocks.
- **Smoke test logic without Windows:** `cc -std=c99 -Wall -Wextra smoke/smoke.c -lssl -lcrypto`
  against the container's OpenSSL 3 headers, then run the binary.
- **Smoke test on Windows:** `./smoke/run.ps1 -Arch x64 -Config Release -OpenSslRoot <unpacked
  artifact>`; output goes to `smoke/out/`.
- **Release files without publishing:** `./release/prepare.sh <dir of openssl-<triplet>/>
  <out dir>` from the repository root, on downloaded artifacts.
- **Run CI:** push to `main` touching anything but `*.md` / `LICENSE` (`paths-ignore` in the
  workflow). The owner can also start it by hand (`workflow_dispatch`).
- **Follow a run:** unauthenticated reads of the GitHub API work from the container, e.g.
  `curl -s "https://api.github.com/repos/code-agent-43824/openssl-vcpkg/actions/runs?head_sha=<sha>"`,
  then `.../actions/runs/<id>/jobs`; job logs and artifact download links through the GitHub tools.
- **One triplet or configuration:** every matrix entry is an independent job (`fail-fast: false`),
  so its result is its own job log. No narrower unit exists.
- **Update OpenSSL:** move `builtin-baseline` in `vcpkg.json` to a newer `microsoft/vcpkg` commit.

## Map of the code

- `vcpkg.json` — manifest: the `openssl` dependency and `builtin-baseline`.
- `.github/workflows/ci.yml` — three jobs, each gated on the previous one:
  - `build` — one job per triplet on an x64 runner: checks out `microsoft/vcpkg` at the baseline
    into `vcpkg/`, bootstraps it, runs `vcpkg install` in manifest mode and uploads
    `vcpkg_installed/<triplet>/` as artifact `openssl-<triplet>`: `include/`, `lib/` (Release),
    `debug/lib/` (Debug), `share/` (CMake config, license, SBOM `vcpkg.spdx.json`). No PDBs —
    debug info is embedded (`/Z7`). `Program Files/Common Files/SSL/` holds sample configs for
    the compiled-in default `OPENSSLDIR`; the port leaves `openssldir` unset on purpose (its
    comment cites CVE-2019-12572). On failure the tails of vcpkg's build logs go into the job log.
  - `smoke` — architecture × configuration, each on a runner of its own architecture: downloads
    the artifact and runs `smoke/run.ps1`.
  - `release` — on `main` only, on Linux: `release/prepare.sh` packages the artifacts, then
    `gh release create` publishes unless the tag already exists. The tag is created by the
    release, not pushed from a session (§12).
- `smoke/smoke.c` — the test program: library version against headers, a SHA-256 test vector,
  an AES-256-GCM round trip, a TLS 1.3 handshake between in-process client and server.
- `smoke/run.ps1` — enters the MSVC environment for the target, compiles `smoke.c` with
  `/MT` / `/MTd`, rejects an executable of the wrong machine type or one importing CRT or OpenSSL
  DLLs, then runs it.
- `release/prepare.sh` — checks that all artifacts carry one port version, zips each, writes
  `SHA256SUMS` and `notes.md` (version, upstream tag and commit, source SHA512, port version,
  baseline, archive hashes) and prints the tag and title.
- `.gitignore` — directories CI and the smoke script create, for a local Windows reproduction.

## Version discipline

The OpenSSL version is the `openssl` port as of `builtin-baseline` in `vcpkg.json`. CI checks out
vcpkg at that same commit, so the tool and the ports move together and this is the only pin; the
release tag follows from it. Do not switch to the vcpkg preinstalled on the runner image: it moves
when the image updates and would change OpenSSL silently.

## Deployment

Publishing is CI-driven, so any agent may push (§6): a push to `main` that changes the `openssl`
port version publishes a new release once every smoke job passes. Changes to the workflow or the
smoke test alone do not re-release an existing port version — that takes deleting the release and
its tag, which is the owner's call (§5). After a release, check its notes and assets against the
run that produced it.
