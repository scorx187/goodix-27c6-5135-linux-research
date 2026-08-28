# AI / developer handoff — START HERE

## Goal

Make Goodix USB fingerprint sensor `27c6:5135` work natively under Linux/libfprint **without breaking Windows Hello, factory firmware, factory PSK provisioning, or existing Windows fingerprints**.

## Exact tested device

```text
USB VID:PID: 27c6:5135
Firmware:    GF_HC460SEC_APP_12508
Chip ID:     raw a2042500 / logical 0x2504
Profile:     ChicagoHS / ChicagoHU
Sensor type: 12 / family 0x0c
Packed axis: 64 fast x 80 slow
Output plane:80 columns x 64 rows
Pixels:      5120
USB bulk IN: 0x81
USB bulk OUT:0x01
```

## Canonical current checkpoint

Read first:

1. `docs/CURRENT_STATUS_2026-08-28.md`
2. `docs/CHICAGO_PREPROCESS_CORE_2026-08-28.md`
3. `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`
4. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
5. `docs/DEVELOPER_ROADMAP.md`
6. `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`
7. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
8. `docs/FDT_5135_PROOF_2026-08-27.md`
9. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
10. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
11. `docs/SAFETY.md`

Older handoff/current-status documents are historical evidence and may describe blockers already solved.

## Current state — do not repeat solved work

```text
CFG70 reconstruction                         DONE
command 0x90 upload                          DONE
factory TLS                                  DONE
verified activation sequence                 DONE
FDT manual/down/up                           DONE on tested unit
image transport/decrypt                      DONE
Windows-compatible image CRC                 DONE on one private capture
RAW12 -> 5120 samples                        DONE
ChicagoHU regroup                            PROVEN
ImageBase/live relative layout               PROVEN
Candidate A                                  PROVEN
Candidate B double-regroup                   REJECTED
gfusb.dll post-detection packaging           PROVEN
gfusb.dll Windows request handoff             PROVEN
Windows engine component                     IDENTIFIED: EngineAdapter.dll
0x2504 / family 0x0c algorithm selection     PROVEN: AlgoChicago.dll
AlgoChicago preprocessor export              PROVEN: RVA 0x0000b560
outer semantic preprocessor routine          PROVEN: 0x18000e780..0x18000e947
core preprocessing orchestrator              PROVEN: 0x1800484e0
first source/state subtraction               PROVEN: 0x180044970
mask geometry/source-range cleanup            PROVEN: 0x180043c40
post-mask correction stage                   PROVEN: 0x18004aea0
persistent corrected plane                   PROVEN: state+0x13244
primary Q13 correction surface               PROVEN: scratch_A
type-0x0c late scratch_A composition         PROVEN
type-0x0c scratch_B copy/compose logic       PROVEN
current unresolved helper                    0x18004d6f0
matcher/enrollment                           LATER
libfprint/fprintd integration                 LATER
release/safety matrix                        FINAL STAGE
```

## Preprocessing proof so far

### 1. Signed source/state difference

`0x1800484e0` copies the source u16 image and an AlgoChicago internal plane from `state+0x9924`, then `0x180044970` computes for type `0x0c`:

```text
diff16[i] = source_u16[i] - state_plane_u16[i]
```

The difference is intentionally signed 16-bit. Do **not** yet equate `state+0x9924` with gfusb persisted ImageBase.

### 2. Mask cleanup / coverage

`0x180043c40` cleans mask geometry and applies source-range filtering. For type `0x0c`, a mask pixel is rejected when:

```text
source <= 100
or
source >= 3800
```

This stage is mask/coverage logic, not image normalization.

### 3. Post-mask parent `0x18004aea0`

The parent allocates full-size u16 planes `scratch_A`, `scratch_B`, `scratch_C`.

For type `0x0c`:

```text
scratch_C[i] = 3 * source_side_word[i]
global_0x1800ff920[i] = 3 * state_plus_0x9924[i]
```

It then runs `0x180049ba0` and writes a persistent corrected image-like plane at:

```text
state + 0x13244
```

For type `0x0c` the parent uses shift 14:

```text
if gate_word[i] != 0:
    if scratch_A[i] == 0:
        processed[i] = scratch_C[i] << 14
    else:
        processed[i] = round((scratch_C[i] << 14) / scratch_A[i])
else:
    processed[i] = scratch_C[i]
```

`processed = state+0x13244`.

### 4. Q13 surfaces inside `0x180049ba0`

For type `0x0c`, `scratch_A` is initialized to:

```text
0x2000
```

per WORD, i.e. unity in Q13.

Repeated scalar and SIMD composition is exactly:

```text
Q13_mul(a,b) = (a*b + 0x1000) >> 13
```

Descriptor/static surface mapping relevant to the tested branch:

```text
scratch_A = descriptor+0x00
scratch_B = descriptor+0x30
F0 = 0x1801151b0
F1 = 0x18010b890
F2 = 0x18011ead0
flag_G = *(u32 *)0x1800ff91c
```

Late type-0x0c `scratch_B` behavior is proven:

```text
if local_flag != 0:
    scratch_B = copy(scratch_A)
else if flag_G != 0:
    scratch_B = copy(scratch_A)
else:
    scratch_B[i] = Q13_mul(scratch_A[i], F2[i])
```

Late type-0x0c `scratch_A` behavior is proven:

```text
if flag_G != 0:
    scratch_A[i] = Q13_mul(scratch_A[i], F0[i])
else:
    scratch_A[i] = Q13_mul(Q13_mul(scratch_A[i], F0[i]), F1[i])
```

Therefore the unresolved part is no longer the final Q13 composition. The unresolved part is the **base value of `scratch_A` before these late factors**.

## Immediate next task

**Reverse exactly the PE runtime-function containing `AlgoChicago.dll 0x18004d6f0`.**

Reason: on the tested selector `0x0c` path, when the surrounding condition enables the correction branch, `0x180049ba0` calls:

```text
0x18004d6f0(
    RCX = 0x1800ff920,
    RDX = temporary full-image/work buffer,
    R8  = scratch_A,
    R9D = one geometry dimension,
    stack = other geometry dimension + selector 0x0c
)
```

So `0x18004d6f0` is the first unresolved helper that directly receives the primary Q13 denominator surface.

Need to prove:

1. exact runtime-function boundary;
2. argument roles from first reads/writes;
3. whether `R8 = scratch_A` is output or in/out;
4. exact per-pixel/spatial formula written to it;
5. whether a deeper child actually creates the factor plane; descend only into that decisive child if necessary;
6. only after this inspect `0x1800497c0` / `0x18004b460` if the type-0x0c data flow proves they further modify the denominator;
7. later trace `state+0x13244` into matcher/enrollment-facing processing;
8. independently prove the producer of `state+0x9924` before equating it with gfusb ImageBase.

Do not return to transport/TLS/CRC/regroup/gfusb callbacks, `0x180044970`, or `0x180043c40` unless a newly discovered dependency requires it.

## Windows biometric architecture checkpoint

```text
Goodix 27c6:5135
 -> gfusb.dll UMDF sensor driver
 -> Windows WinBioSensorAdapter.DLL
 -> EngineAdapter.dll vendor WBDI engine adapter
 -> family/type 0x0c -> AlgoChicago.dll
 -> preprocessing / identify / enroll / matcher
```

EngineAdapter resolves algorithm functions dynamically with `LoadLibraryW` / `GetProcAddress`.

## Solved image layout

```text
packed stream: 64 fast x 80 slow
samples:       5120
packed bytes:  7680
output plane:  80 columns x 64 rows
u16 bytes:     10240
```

Regroup:

```text
dst = (n % 64) * 80 + (n / 64)
```

Persisted gfusb ImageBase is already downstream-regrouped. Do not regroup it again.

## Safety / privacy

Never publish or ask the user to upload:

- plaintext factory PSK or hashes;
- full OTP;
- fingerprint images/raw frames/templates;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- proprietary Goodix DLL/EXE/CAT binaries;
- Windows biometric database material;
- process memory dumps;
- full unit-specific runtime configuration or private hashes.

Never erase/flash firmware or write/re-provision PSK. Preserving Windows Hello is a release requirement.

## Working style

One terminal block at a time. Do not repeat solved steps. Use proof-driven labels (`PROVEN` vs open hypothesis) and preserve decisive checkpoints in this repository.
