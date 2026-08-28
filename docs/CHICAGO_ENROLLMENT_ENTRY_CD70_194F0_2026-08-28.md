# Chicago enrollment entry: `0x18000cd70` -> `0x1800194f0`

Date: 2026-08-28

## Scope

Static analysis of `AlgoChicago.dll` for the family used by Goodix `27c6:5135` / ChicagoHU. No private biometric data, PSK, OTP, calibration payload, or proprietary binary was added to this repository.

## Exported enrollment path

`enrolAddImageWrapper` at `0x18000b6b0` is an argument-shuffling wrapper around `0x18000cd70`.

`0x18000cd70` is the outer enrollment-capture orchestrator. It is not itself the deep merge/comparison engine.

## Input validation in `0x18000cd70`

The function validates the input descriptor before feature construction:

- descriptor byte `+0x0e == 8`
- descriptor byte `+0x0f == 1`
- descriptor word `+0x18 != 0`
- descriptor data pointer is non-null
- the enrollment/context pointer chain is non-null

It allocates a temporary copy of the input image plane and copies input quality/coverage bytes from descriptor `+0x28/+0x29` into the caller output structure.

## Shared representation construction

If the enrollment state still has capacity (`state+0x0a < state+0x08`), `0x18000cd70` calls the already-mapped shared builder `0x180016920` to build the current fingerprint feature object.

If the builder fails, `0x18000cd70` returns `0x80000001` after cleanup.

## Decisive enrollment child

On successful feature construction, `0x18000cd70` calls:

`0x1800194f0`

with the newly built fingerprint object plus persistent enrollment/context state.

This is the first decisive post-builder enrollment function and is the current target for full logical-CFG recovery.

The first PE runtime entry is not a safe logical boundary: the automatic child dump showed control flow continuing beyond that entry (for example toward `0x18001977e`). The function must therefore be recovered across split `RUNTIME_FUNCTION` entries exactly as was required for matcher functions such as `0x5baf0`.

## Success-path state mutation after `0x1800194f0`

Only when `0x1800194f0` returns success does `0x18000cd70` perform the outer enrollment-state update:

- increment `WORD state+0x0a` (accepted enrollment capture count)
- update `state+0x10` from the high byte of the packed result DWORD returned through the local output from `0x194f0`
- update `state+0x14` from the low byte of that packed result DWORD
- update `state+0x0c = min(100, accepted_count * 100 / target_count)`

Therefore `0x194f0` decides whether the newly built capture can be accepted/merged before progress is advanced.

## Error/status observations

`0x18000cd70` uses, among others:

- `0x81` for invalid/null/unsupported input conditions
- `0x82` for temporary allocation failure
- `0x80000001` for feature-builder failure
- `0x83` when `0x194f0` reports a nonzero enrollment update failure
- `0` on the normal accepted path

## Next target

Recover the full logical CFG of `0x1800194f0` across all unwind entries and determine:

1. how it compares the new capture against existing enrollment state;
2. whether and where it invokes the already-mapped matcher/correspondence pipeline indirectly;
3. the accept/reject and duplicate/coverage rules;
4. the meaning of its packed output DWORD;
5. the exact persistent template mutation/merge child;
6. capacity/full-template behavior and status codes.
