# Windows live-image layout proof — Goodix 27c6:5135

Date: 2026-08-27

## Scope

This document records the static Windows proof that the full 5135 live image is decoded/regrouped into the same 16-bit spatial layout consumed alongside the persisted `ImageBase`. It closes the previous Candidate A vs Candidate B ambiguity.

No proprietary binary, fingerprint bytes, image values, OTP, PSK material, private calibration data, or unit-specific runtime configuration is included here.

## Conclusion

For logical chip `0x2504`, the Windows path is:

```text
packed RAW12 image (0x1e00 bytes)
        -> Windows image CRC check
        -> Chicago decode/regroup routine
        -> 80x64 / 5120-sample 16-bit live-image buffer
        -> same-index comparison with persisted ImageBase
```

The persisted `ImageBase` is therefore already in the downstream regrouped spatial order. Applying the Chicago regroup transform to `ImageBase` a second time is incorrect.

**Candidate A: PROVEN.**

**Candidate B (regroup ImageBase again): rejected.**

## 0x2504 family registration

The 0x2504 family-selection path chooses family/type `0x0c` and invokes the Chicago-family initializer at VA `0x1800266a4` with object base `0x180588dc0`.

That initializer installs, among other callbacks:

```text
object + 0x13d50 -> 0x1800288b0
object + 0x13d48 -> 0x1800289f8
```

The `0x1800289f8` callback is the full-image path relevant to the 5135 packed image length.

## Full-image callback data flow

Inside `0x1800289f8`:

1. the received image block is checked by the Windows image CRC checker;
2. on CRC success, the code selects packed length `0x1e00` (7680 bytes);
3. it calls the Chicago decode/regroup routine at `0x180023e38` with the received packed image as source and a temporary output buffer as destination;
4. it then copies the resulting 16-bit image plane from that temporary buffer into the pointer stored at `object + 0x13cc0`.

The family image-plane length used for that final copy is the same per-family 16-bit image-size entry used elsewhere for the 5135 `80 * 64 * 2 = 10240` byte plane.

## The key address identity

For this family object:

```text
object base        = 0x180588dc0
field offset       =      0x13cc0
--------------------------------
field address      = 0x18059ca80
```

`0x18059ca80` is the runtime QWORD that receives the allocated live-image buffer pointer.

Therefore these are not two independent buffers or two unrelated paths:

```text
object->field_13cc0
```

and

```text
QWORD [0x18059ca80]
```

are the same field.

The Chicago full-image callback writes the decode/regroup result into the exact live-image buffer later exposed through `0x18059ca80`.

## Capture-side propagation

The capture wrapper calls the image/state dispatcher and then copies from the buffer pointer stored at `0x18059ca80` into its caller-provided live-image buffer using the per-family 16-bit plane size.

The capture routine later passes:

```text
RCX = persisted ImageBase pointer (runtime slot 0x18059ca88)
RDX = captured live-image buffer
```

into wrapper `0x180023acc`, which forwards to the base/live classifier at `0x180022178`.

That classifier reads both images at the exact same pixel index. No transpose/regroup is performed inside the comparison routine.

## Persisted ImageBase side

The persisted IMAGE block is loaded directly into the runtime ImageBase allocation at `0x18059ca88` using its image-plane byte count. The corresponding save/update paths also copy the image plane directly; no Chicago regroup operation is applied on load or save.

Combined with the proven live path above, this establishes that the persisted ImageBase and live Chicago-regrouped image are already in the same index order.

## Related geometry

Keep transport and downstream geometry distinct:

```text
packed transport fast axis : 64
packed transport slow axis : 80
packed samples             : 5120
packed bytes               : 7680

Chicago regroup output     : 80 columns x 64 rows
16-bit output bytes        : 10240
```

The proven regroup index mapping is:

```text
dst = (n % 64) * 80 + (n / 64)
```

for the decoded stream index `n`.

## Detector role

The base/live routine at `0x180022178` is a detector/classifier, not the matcher preprocessing output stage. It computes same-index tile statistics and directional differences and returns one of four Windows-observed result labels:

```text
0 -> temperature
1 -> finger down
2 -> void
3 -> bad
```

This detector is now sufficiently understood for image-layout purposes and should not be the focus of further preprocessing work.

## What remains unproven

This proof does **not** yet establish the exact image preparation handed to the biometric matcher. Remaining work includes locating the post-detection processing callback and proving, if present:

- baseline subtraction direction;
- clamp/saturation rules;
- normalization or gain scaling;
- additional per-pixel calibration;
- crop/output dimensions;
- any role of `goodix_calib.dat`;
- exact buffer passed into the matcher/feature extractor.

## Immediate next target

The capture path calls runtime callback slot `0x18059cb60` with the runtime object, persisted ImageBase, captured live image, and a mode/flag byte. The next static-analysis task is to resolve who registers that callback and then inspect its concrete target as the likely post-detection preprocessing path.
