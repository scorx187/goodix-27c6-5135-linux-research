# GitHub integration plan — image-transport milestone

The previous chat could not push because the GitHub connector became disabled. This checkpoint contains a real local git commit based on the repository snapshot that was available in that chat.

## In the next chat

1. Open `scorx187/goodix-27c6-5135-linux-research`.
2. Read current `AI_START_HERE.md` and inspect `main` HEAD before writing.
3. Do not blindly overwrite if `main` has advanced beyond the checkpoint parent.
4. Compare the supplied bundle/ZIP commit with current `main`.
5. If current `main` is still the checkpoint parent, fast-forward/push the checkpoint commit.
6. If `main` has advanced, cherry-pick or selectively merge the checkpoint changes, resolving docs rather than discarding newer facts.
7. Check that no private/biometric material is staged.
8. Commit/push and report the final GitHub SHA.

## Mandatory exclusion audit

Before push, search staged files for accidental:

- PSK plaintext/files/hashes;
- full OTP;
- full per-device 224-byte runtime config / private hash;
- fingerprint raw/image/template data;
- `goodix.dat`, `Goodix_Cache.bin`;
- Windows binaries/dumps.

The checkpoint intentionally contains only public-safe protocol facts and scripts that expect private local inputs.
