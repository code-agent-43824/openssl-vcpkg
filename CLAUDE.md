# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

The line above imports the owner's shared rules from `code-agent-43824/coding-rules`; nothing from
them is restated here. `AGENTS.md` is a byte-for-byte copy — refresh it with that repository's
`install.sh`, never edit it here.

## What this repository is for

Windows builds of OpenSSL compiled by MSVC through vcpkg on GitHub-hosted runners — static
libraries and DLLs — tested on runners of their own architecture and published as GitHub
Releases. The product, its requirements and stages are in
`docs/ROADMAP.md`; what works today is in `docs/STATUS.md`. OpenSSL sources are not kept here —
vcpkg fetches and builds them, so this repository holds CI configuration, vcpkg setup, the test
programs and the release script.

## Settled decisions

Owner's requirements, 2026-10-17 (§8). The reason for each: producing exactly these builds is why
the repository exists.

- **vcpkg, not OpenSSL's own `Configure` + `nmake`.** The OpenSSL build is vcpkg's `openssl` port.
- **GitHub-hosted runners only**, for builds and tests alike — no self-hosted runners.
- **Smoke tests are separate jobs**, not steps appended to a build job. A separate job shares no
  filesystem with the build, so binaries reach it as an uploaded workflow artifact.
- **Two builds per architecture, both importing only Windows system DLLs:** static libraries
  (`<arch>-windows-static`) and DLLs with the static CRT (`<arch>-windows-dll-mt`, the overlay
  triplets in `triplets/`; vcpkg has no stock triplet for that pairing). Nothing linked against
  the dynamic CRT. The port's `fips` feature is unavailable for the static triplets
  (`"supports": "!static"` in its `vcpkg.json`).
- **Both Debug and Release must build; how does not matter** — one `vcpkg install` producing both
  or separate jobs are equally fine.
- **CI publishes the releases**, recording the OpenSSL version and hashes. One release per
  `openssl` port version and build revision, tagged `openssl-<version>-port<N>-r<revision>`
  (`release/revision`); a later run with both unchanged finds the release and skips. The first
  release, `openssl-3.6.5-port2`, predates the revision and stays as published. The version and
  hashes are read from the vcpkg SBOM inside the artifacts, never typed into the repository.

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
- **Each `dll-mt` DLL carries a CRT of its own.** A `FILE*` from the application reaches OpenSSL
  only through `include/openssl/applink.c` compiled into the application (`NOTES-WINDOWS.md`;
  all three targets have uplink support), and OpenSSL-allocated memory goes back through
  `OPENSSL_free`. That is why `smoke.c` prints errors with `ERR_print_errors_cb`, not `_fp`.
- **Static-library consumers link the system libraries in `$systemLibs` of `smoke/run.ps1`**
  (from OpenSSL's `NOTES-WINDOWS.md`). The port's CMake wrapper for `find_package(OpenSSL)` adds only
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
- **Test logic without Windows:** `cc -std=c99 -Wall -Wextra -pthread smoke/<test>.c -lssl
  -lcrypto` (`smoke` or `functional`) against the container's OpenSSL 3, then run the binary.
  Before trusting a new vector, corrupt it once and confirm its check fails.
- **Smoke test on Windows:** `./smoke/run.ps1 -Arch x64 -Linkage dll-mt -Config Release
  -OpenSslRoot <unpacked artifact>`; output goes to `smoke/out/`.
- **Release files without publishing:** `./release/prepare.sh <dir of openssl-<triplet>/>
  <out dir>` from the repository root, on downloaded artifacts.
- **Run CI:** push to `main` touching anything but `*.md` / `LICENSE` (`paths-ignore` in the
  workflow). The owner can also start it by hand (`workflow_dispatch`).
- **Follow a run:** unauthenticated reads of the GitHub API work from the container, e.g.
  `curl -s "https://api.github.com/repos/code-agent-43824/openssl-vcpkg/actions/runs?head_sha=<sha>"`,
  then `.../actions/runs/<id>/jobs`; job logs and artifact download links through the GitHub tools.
- **One variant:** each architecture × linkage is its own build job and smoke job
  (`fail-fast: false`); the smoke job reports Debug and Release as separate steps.
- **Update OpenSSL:** move `builtin-baseline` in `vcpkg.json` to a newer `microsoft/vcpkg` commit.

## Map of the code

- `vcpkg.json` — manifest: the `openssl` dependency, `builtin-baseline`, and `triplets/` as overlay
  triplets.
- `triplets/<arch>-windows-dll-mt.cmake` — dynamic library linkage with the static CRT.
- `.github/workflows/ci.yml` — calls `build-and-test.yml` once per architecture × linkage
  (`static`, `dll-mt`; the triplet is `<arch>-windows-<linkage>`), then `release` after all of
  them. Reusable-workflow calls are what let each smoke job start as soon as its own build
  finishes (`needs` on a matrix job waits for every entry). They are six explicit jobs, not a
  matrix, so the run graph shows each variant as its own build → smoke card — the owner's
  request; a new variant goes into `release`'s `needs` too.
  - `release` — on `main` only, on Linux: `release/prepare.sh` packages the artifacts, then
    `gh release create` publishes unless the tag already exists. The tag is created by the
    release, not pushed from a session (§12).
- `.github/workflows/build-and-test.yml` — one variant, two jobs:
  - `build` — on an x64 runner: checks out `microsoft/vcpkg` at the baseline
    into `vcpkg/`, bootstraps it, runs `vcpkg install` in manifest mode and uploads
    `vcpkg_installed/<triplet>/` as artifact `openssl-<triplet>`: `include/`, `lib/` (Release),
    `debug/lib/` (Debug), `share/` (CMake config, license, SBOM `vcpkg.spdx.json`), and for
    `dll-mt` also `bin/` and `debug/bin/`. Static libraries have no PDBs — debug info is
    embedded (`/Z7`). `Program Files/Common Files/SSL/` holds sample configs for
    the compiled-in default `OPENSSLDIR`; the port leaves `openssldir` unset on purpose (its
    comment cites CVE-2019-12572). On failure the tails of vcpkg's build logs go into the job log.
  - `smoke` — on a runner of the variant's architecture: downloads the artifact and runs
    `smoke/run.ps1` (both test programs) for Debug, then for Release even if Debug failed.
- `smoke/smoke.c` — the test program: library version against headers, a SHA-256 test vector,
  an AES-256-GCM round trip, a TLS 1.3 handshake between in-process client and server, a BIO on
  the application's stdout (passes in the DLL build only through applink).
- `smoke/functional.c` — functional tests: 8 threads hitting OpenSSL from first use;
  known-answer tests whose vectors come from the RFC or NIST file named beside each one; RSA,
  ECDSA, ECDH and AEAD round trips that must also reject changed data or another key; the
  `default` and `legacy` providers (`legacy` is built into `libcrypto` in the static build —
  the port configures it `no-module` — and is `legacy.dll` in the DLL build).
- `smoke/run.ps1` — enters the MSVC environment for the target and compiles both programs with
  `/MT` / `/MTd` (plus `applink.c` for `dll-mt`, whose DLLs it copies next to the executables).
  Rejects any binary of the wrong machine type or importing CRT DLLs; a static build's executables
  must not import OpenSSL DLLs, a DLL build's must import libcrypto (the linker drops an unused
  libssl import). Then runs both, even if the first fails.
- `release/prepare.sh` — checks that all artifacts carry one port version, zips each, writes
  `SHA256SUMS` and `notes.md` (version, upstream tag and commit, source SHA512, port version,
  baseline, revision, archive hashes) and prints the tag and title.
- `release/revision` — the build revision in the release tag.
- `.gitignore` — directories CI and the smoke script create, for a local Windows reproduction.

## Version discipline

The OpenSSL version is the `openssl` port as of `builtin-baseline` in `vcpkg.json`. CI checks out
vcpkg at that same commit, so the tool and the ports move together and this is the only pin; the
release tag follows from it. Do not switch to the vcpkg preinstalled on the runner image: it moves
when the image updates and would change OpenSSL silently.

## Deployment

Publishing is CI-driven, so any agent may push (§6): a push to `main` that changes the `openssl`
port version publishes a new release once every smoke job passes. When the release contents change
without a new port version — a new build variant, different packaging — bump `release/revision`
in the same change; changes that leave the assets as they were need no new release. Deleting a
release or tag is the owner's call (§5). After a release, check its notes and assets against the
run that produced it.
