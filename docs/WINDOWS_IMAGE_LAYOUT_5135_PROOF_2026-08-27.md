# Windows live-image layout proof — Goodix 27c6:5135

Date: 2026-08-27

## Scope

This document records the static Windows proof that the full 5135 live image is decoded/regrouped into the same 16-bit spatial layout consumed alongside the persisted `ImageBase`. It also records the now-resolved post-detection callback and the boundary where `gfusb.dll` hands the image package to the next Windows layer.

No proprietary binary, fingerprint bytes, image values, OTP, PSK material, private calibration data, or unit-specific runtime configuration is included here.

## Conclusion

For logical chip `0x2504`, the Windows path is:

```text
packed RAW12 image (0x1e00 bytes)
        -> Windows image CRC check
        -> Chicago decode/regroup routine
        -> 80x64 / 5120-sample 16-bit live-image buffer
        -> same-index comparison with persisted ImageBase
        -> post-detection callback packages ImageBase + live image unchanged
        -> request output buffer
        -> next Windows biometric layer
```

The persisted `ImageBase` is already in the downstream regrouped spatial order. Applying the Chicago regroup transform to `ImageBase` a second time is incorrect.

**Candidate A: PROVEN.**

**Candidate B (regroup ImageBase again): rejected.**

The examined `gfusb.dll` post-detection callback does **not** perform baseline subtraction, normalization, clamp/saturation, crop, or other per-pixel matcher preparation. It packages the already-regrouped ImageBase/live planes and completes the pending request. Exact matcher preprocessing therefore lies in the consumer above this driver handoff, not in this callback.

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

## Post-detection callback resolved

Runtime slot `0x18059cb60` is registered through setter `0x1800621b8`.

The registration call loads:

```text
RCX = 0x180013280
DL  = 1
call 0x1800621b8
```

and the setter stores the supplied RCX value into `0x18059cb60`. Therefore the concrete post-detection callback is:

```text
0x18059cb60 -> 0x180013280
```

Capture call sites invoke it with the effective signature:

```text
callback(context, ImageBase, live_image, flags)
```

## What callback 0x180013280 actually does

`0x180013280` allocates a large local package, copies both image planes into that package using the family image-plane byte count, fills metadata/flags, and submits the package through `0x18001393c`.

Critically, between entry and submission there is no per-pixel arithmetic over ImageBase/live data: no baseline subtraction loop, no normalization loop, no clamp/saturation loop, and no crop transform. The two image planes are copied as blocks.

For the 5135 path the package submitted to `0x18001393c` is `0xeb88` bytes total, with `0xeb70` bytes of payload passed by pointer.

## Request-completion boundary at 0x18001393c

`0x18001393c` is a request-output/completion helper, not an image-processing routine.

On the successful payload path it:

1. locks the request/context synchronization object;
2. verifies a pending request at `context + 0x188` and output buffer at `context + 0x198`;
3. writes output metadata including total result size `0xeb88` and payload length `0xeb70`;
4. copies exactly `0xeb70` bytes from the supplied package pointer into output buffer offset `+0x14`;
5. completes the pending request via `0x18001d64c` with the supplied status/length;
6. clears the pending request/output-buffer pointers and request-active flag.

Other call sites use the same helper to complete requests with Windows error statuses such as `0xc0000120` and `0xc000000d`, further confirming that this is a generic request completion path.

Therefore the full 5135 image pair crosses the `gfusb.dll` boundary as a packaged request result. Matcher/feature-extractor preprocessing, if any, occurs in the higher Windows component consuming this result.

## Current next target

Do **not** keep searching `0x180013280` or `0x18001393c` for matcher preprocessing.

The next task is to identify the Windows component that opens/reads this driver interface and consumes the `0xeb88` image result. From that consumer, trace the two 10240-byte 80x64 `u16` planes into the actual matcher/feature extractor and prove, if present:

- baseline subtraction direction;
- clamp/saturation rules;
- normalization or gain scaling;
- additional per-pixel calibration;
- crop/output dimensions;
- any role of `goodix_calib.dat`;
- exact matcher input buffer.
