# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

The line above imports the owner's shared rules from `code-agent-43824/coding-rules`; nothing from
them is restated here. `AGENTS.md` is a byte-for-byte copy — refresh it with that repository's
`install.sh`, never edit it here.

## What this repository is for

Windows builds of OpenSSL compiled by MSVC through vcpkg on GitHub-hosted runners, with smoke
tests. The product, its requirements and stages are in `docs/ROADMAP.md`; what works today is in
`docs/STATUS.md`. OpenSSL sources are not kept here — vcpkg fetches and builds them, so this
repository holds CI configuration and vcpkg setup.

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

## Pitfalls

- **Nothing builds or runs in the agent container.** It is Linux; MSVC and the Windows triplets
  exist only on the runners. A change is verified by the CI run on the pushed commit — read its
  job logs and artifacts, not just the status (§4).
- **"x32" means 32-bit x86**, i.e. the `x86-windows*` triplets, not the Linux x32 ABI.
- **arm64 binaries cannot run on x64 runners.** MSVC cross-compiles arm64 on x64 fine, but an arm64
  smoke test needs an arm64 runner (`windows-11-arm`). x86 binaries run on x64 runners under WoW64.
- **One `vcpkg install` builds both Debug and Release by default**; debug output lands under
  `installed/<triplet>/debug/`. A triplet that sets `VCPKG_BUILD_TYPE` builds only one.
- **Only `x64-windows-static` is a built-in triplet**; `x86-windows-static` and
  `arm64-windows-static` live in vcpkg's `triplets/community/`, and vcpkg warns about that —
  expected, not an error.
- **Static CRT binds the consumer.** Code linking these libraries, smoke tests included, must
  compile with `/MT` (Release) or `/MTd` (Debug); mixing with `/MD` fails with LNK2038. Linking by
  hand also needs `crypt32.lib` and `ws2_32.lib` — the port's CMake wrapper for
  `find_package(OpenSSL)` adds exactly these for static Windows builds.
- **Workflow artifacts expire** (90 days by default). Publishing GitHub Releases is a release job
  and needs the owner's decision (§6).
- **Prefer `pwsh` steps and `.ps1` scripts over `.cmd`/`.bat`.** Files are LF (§9), and cmd.exe can
  mis-parse labels in LF-only batch files.

## Version discipline

The OpenSSL version is whatever vcpkg resolves for the `openssl` port: the vcpkg commit or
manifest `builtin-baseline`, plus any `overrides`. A vcpkg preinstalled on a runner image moves
when the image updates, so an unpinned vcpkg changes OpenSSL silently.
