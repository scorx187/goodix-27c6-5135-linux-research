# Checkpoint — V5D4B banking attempt — disk full / safe rollback

**Date:** 2026-08-31

## Purpose

Record the first attempt to bank the already-verified cumulative V5D4B host-only SIGFM stack into the real local libfprint branch.

## Starting state

- local repo: `~/libfprint`
- branch: `goodix-27c6-5135-chicagohu`
- starting HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- starting repository: CLEAN
- cumulative patch: `~/goodix5135-v5d4b-sigfm-parser-hardening.patch`
- patch SHA256: `9b503c9b969c5848f7319fb86b8cdb8259be564ad6d858a41b669b0fffdaf632`
- patch paths: 20

## Gates that passed before build setup

- expected branch: PASS
- expected HEAD: PASS
- clean working tree: PASS
- patch SHA: PASS
- cumulative 20-path scope: PASS
- Goodix driver paths unchanged: PASS
- `git apply --check`: PASS
- patch applied with `--index`: PASS
- staged paths: 20
- staged `git diff --check`: PASS
- V5D3 comparator hardening present: PASS
- geometry hardening present: PASS
- parser bounds present: PASS
- logical SIGFM validation present: PASS
- generic NBIS/SIGFM selector present: PASS
- Goodix SIGFM opt-in: NO
- live SIGFM threshold: NOT SELECTED

## Failure

Meson setup failed before compilation with the environment error:

```text
OSError: [Errno 28] No space left on device
```

The traceback occurred while Meson attempted to create build metadata (`.hgignore`) in the temporary build directory.

This failure does **not** invalidate the V5D4B source stack. V5D4B had already passed in an isolated worktree immediately before banking:

- Build PASS
- SIGFM robustness PASS
- SIGFM geometry PASS
- SIGFM image PASS
- SIGFM print PASS
- `fpi-device` PASS
- Goodix 9/9 PASS

## Rollback

The banking wrapper invoked its safe rollback because no bank commit had been created:

```text
git reset --hard 37ac6876fd6d248b48a7892410cf75144f3882e5
```

Confirmed final state:

- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- working tree: CLEAN
- bank commit created: NO
- hardware touched: NO

## Next action

Before retrying banking:

1. inspect/free disk space on the filesystem backing `/tmp` and the build/repository paths
2. retain all research patches, checkpoints, and consumed guard files
3. re-confirm clean baseline SHA
4. re-confirm cumulative V5D4B patch SHA
5. retry banking with fresh build directory
6. commit only after all host regressions are green again

Do not enable Goodix SIGFM or perform a live fingerprint test as part of the banking retry.
