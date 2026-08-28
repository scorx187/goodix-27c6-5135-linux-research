# AlgoChicago type-0x0c fallback matcher score closure — 2026-08-28

Scope: static analysis of `AlgoChicago.dll` only. No biometric data, fingerprint images/templates, PSK, OTP, calibration payloads, `goodix.dat`, or private runtime data were accessed or published.

## Closed path

The general/fallback matcher path for family/type `0x0c` is now structurally closed enough to implement without treating opaque helpers as score multipliers.

High-level path:

`0x293c0 -> 0x27490 -> 0x53b00 -> 0x51780 / 0x53c90 -> 0x28500`

### `0x27490`

`0x27490` is a real fallback/final score finalizer, not a thin wrapper.

- If the probe object has zero features (`probe+0xf0 == 0`), it writes score `0` and returns status `0x80000006`.
- On the normal path, `EAX` is an execution/status result (normally zero); the match score is written via the first pointer argument.
- It iterates candidate/template subobjects, recomputes correspondence/inlier evidence through `0x1f840`, requires more than four surviving inliers, obtains spatial/image similarity through `0x50c80`, computes additional transform/overlap metrics, and selects the best candidate lexicographically by its stored quality metrics.
- The selected winner is passed through `0x53b00`, then `0x53c90`, then `0x28500` for the type-specific final score gate.

### `0x51780`

`0x51780` does not multiply the final score. It adds three Q8 binary-overlap ratios to the selected score-feature structure.

Its decisive child `0x54250` builds four counts for paired binary samples. The histogram index is:

`index = B + 2*A`

so the four counters are:

- `n00`: A=0, B=0
- `n01`: A=0, B=1
- `n10`: A=1, B=0
- `n11`: A=1, B=1

`0x51780` then writes:

- `out+0x18 = round_Q8((n00+n11)/(n00+n01+n10+n11))`
  - binary agreement ratio.
- `out+0x1c = round_Q8(n00/(n00+n01+n10))`
  - Jaccard/IoU of the zero-bit sets.
- `out+0x20 = round_Q8(n11/(n11+n01+n10))`
  - Jaccard/IoU of the one-bit sets.

The integer rounding pattern is the DLL pattern used here:

`((numerator << 8) + denominator/2) / denominator`

for positive denominators; zero denominator yields zero in these branches.

For type `0x0c`, `out+0x20` is used as an acceptance/gating metric by `0x28500`; it is not multiplied into the final score.

### `0x28500` for type `0x0c`

Type `0x0c` is in the `0x413000` mask, so its normalization divisor is `42`.

The relevant score-feature fields on the type-0x0c branch are:

- `+0x04`: copied inlier/correspondence count from the selected winner.
- `+0x14`: selected spatial/quality metric from the `0x27490` candidate comparison.
- `+0x20`: one-bit Jaccard/IoU Q8 value from `0x51780`.
- `+0x24`: selected scaled auxiliary similarity metric from `0x27490`.

`+0x14`, `+0x20`, and `+0x24` participate in threshold combinations that decide whether the fallback candidate is acceptable. They are gates, not multiplicative score factors.

When a type-0x0c acceptance combination succeeds, `0x28500` computes the positive score from `+0x04` only, using the type divisor `42`:

```text
q8 = ((field_04 << 8) + 21) / 42
score = (q8 * 100) >> 8
```

This is the same integer normalization family seen in the direct type-0x0c score path. Conceptually it is approximately:

`score ~= 100 * field_04 / 42`

but Linux reimplementation should preserve the exact integer operation order rather than use floating-point arithmetic.

If the acceptance gates fail, `0x27490` writes a small negative encoded rejection value rather than a positive similarity score.

## Practical conclusion

For family/type `0x0c`, both the direct and fallback positive matcher scores are fundamentally normalized surviving-correspondence/inlier counts. Spatial/image/overlap metrics determine whether a candidate is accepted and which candidate wins, but they are not continuous multipliers in the final positive score formula.

This closes the fallback score-fusion path sufficiently for the current reconstruction. Remaining matcher helpers may still be revisited later for bit-exact threshold behavior, but they are no longer blockers for understanding the type-0x0c score architecture.

## Next major target

Move to the enrollment/update path beginning at `enrolAddImageWrapper -> 0x18000cd70`, focusing on how successive captures are accepted, merged, and written into the persistent template representation.
