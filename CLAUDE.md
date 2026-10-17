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
- **The consumer's link picks the CRT.** OpenSSL's static build compiles with `/MT` (Release) or
  `/MTd` (Debug) plus `/Zl`, so the libraries carry no `/DEFAULTLIB` directive. A consumer that
  must not depend on CRT DLLs, smoke tests included, compiles with `/MT` / `/MTd` itself. Linking by
  hand also needs `crypt32.lib` and `ws2_32.lib` — the port's CMake wrapper for
  `find_package(OpenSSL)` adds exactly these for static Windows builds.
- **The flags a library was built with are embedded in it** as OpenSSL's `compiler: ...` string,
  next to `OPENSSLDIR:`; `grep -a` on the `.lib` shows them without a Windows machine.
- **Workflow artifacts expire** (90 days by default). Publishing GitHub Releases is a release job
  and needs the owner's decision (§6).
- **Prefer `pwsh` steps and `.ps1` scripts over `.cmd`/`.bat`.** Files are LF (§9), and cmd.exe can
  mis-parse labels in LF-only batch files.

## Commands

Nothing builds in the container (see Pitfalls); CI is the build.

- **Lint the workflow before pushing:** `actionlint .github/workflows/build.yml`. Not preinstalled;
  `pip install actionlint-py` in a venv provides it.
- **Run CI:** push to `main` touching anything but `*.md` / `LICENSE` (`paths-ignore` in the
  workflow). The owner can also start it by hand (`workflow_dispatch`).
- **Follow a run:** unauthenticated reads of the GitHub API work from the container, e.g.
  `curl -s "https://api.github.com/repos/code-agent-43824/openssl-vcpkg/actions/runs?head_sha=<sha>"`,
  then `.../actions/runs/<id>/jobs`; job logs through the GitHub tools.
- **One triplet:** each matrix entry is an independent job (`fail-fast: false`), so a triplet's
  result is its own job log. No narrower unit exists.
- **Update OpenSSL:** move `builtin-baseline` in `vcpkg.json` to a newer `microsoft/vcpkg` commit.

## Map of the code

- `vcpkg.json` — manifest: the `openssl` dependency and `builtin-baseline`.
- `.github/workflows/build.yml` — one job per triplet on an x64 runner: checks out
  `microsoft/vcpkg` at the baseline into `vcpkg/`, bootstraps it, runs `vcpkg install` in
  manifest mode into `vcpkg_installed/<triplet>/` and uploads that directory as artifact
  `openssl-<triplet>`: `include/`, `lib/` (Release), `debug/lib/` (Debug), `share/` (CMake
  config, license). There are no PDBs — debug info is embedded (`/Z7`). `Program Files/Common
  Files/SSL/` holds sample configs for the compiled-in default `OPENSSLDIR`; the port leaves
  `openssldir` unset on purpose (its comment cites CVE-2019-12572). The "List built libraries"
  step logs what was produced; on failure, "Show vcpkg build logs" prints the tails of vcpkg's
  build logs.
- `.gitignore` — the two directories CI creates, for a local Windows reproduction.

## Version discipline

The OpenSSL version is the `openssl` port as of `builtin-baseline` in `vcpkg.json`. CI checks out
vcpkg at that same commit, so the tool and the ports move together and this is the only pin. Do
not switch to the vcpkg preinstalled on the runner image: it moves when the image updates and
would change OpenSSL silently.
