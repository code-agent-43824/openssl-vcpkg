#!/usr/bin/env bash
# Turns the per-triplet CI artifacts into release files: one zip per triplet, SHA256SUMS and
# notes.md recording the OpenSSL version and source hash. Those facts are read from the vcpkg
# SBOM inside each artifact (share/openssl/vcpkg.spdx.json), not kept as values in this repo.
# Prints tag= and title= lines for $GITHUB_OUTPUT; run from the repository root.
#
#   ./release/prepare.sh <dir with openssl-<triplet>/ artifacts> <output dir>

set -euo pipefail
shopt -s nullglob

artifacts=$1
mkdir -p "$2"
dist=$(cd "$2" && pwd)

dirs=("$artifacts"/openssl-*/)
[ ${#dirs[@]} -gt 0 ] || { echo "no openssl-* artifacts in $artifacts" >&2; exit 1; }

sbom_field() { jq -r "$1" "$2/share/openssl/vcpkg.spdx.json"; }

# All triplets must come from the same port version, or one release would mix builds.
port_version=$(sbom_field '.packages[] | select(.SPDXID == "SPDXRef-port") | .versionInfo' "${dirs[0]}")
for dir in "${dirs[@]}"; do
  v=$(sbom_field '.packages[] | select(.SPDXID == "SPDXRef-port") | .versionInfo' "$dir")
  [ "$v" = "$port_version" ] || { echo "port versions differ: $port_version vs $v ($dir)" >&2; exit 1; }
done

version=${port_version%%#*}
port_revision=0
[[ $port_version == *"#"* ]] && port_revision=${port_version#*#}
# Bumped by hand when the release contents change without a new port version.
revision=$(tr -d '[:space:]' < release/revision)
[[ $revision =~ ^[0-9]+$ ]] || { echo "release/revision is not a number: $revision" >&2; exit 1; }
tag="openssl-$version-port$port_revision-r$revision"

# downloadLocation looks like git+https://github.com/openssl/openssl@openssl-3.6.5
source=$(sbom_field '.packages[] | select(.name == "openssl/openssl") | .downloadLocation' "${dirs[0]}")
source_sha512=$(sbom_field '.packages[] | select(.name == "openssl/openssl")
  | .checksums[] | select(.algorithm == "SHA512") | .checksumValue' "${dirs[0]}")
source=${source#git+}
source_repo=${source%@*}
source_ref=${source##*@}
[ -n "$source_sha512" ] && [ "$source_repo" != "$source" ] || { echo "SBOM lacks the source record" >&2; exit 1; }
source_commit=$(git ls-remote "$source_repo" "refs/tags/$source_ref^{}" | cut -f1)
[ -n "$source_commit" ] || source_commit=$(git ls-remote "$source_repo" "refs/tags/$source_ref" | cut -f1)
[ -n "$source_commit" ] || { echo "cannot resolve $source_ref in $source_repo" >&2; exit 1; }

baseline=$(jq -r '."builtin-baseline"' vcpkg.json)
repo_url="${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-code-agent-43824/openssl-vcpkg}"
commit=${GITHUB_SHA:-$(git rev-parse HEAD)}
run=${GITHUB_RUN_ID:+$repo_url/actions/runs/$GITHUB_RUN_ID}

for dir in "${dirs[@]}"; do
  triplet=$(basename "$dir")
  triplet=${triplet#openssl-}
  (cd "$dir" && zip -qr -X "$dist/openssl-$version-$triplet.zip" .)
done
(cd "$dist" && sha256sum -- *.zip > SHA256SUMS)

cat > "$dist/notes.md" <<EOF
OpenSSL $version для Windows, собранный MSVC через vcpkg, Debug и Release. Обе сборки зависят
только от системных DLL Windows.

| | |
| --- | --- |
| Версия OpenSSL | \`$version\` |
| Исходники | $source_repo, тег \`$source_ref\`, коммит \`$source_commit\` |
| SHA512 архива исходников (проверен vcpkg) | \`$source_sha512\` |
| Порт vcpkg | \`openssl $port_version\` |
| vcpkg (\`builtin-baseline\`) | \`$baseline\` |
| Коммит этого репозитория | \`$commit\`, ревизия сборки \`r$revision\` |
| Прогон CI | ${run:-локальная сборка} |

- \`*-windows-static\` — статические библиотеки: \`lib/\` (Release, \`/MT\`), \`debug/lib/\`
  (Debug, \`/MTd\`).
- \`*-windows-dll-mt\` — DLL со статическим CRT: \`bin/\` и \`debug/bin/\`, import-библиотеки
  в \`lib/\` и \`debug/lib/\`. Приложению, которое передаёт OpenSSL \`FILE*\`, нужен
  \`include/openssl/applink.c\`.

В каждом архиве ещё \`include/\` и \`share/\` (CMake-конфиг, лицензия, SBOM). Системные
библиотеки для линковки и проверки, которые прошёл каждый архив, — в
[\`smoke/\`]($repo_url/tree/$commit/smoke).

SHA-256 архивов:

\`\`\`
$(cat "$dist/SHA256SUMS")
\`\`\`
EOF

echo "tag=$tag"
echo "title=OpenSSL $version (vcpkg $port_version, r$revision)"
