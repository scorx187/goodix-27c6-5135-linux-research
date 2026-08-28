# Chicago type-0x0c matcher — fallback/final score path checkpoint (2026-08-28)

Target device family/type: `0x0c` (`AlgoChicago.dll`).

This checkpoint records static reverse-engineering results only. No proprietary Goodix binary, fingerprint image/template, device PSK/OTP, unit-specific runtime configuration, Windows biometric database material, or private biometric payload is committed.

## Direct score path correction

The earlier hypothesis that `0x180024c70` returns a normalization factor was wrong.

`0x24c70` sorts/reorders the candidate-subobject index array at `container+0x87f0` using descending lexicographic priority:

1. `subobject+0x100`
2. `subobject+0x124`
3. `subobject+0x11c`

Its return value is not consumed by the direct score formula. Immediately after the call, the three-operand instruction

```text
imul eax, DWORD PTR [rbp-0x7c], 0x64
```

overwrites `EAX` completely.

For type `0x0c`, the direct score arithmetic is therefore:

```text
q8_i = ((match_record_i[+0x04] << 8) + 21) / 42
accumulator = sum(q8_i for accepted candidates)
direct_score = ((accumulator * 100) / accepted_count) >> 8
```

All operations are integer operations with the same truncation/rounding order as the Windows implementation.

## `0x180027490` — general/fallback score finalizer

Full CFG recovery proves one logical function across three unwind entries:

```text
0x180027490 .. 0x180027883
263 reachable instructions
```

Only direct caller: `0x1800293c0`.

Effective call contract from that caller:

```text
RCX  = score/result output pointer
RDX  = probe fingerprint object
R8   = matcher workspace / per-candidate state
R9   = candidate/template container
arg5 = matcher context/policy object
arg6 = per-candidate selection/status array
arg7 = optional output/context
```

If `probe+0xf0 == 0`, it writes score `0` and returns status `0x80000006`.

Otherwise, normal completion returns `EAX=0`; the match score/result is written through the first argument.

Thus `EAX` is execution/status, not the match score.

## Per-subobject fallback evaluation

For each enabled candidate subobject, `0x27490`:

1. calls `0x18001f840` and requires more than four surviving correspondence inliers;
2. applies policy helper `0x18002c1e0`;
3. calls `0x180050c80` for the already-known spatial/image similarity channel;
4. derives additional transform/relation metrics through `0x180053c90`;
5. requires the spatial score to exceed a context threshold and another metric to exceed `0xc3` (195);
6. optionally applies small penalties to that metric from transform-derived conditions;
7. ranks surviving subobjects lexicographically by:
   - primary adjusted metric,
   - then surviving inlier count,
   - then a scaled auxiliary metric;
8. stores the winning subobject index and its transform/work state.

The loop also tracks:

- number of accepted subobjects;
- sum of accepted inlier counts;
- maximum adjusted primary metric.

## Winner reconstruction before final score

After selecting a winner, `0x27490` calls:

```text
0x180053b00(probe, winning_subobject, winning_transform, context, score_feature_struct)
0x180053c90(winning_transform, transform_metric_struct)
```

`0x53b00` is a wrapper that prepares spatial/image representations and delegates the decisive score-feature construction to `0x180051780`.

After these calls, `0x27490` adds boolean transform-quality fields and calls:

```text
0x180028500(score_feature_struct, matcher_context)
```

## `0x180028500` — type-specific score gate/normalizer

The automatic child dump is sufficient to reduce its type-`0x0c` branch structurally.

For type `0x0c`:

- bit 12 is set in mask `0x00413000`, so normalization divisor `0x2a` (42) is selected;
- bit 12 is clear in mask `0x07a20ca0`, so type `0x0c` follows the normal branch beginning around `0x18002867c`.

The branch tests a set of fields from the score-feature structure, including `+0x04`, `+0x14`, `+0x20`, and `+0x24`, against several acceptance combinations.

If none of the combinations passes, it returns zero.

If one passes, its final normalization for type `0x0c` is equivalent to:

```text
score = ((((field_04 << 8) + 21) / 42) * 100) >> 8
```

The result is then clamped by `0x27490` to at most `100` and written to `*score_out`.

The exact semantic names of the `0x28500` structure fields are not yet all closed, so do not over-name them.

## Negative fallback result encoding

If `0x28500` does not produce a positive score, `0x27490` writes a small negative encoded result rather than a positive match score. The encoding combines three boolean failure/quality flags and negates the packed value. This is a rejection/failure-state encoding, not a biometric similarity score.

## Immediate next target

Do **not** re-decode `0x28500`; its complete body was already captured.

The remaining decisive unknown for the type-`0x0c` fallback path is:

```text
0x180051780
```

`0x180053b00` prepares the two object/image representations and passes an output score-feature structure to `0x51780`. That structure is later consumed by `0x28500`, including the currently unnamed fields used by the type-`0x0c` threshold combinations.

Next task: recover `0x51780` and map exactly how it populates the score-feature fields consumed by `0x28500`, especially offsets `+0x14`, `+0x20`, and `+0x24`.
