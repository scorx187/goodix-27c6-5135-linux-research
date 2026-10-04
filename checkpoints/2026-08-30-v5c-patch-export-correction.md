# V5C patch export correction

The first local V5C run reported:

- local patch path: `~/goodix5135-v5c-sigfm-meson-scaffold.patch`
- SHA256: `8eeb896ed9ab91f8a106e4158f90e3cf8789d748e5568640f3e07921de6148e9`

Important correction: that run exported the patch with `git diff --binary` while the newly added `libfprint/sigfm/*` files were still untracked.

Plain `git diff` does not include untracked files. Therefore the original local patch must **not** be treated as a complete six-file recovery artifact, even though the V5C build and tests themselves were valid because those files existed in the temporary worktree.

The preserved recovery script on this branch fixes the artifact export by:

1. verifying the exact six-file scope,
2. staging those files only inside the disposable detached worktree,
3. running `git diff --cached --check`,
4. exporting with `git diff --cached --binary`, and
5. asserting that the resulting patch contains exactly six `diff --git` entries.

Use `scripts/reproduce-v5c-sigfm-scaffold.sh` to recreate a complete V5C patch from the exact libfprint HEAD and pinned upstream SIGFM commit.

This correction does not invalidate the earlier V5C build result:

- build: PASS
- SIGFM target: PASS
- Goodix tests: 9/9 PASS
- runtime behavior: unchanged
- hardware: not touched
