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

## Pitfalls

- **Nothing builds or runs in the agent container.** It is Linux; MSVC and the Windows triplets
  exist only on the runners. A change is verified by the CI run on the pushed commit — read its
  job logs and artifacts, not just the status (§4).
- **"x32" means 32-bit x86**, i.e. the `x86-windows*` triplets, not the Linux x32 ABI.
- **arm64 binaries cannot run on x64 runners.** MSVC cross-compiles arm64 on x64 fine, but an arm64
  smoke test needs an arm64 runner (`windows-11-arm`). x86 binaries run on x64 runners under WoW64.
- **One `vcpkg install` builds both Debug and Release by default**; debug output lands under
  `installed/<triplet>/debug/`. A triplet that sets `VCPKG_BUILD_TYPE` builds only one. Separate
  Debug/Release jobs are a design choice, not something vcpkg requires.
- **The triplet decides linkage and CRT:** `<arch>-windows` gives DLLs with the dynamic CRT, the
  `-static` suffix static libraries with the static CRT, `-static-md` static libraries with the
  dynamic CRT. Which of these ships is the owner's call, not an agent's default.
- **Workflow artifacts expire** (90 days by default). Publishing GitHub Releases is a release job
  and needs the owner's decision (§6).
- **Prefer `pwsh` steps and `.ps1` scripts over `.cmd`/`.bat`.** Files are LF (§9), and cmd.exe can
  mis-parse labels in LF-only batch files.

## Version discipline

The OpenSSL version is whatever vcpkg resolves for the `openssl` port: the vcpkg commit or
manifest `builtin-baseline`, plus any `overrides`. A vcpkg preinstalled on a runner image moves
when the image updates, so an unpinned vcpkg changes OpenSSL silently.
