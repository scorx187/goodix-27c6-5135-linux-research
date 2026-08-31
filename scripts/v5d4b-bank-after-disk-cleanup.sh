#!/usr/bin/env bash

# Guarded banking script for the cumulative V5D4B host-only SIGFM stack.
#
# IMPORTANT:
# - This script does not enable Goodix SIGFM.
# - It does not access USB/sensor/PSK/FDT/fingerprint data.
# - The previous run reached Meson setup and failed only with ENOSPC.
# - The rollback restored the real branch to 37ac6876... clean.
# - Free sufficient disk space before running again.

set +e

cd "$HOME/libfprint" || exit 1

goodix5135_bank_v5d4b_real_branch()
{
    EXPECTED_BRANCH="goodix-27c6-5135-chicagohu"
    EXPECTED_HEAD="37ac6876fd6d248b48a7892410cf75144f3882e5"

    PATCH="$HOME/goodix5135-v5d4b-sigfm-parser-hardening.patch"
    EXPECTED_PATCH_SHA="9b503c9b969c5848f7319fb86b8cdb8259be564ad6d858a41b669b0fffdaf632"
    EXPECTED_PATHS=20

    BUILD_ROOT="$(mktemp -d /tmp/goodix5135-v5d4b-bank.XXXXXX)"
    BUILD="$BUILD_ROOT/build"

    COMMITTED=0
    PATCH_APPLIED=0

    cleanup_build()
    {
        rm -rf "$BUILD_ROOT" >/dev/null 2>&1 || true
    }

    rollback()
    {
        if [ "$COMMITTED" -eq 0 ] &&
           [ "$PATCH_APPLIED" -eq 1 ]; then
            echo
            echo "===== SAFE ROLLBACK ====="
            git reset --hard "$EXPECTED_HEAD" >/dev/null 2>&1 || true
            echo "Repository restored         : $EXPECTED_HEAD"
        fi
    }

    fail()
    {
        echo
        echo "================================================"
        echo "GOODIX5135 V5D4B BANK FAILED"
        echo "Reason: $1"
        echo "================================================"
        rollback
        cleanup_build
        unset -f git 2>/dev/null || true
        unset -f meson 2>/dev/null || true
        hash -r
        echo
        echo "Branch                      : $(git branch --show-current 2>/dev/null)"
        echo "HEAD                        : $(git rev-parse HEAD 2>/dev/null)"
        echo "Status:"
        git status --short 2>/dev/null || true
        echo "Hardware touched            : NO"
        echo "Terminal still alive"
        return 1
    }

    unset -f git 2>/dev/null || true
    unset -f meson 2>/dev/null || true
    hash -r

    echo "================================================"
    echo "GOODIX5135 V5D4B"
    echo "BANK CUMULATIVE HOST-ONLY SIGFM STACK"
    echo "REAL LIBFPRINT BRANCH"
    echo "NO HARDWARE"
    echo "================================================"

    echo
    echo "===== DISK PREFLIGHT ====="
    df -h / /tmp "$HOME/libfprint" 2>/dev/null || true
    echo
    echo "Previous attempt failed with: OSError: [Errno 28] No space left on device"
    echo "If available space is still critically low, stop and clean safe build/temp artifacts first."

    echo
    echo "===== SHELL GATE ====="
    REAL_GIT="$(type -P git)"
    REAL_MESON="$(type -P meson)"
    echo "git                         : $REAL_GIT"
    echo "meson                       : $REAL_MESON"

    [ "$REAL_GIT" = "/usr/bin/git" ] || { fail "unexpected git binary"; return 1; }
    [ -x "$REAL_MESON" ] || { fail "Meson binary unavailable"; return 1; }

    echo
    echo "===== REAL REPOSITORY GATE ====="
    CURRENT_BRANCH="$(git branch --show-current)"
    CURRENT_HEAD="$(git rev-parse HEAD)"
    echo "Branch                      : $CURRENT_BRANCH"
    echo "HEAD                        : $CURRENT_HEAD"

    [ "$CURRENT_BRANCH" = "$EXPECTED_BRANCH" ] || { fail "unexpected branch"; return 1; }
    [ "$CURRENT_HEAD" = "$EXPECTED_HEAD" ] || { fail "unexpected starting HEAD"; return 1; }

    if [ -n "$(git status --porcelain)" ]; then
        git status --short
        fail "repository must be clean before banking"
        return 1
    fi
    echo "Repository                  : CLEAN"

    echo
    echo "===== PATCH INTEGRITY GATE ====="
    [ -s "$PATCH" ] || { fail "V5D4B patch missing"; return 1; }

    ACTUAL_SHA="$(sha256sum "$PATCH" | awk '{print $1}')"
    echo "Expected SHA                : $EXPECTED_PATCH_SHA"
    echo "Actual SHA                  : $ACTUAL_SHA"
    [ "$ACTUAL_SHA" = "$EXPECTED_PATCH_SHA" ] || { fail "patch SHA mismatch"; return 1; }

    PATCH_PATHS="$(grep '^diff --git ' "$PATCH" | sed -E 's#^diff --git a/([^ ]+) b/.*#\1#' | sort -u | wc -l)"
    echo "Patch paths                 : $PATCH_PATHS"
    [ "$PATCH_PATHS" -eq "$EXPECTED_PATHS" ] || { fail "expected cumulative 20-path patch"; return 1; }

    if grep '^diff --git ' "$PATCH" | grep -qE 'libfprint/drivers/.+goodix'; then
        fail "patch unexpectedly modifies Goodix driver runtime"
        return 1
    fi
    echo "Goodix driver paths         : UNCHANGED"

    git apply --check "$PATCH" || { fail "patch no longer applies cleanly"; return 1; }
    echo "Patch apply-check           : PASS"

    echo
    echo "===== APPLY TO REAL BRANCH ====="
    git apply --index "$PATCH"
    RC=$?
    [ "$RC" -eq 0 ] || { fail "could not apply cumulative patch"; return 1; }
    PATCH_APPLIED=1

    STAGED_PATHS="$(git diff --cached --name-only | wc -l)"
    echo "Staged paths                : $STAGED_PATHS"
    [ "$STAGED_PATHS" -eq "$EXPECTED_PATHS" ] || { fail "staged path count is not 20"; return 1; }

    UNSTAGED="$(git diff --name-only)"
    [ -z "$UNSTAGED" ] || { printf '%s\n' "$UNSTAGED"; fail "unexpected unstaged changes after apply"; return 1; }

    git diff --cached --check || { fail "staged patch failed diff check"; return 1; }
    echo "Staged diff check           : PASS"

    echo
    echo "===== STATIC SAFETY PROOFS ====="
    grep -q 'std::tie(this->p1.y, this->p1.x)' libfprint/sigfm/sigfm.cpp || { fail "V5D3 comparator hardening missing"; return 1; }
    grep -q 'max_serialized_bytes' libfprint/sigfm/binary.hpp || { fail "serialized-size bound missing"; return 1; }
    grep -q 'tried to read past end of stream' libfprint/sigfm/binary.hpp || { fail "stream bound guard missing"; return 1; }
    grep -q 'sigfm_info_valid' libfprint/sigfm/sigfm.cpp || { fail "SIGFM logical validation missing"; return 1; }
    grep -q 'FPI_IMAGE_DEVICE_ALGORITHM_SIGFM' libfprint/fpi-image-device.h || { fail "generic algorithm selector missing"; return 1; }

    echo "Comparator hardening        : PRESENT"
    echo "Geometry hardening          : PRESENT"
    echo "Parser bounds               : PRESENT"
    echo "Logical validation          : PRESENT"
    echo "Generic selector            : PRESENT"

    echo
    echo "===== GOODIX OPT-IN GATE ====="
    if git diff --cached --name-only | grep -qE 'libfprint/drivers/.+goodix'; then
        fail "Goodix runtime unexpectedly staged"
        return 1
    fi
    echo "Goodix SIGFM opt-in         : NO"
    echo "Live SIGFM threshold        : NOT SELECTED"
    echo "Runtime hard-min gate       : HOST ONLY / NOT LIVE-APPROVED"

    echo
    echo "===== MESON SETUP ON REAL TREE ====="
    meson setup "$BUILD" "$PWD" -Ddrivers=goodix5135
    RC=$?
    [ "$RC" -eq 0 ] || { fail "Meson setup failed"; return 1; }

    echo
    echo "===== BUILD REAL TREE ====="
    meson compile -C "$BUILD"
    RC=$?
    [ "$RC" -eq 0 ] || { fail "real-tree build failed"; return 1; }
    echo "Build                       : PASS"

    for TEST in \
        sigfm-robustness \
        sigfm-geometry-hardening \
        sigfm-image-core \
        sigfm-print-core \
        fpi-device
    do
        echo
        echo "===== TEST: $TEST ====="
        meson test -C "$BUILD" --print-errorlogs "$TEST"
        RC=$?
        [ "$RC" -eq 0 ] || { fail "$TEST failed"; return 1; }
        echo "$TEST : PASS"
    done

    echo
    echo "===== GOODIX REGRESSION 9/9 ====="
    meson test \
        -C "$BUILD" \
        --print-errorlogs \
        goodix5135-conditioning \
        goodix5135-preprocess \
        goodix5135-image \
        goodix5135-proto \
        goodix5135-image-response \
        goodix5135-io \
        goodix5135-request \
        goodix5135-queue-cleanup \
        goodix5135-async-dispatch
    RC=$?
    [ "$RC" -eq 0 ] || { fail "Goodix regression failed"; return 1; }
    echo "Goodix suites               : 9/9 PASS"

    echo
    echo "===== PRE-COMMIT FINAL GATE ====="
    [ "$(git rev-parse HEAD)" = "$EXPECTED_HEAD" ] || { fail "HEAD changed before commit"; return 1; }
    [ "$(git branch --show-current)" = "$EXPECTED_BRANCH" ] || { fail "branch changed before commit"; return 1; }

    STAGED_PATHS="$(git diff --cached --name-only | wc -l)"
    [ "$STAGED_PATHS" -eq "$EXPECTED_PATHS" ] || { fail "pre-commit staged path count changed"; return 1; }

    if [ -n "$(git diff --name-only)" ]; then
        git diff --name-only
        fail "unexpected unstaged changes before commit"
        return 1
    fi

    git diff --cached --check || { fail "pre-commit diff check failed"; return 1; }
    echo "Staged scope                : 20 PATHS"
    echo "Unstaged changes            : NONE"
    echo "Diff check                  : PASS"
    echo "Host gates                  : ALL PASS"
    echo "Goodix runtime              : UNCHANGED"

    echo
    echo "===== BANK COMMIT ====="
    git commit -m "sigfm: add hardened host-only image matching stack"
    RC=$?
    [ "$RC" -eq 0 ] || { fail "commit failed"; return 1; }

    COMMITTED=1
    PATCH_APPLIED=0

    NEW_HEAD="$(git rev-parse HEAD)"
    PARENT="$(git rev-parse HEAD^)"
    echo "New HEAD                    : $NEW_HEAD"
    echo "Parent                      : $PARENT"

    [ "$PARENT" = "$EXPECTED_HEAD" ] || { echo "ERROR: committed parent is unexpected"; cleanup_build; return 1; }

    COMMIT_PATHS="$(git diff-tree --no-commit-id --name-only -r HEAD | wc -l)"
    echo "Commit paths                : $COMMIT_PATHS"
    [ "$COMMIT_PATHS" -eq "$EXPECTED_PATHS" ] || { echo "ERROR: committed path count is not 20"; cleanup_build; return 1; }

    echo
    echo "===== POST-COMMIT REPOSITORY GATE ====="
    if [ -n "$(git status --porcelain)" ]; then
        git status --short
        echo "ERROR: repository not clean after commit"
        cleanup_build
        return 1
    fi

    echo "Branch                      : $(git branch --show-current)"
    echo "Repository                  : CLEAN"

    cleanup_build

    echo
    echo "================================================"
    echo "GOODIX5135 V5D4B BANKED"
    echo "================================================"
    echo "Previous HEAD               : $EXPECTED_HEAD"
    echo "New HEAD                    : $NEW_HEAD"
    echo "Commit paths                : 20"
    echo "Goodix regression           : 9/9 PASS"
    echo "Goodix runtime              : UNCHANGED"
    echo "Hardware                    : NOT TOUCHED"
    echo
    echo "LIVE STATUS: BLOCKED"
    echo "Resolve runtime keypoint gate (5 vs 25) and choose a dedicated SIGFM threshold before any live run."
    echo "================================================"
    return 0
}

goodix5135_bank_v5d4b_real_branch
STATUS=$?

echo
echo "================================================"
echo "Script status: $STATUS"
echo "Terminal still alive"
echo "================================================"

exit "$STATUS"
