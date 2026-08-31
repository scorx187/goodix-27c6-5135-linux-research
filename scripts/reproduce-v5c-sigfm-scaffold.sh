#!/usr/bin/env bash
set -euo pipefail

EXPECTED_BRANCH="goodix-27c6-5135-chicagohu"
EXPECTED_HEAD="37ac6876fd6d248b48a7892410cf75144f3882e5"
SIGFM_REPO="https://github.com/goodix-fp-linux-dev/libfprint.git"
SIGFM_COMMIT="07306bbc9256942595e31fb0f407b364ffa24d07"
PATCH_OUT="${HOME}/goodix5135-v5c-sigfm-meson-scaffold-complete.patch"

WT=""
UPSTREAM=""
BUILD=""

cleanup() {
    if [[ -n "${WT}" && -d "${WT}" ]]; then
        git worktree remove --force "${WT}" >/dev/null 2>&1 || true
    fi
    git worktree prune >/dev/null 2>&1 || true

    if [[ -n "${UPSTREAM}" && -d "${UPSTREAM}" ]]; then
        rm -rf "${UPSTREAM}" >/dev/null 2>&1 || true
    fi
}

fail() {
    echo
    echo "================================================"
    echo "V5C REPRODUCTION FAILED: $1"
    echo "================================================"
    if [[ -n "${WT}" && -d "${WT}" ]]; then
        git -C "${WT}" status --short --untracked-files=all || true
    fi
    cleanup
    exit 1
}

trap cleanup EXIT INT TERM

echo "================================================"
echo "GOODIX5135 V5C REPRODUCTION"
echo "HOST ONLY — NO USB / NO SENSOR / NO BIOMETRIC DATA"
echo "================================================"

[[ "$(git branch --show-current)" == "${EXPECTED_BRANCH}" ]] || fail "unexpected branch"
[[ "$(git rev-parse HEAD)" == "${EXPECTED_HEAD}" ]] || fail "unexpected HEAD"
[[ -z "$(git status --porcelain)" ]] || fail "real repository is not clean"

echo "Repository                 : CLEAN"
echo "HEAD                       : ${EXPECTED_HEAD}"

pkg-config --exists opencv4 || fail "opencv4 pkg-config missing"
pkg-config --atleast-version=4.5.0 opencv4 || fail "OpenCV older than 4.5"
OPENCV_VERSION="$(pkg-config --modversion opencv4)"

echo "OpenCV                     : ${OPENCV_VERSION}"

WT="$(mktemp -d /tmp/goodix5135-v5c-reproduce.XXXXXX)"
git worktree add --detach "${WT}" "${EXPECTED_HEAD}" >/dev/null
BUILD="${WT}/build-v5c"

UPSTREAM="$(mktemp -d /tmp/goodix5135-v5c-upstream.XXXXXX)"
git clone --quiet --filter=blob:none --no-checkout "${SIGFM_REPO}" "${UPSTREAM}"
git -C "${UPSTREAM}" fetch --quiet --depth=1 origin "${SIGFM_COMMIT}"
FETCHED="$(git -C "${UPSTREAM}" rev-parse FETCH_HEAD)"
[[ "${FETCHED}" == "${SIGFM_COMMIT}" ]] || fail "SIGFM commit mismatch"

git -C "${UPSTREAM}" checkout --quiet "${SIGFM_COMMIT}" -- \
    libfprint/sigfm/binary.hpp \
    libfprint/sigfm/img-info.hpp \
    libfprint/sigfm/sigfm.cpp \
    libfprint/sigfm/sigfm.hpp

mkdir -p "${WT}/libfprint/sigfm"
cp \
    "${UPSTREAM}/libfprint/sigfm/binary.hpp" \
    "${UPSTREAM}/libfprint/sigfm/img-info.hpp" \
    "${UPSTREAM}/libfprint/sigfm/sigfm.cpp" \
    "${UPSTREAM}/libfprint/sigfm/sigfm.hpp" \
    "${WT}/libfprint/sigfm/"

cat >"${WT}/libfprint/sigfm/meson.build" <<'MESON'
sigfm_opencv_dep = dependency(
    'opencv4',
    required: true,
    version: '>=4.5.0',
)

libsigfm = static_library(
    'sigfm',
    [
        'sigfm.cpp',
    ],
    dependencies: [
        sigfm_opencv_dep,
    ],
    pic: true,
    install: false,
)

sigfm_dep = declare_dependency(
    link_with: libsigfm,
    dependencies: [
        sigfm_opencv_dep,
    ],
    include_directories: include_directories('.'),
)
MESON

python3 - "${WT}/libfprint/meson.build" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

if "subdir('sigfm')" in text or "libsigfm" in text:
    raise SystemExit("SIGFM_ALREADY_PRESENT")

nbis_token = "libnbis = static_library("
nbis_pos = text.find(nbis_token)
if nbis_pos < 0:
    raise SystemExit("LIBNBIS_ANCHOR_NOT_FOUND")

text = text[:nbis_pos] + "subdir('sigfm')\n\n" + text[nbis_pos:]

start_token = "libfprint_private = static_library("
start = text.find(start_token)
if start < 0:
    raise SystemExit("LIBFPRINT_PRIVATE_START_NOT_FOUND")

open_paren = text.find("(", start)
depth = 0
quote = None
escape = False
end = None

for i in range(open_paren, len(text)):
    ch = text[i]
    if quote is not None:
        if escape:
            escape = False
            continue
        if ch == "\\":
            escape = True
            continue
        if ch == quote:
            quote = None
        continue
    if ch in ("'", '"'):
        quote = ch
        continue
    if ch == "(":
        depth += 1
    elif ch == ")":
        depth -= 1
        if depth == 0:
            end = i + 1
            break

if end is None:
    raise SystemExit("LIBFPRINT_PRIVATE_END_NOT_FOUND")

block = text[start:end]
patterns = [
    (r"link_with\s*:\s*libnbis\b", "link_with: [libnbis, libsigfm]"),
    (r"link_with\s*:\s*\[\s*libnbis\s*\]", "link_with: [libnbis, libsigfm]"),
]

matches = []
for pattern, replacement in patterns:
    for match in re.finditer(pattern, block):
        matches.append((match, replacement))

if len(matches) != 1:
    raise SystemExit(f"LIBFPRINT_PRIVATE_LINK_WITH_MATCHES={len(matches)}")

match, replacement = matches[0]
block = block[:match.start()] + replacement + block[match.end():]
text = text[:start] + block + text[end:]

if text.count("subdir('sigfm')") != 1:
    raise SystemExit("SIGFM_SUBDIR_COUNT_INVALID")
if text.count("link_with: [libnbis, libsigfm]") != 1:
    raise SystemExit("SIGFM_LINK_COUNT_INVALID")

path.write_text(text, encoding="utf-8")
print("SIGFM_SUBDIR_INSERTED=YES")
print("LIBFPRINT_PRIVATE_LINKED_SIGFM=YES")
PY

mapfile -t CHANGED < <(
    {
        git -C "${WT}" diff --name-only
        git -C "${WT}" ls-files --others --exclude-standard
    } | sed '/^$/d' | sort -u
)

EXPECTED=(
    "libfprint/meson.build"
    "libfprint/sigfm/binary.hpp"
    "libfprint/sigfm/img-info.hpp"
    "libfprint/sigfm/meson.build"
    "libfprint/sigfm/sigfm.cpp"
    "libfprint/sigfm/sigfm.hpp"
)

mapfile -t EXPECTED_SORTED < <(printf '%s\n' "${EXPECTED[@]}" | sort)

if [[ "$(printf '%s\n' "${CHANGED[@]}")" != "$(printf '%s\n' "${EXPECTED_SORTED[@]}")" ]]; then
    printf 'Actual changed paths:\n%s\n' "$(printf '%s\n' "${CHANGED[@]}")"
    fail "unexpected diff scope"
fi

echo "Diff scope                 : EXACT 6 FILES"

[[ -z "$(git -C "${WT}" diff -- libfprint/drivers/goodix5135/goodix5135.c)" ]] || fail "Goodix5135 runtime changed"
[[ -z "$(git -C "${WT}" diff -- libfprint/fpi-image-device.c)" ]] || fail "FpImageDevice runtime changed"
[[ -z "$(git -C "${WT}" diff -- libfprint/fpi-print.c)" ]] || fail "print matcher runtime changed"

meson setup "${BUILD}" "${WT}" -Ddrivers=goodix5135
meson compile -C "${BUILD}"

echo "Build                      : PASS"

SIGFM_ARTIFACTS="$(find "${BUILD}" -type f \( -name '*sigfm.cpp.o' -o -name 'libsigfm.a' \) | wc -l)"
[[ "${SIGFM_ARTIFACTS}" -ge 1 ]] || fail "SIGFM target did not build"

echo "SIGFM target               : PASS"

meson test -C "${BUILD}" --print-errorlogs \
    goodix5135-conditioning \
    goodix5135-preprocess \
    goodix5135-image \
    goodix5135-proto \
    goodix5135-image-response \
    goodix5135-io \
    goodix5135-request \
    goodix5135-queue-cleanup \
    goodix5135-async-dispatch

echo "Goodix suites              : 9/9 PASS"

# Stage only inside the disposable detached worktree. This is intentional:
# `git diff` alone does not include untracked SIGFM source files.
git -C "${WT}" add -- "${EXPECTED[@]}"

STAGED_COUNT="$(git -C "${WT}" diff --cached --name-only | wc -l)"
[[ "${STAGED_COUNT}" -eq 6 ]] || fail "staged file count is not six"

git -C "${WT}" diff --cached --check

git -C "${WT}" diff --cached --binary >"${PATCH_OUT}"
PATCH_SHA="$(sha256sum "${PATCH_OUT}" | awk '{print $1}')"
PATCH_FILE_COUNT="$(grep -c '^diff --git ' "${PATCH_OUT}" || true)"
[[ "${PATCH_FILE_COUNT}" -eq 6 ]] || fail "exported patch does not contain six file diffs"

echo "Complete patch             : ${PATCH_OUT}"
echo "Patch SHA256               : ${PATCH_SHA}"
echo "Patch file diffs           : ${PATCH_FILE_COUNT}"

[[ "$(git -C "${OLDPWD:-$PWD}" rev-parse HEAD 2>/dev/null || true)" != "" ]] || true

cd "${HOME}/libfprint"
[[ "$(git branch --show-current)" == "${EXPECTED_BRANCH}" ]] || fail "real branch changed"
[[ "$(git rev-parse HEAD)" == "${EXPECTED_HEAD}" ]] || fail "real HEAD changed"
[[ -z "$(git status --porcelain)" ]] || fail "real repository changed"

echo "Real repository            : CLEAN"
echo "USB accessed               : NO"
echo "Sensor opened              : NO"
echo "Biometric data used        : NO"
echo "Runtime matcher changed    : NO"

echo "================================================"
echo "V5C REPRODUCTION PASSED"
echo "================================================"
