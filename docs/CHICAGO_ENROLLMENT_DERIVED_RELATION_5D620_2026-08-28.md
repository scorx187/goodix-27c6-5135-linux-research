# Chicago enrollment derived relation materializer — `0x18005d620`

Date: 2026-08-28
Target: `AlgoChicago.dll`, family/type `0x0c` (Chicago)
Method: static disassembly only.

## Result

`0x18005d620` is not a matcher and does not re-evaluate minutiae. It is a generic relation-record accessor/materializer for the enrollment multi-capture graph.

The relation store uses 0x1c-byte records:

- `+0x00`: relation strength/state value
- `+0x04..+0x1b`: 24-byte geometric transform

The function accepts two capture indices and supports both relation retrieval and relation materialization. It canonicalizes the unordered pair using the per-capture `+0x104` relation-base offset and uses `0x180042ea0` when transform orientation must be inverted.

## Logical function boundary

The logical body spans:

- `0x18005d620 .. 0x18005d8fe`
- 211 reachable instructions
- 9 PE runtime/unwind entries
- no missing direct CFG targets
- no indirect jumps

This is another split-unwind function and must not be truncated at the first runtime-function boundary.

## Arguments

Windows x64 ABI recovered from the callers and callee stack reads:

- `RCX`: capture-slot pointer array
- `RDX`: persistent 0x1c relation-record store
- `R8`: optional/external relation-record source
- `R9D`: capture count / external-node index boundary
- arg5: source capture index
- arg6: destination capture index
- arg7: mode (`0` read/retrieve, `1` materialize/write)
- arg8: 24-byte transform buffer
- arg9: pointer to relation strength/state DWORD

Invalid same-index or negative-index requests return `-1`.

## Read mode (`arg7 == 0`)

The function clears the output transform and initializes the output relation value to `-1`, then retrieves the canonical pair record.

Depending on index order it either:

- copies the stored 24-byte transform directly with the memcpy helper, or
- runs `0x180042ea0` to produce the reverse-direction transform.

It then returns the record's `+0x00` value through arg9 and returns `1`.

If one endpoint is the external boundary index (`index == R9D`), it can instead read from the optional `R8` relation source.

## Materialize/write mode (`arg7 == 1`)

No descriptor comparison, minutia traversal, or geometric re-estimation occurs.

For an internal capture pair the function:

1. Computes the canonical 0x1c record address using one capture's `+0x104` relation-base offset plus the other capture index.
2. Writes the supplied 24-byte transform to record `+0x04`, direct or inverted depending on index ordering.
3. Writes `*arg9` to record `+0x00`.
4. Returns `1`.

There is one canonical relation record per unordered pair; the reverse direction is represented implicitly by transform inversion, not by writing a second inverse record.

## Enrollment-finalizer call from `0x18005bf40`

Immediately before the call, `0x18005bf40` composes transforms through an indirect graph path with `0x180043460`.

The finalizer initializes its local relation-state value exactly once:

```text
[rbp-0x40] = 2
```

and later passes `&[rbp-0x40]` as arg9 to `0x18005d620` in materialize mode (`arg7 = 1`). No intervening write to that local was found.

Therefore graph-closure-created relations are materialized as:

```text
record +0x00 = 2
record +0x04..+0x1b = composed 24-byte transform
```

So value `2` is the exact marker/state used by this finalizer for a derived/indirect relation.

## Architectural consequence

Enrollment completion does not fuse all minutiae into one list. The persistent representation remains:

```text
multiple capture fingerprint objects
+ pairwise 0x1c geometric-relation graph
```

Direct pair relations originate from earlier pairwise matching/inlier analysis. At completion, `0x18005bf40` performs graph closure; missing relations may be synthesized by transform composition and are then persisted through `0x18005d620` with relation value `2`.

## Safety

Static `AlgoChicago.dll` analysis only. No fingerprint image, enrolled template, PSK, OTP, calibration payload, `goodix.dat`, Windows biometric database, or unit-specific private biometric material was accessed or published.
