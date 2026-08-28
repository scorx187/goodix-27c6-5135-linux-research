# Chicago enrollment architecture: multi-capture graph

Date: 2026-08-28
Target: AlgoChicago.dll, family/type 0x0c

## Main finding

Enrollment does not fuse every accepted capture into one minutia array. The persistent enrollment context stores multiple fingerprint objects plus a pairwise relation graph.

## Entry path

- enrolAddImageWrapper -> 0x18000cd70.
- 0x18000cd70 validates the preprocessed descriptor, builds a fingerprint object through the shared builder 0x180016920, then calls 0x1800194f0.
- 0x18000cd70 advances UI/progress state only after 0x1800194f0 returns success.

## 0x1800194f0 orchestrator

Observed persistent-context fields:

- +0x24: current accepted capture/object count.
- +0x28: capture capacity.
- +0x30: pointer array of capture fingerprint objects.
- +0x2c: running pair-relation record count/index base.
- +0x1c: copy/feature limit used when cloning a capture.
- +0x87f0: capture index/ranking array rebuilt after each accepted capture.

Early gates:

- special type/quality gate may return 0x80000001.
- count >= capacity returns 0x80000005.
- new_capture +0xf0 == 0 returns 0x80000006.

### First capture

If context+0x24 == 0, 0x1800194f0 calls 0x180019240.

0x180019240:

- selects the next slot pointer from context+0x30[count].
- calls 0x1800197a0 to clone the new fingerprint object into that slot.
- initializes slot metadata.
- increments context+0x24.

### Later captures

For count > 0:

1. next slot = context+0x30[count].
2. 0x1800197a0 deep-copies the newly built fingerprint object into the persistent slot.
3. slot metadata is initialized, including +0x104, +0x114, +0x11c, +0x120, +0x150.
4. context+0x24 increments immediately after the clone.
5. 0x180018c70 compares the new slot against all previous capture slots and appends pair-relation records.
6. if useful overlapping relations exist, 0x180019ad0 performs coverage/topology/feedback processing over the capture graph.
7. context+0x87f0 is rebuilt as 0..count-1 with unused entries set to -1.
8. if count reaches capacity, 0x18005bf40 runs graph finalization followed by 0x180032630.

## 0x1800197a0: persistent capture clone

0x1800197a0 copies the fingerprint object into an existing destination slot. Important observations:

- copies sub-buffers referenced by object +0x08/+0x10/+0x18 when present.
- copies the fixed object region around +0x28.
- clones the minutia array from source +0xf8 to destination +0xf8 using count*0x3c bytes.
- destination minutia count is min(source +0xf0, requested limit).
- copies object metadata including +0x100, +0x104, +0x108, +0x10c, +0x110, +0x114, +0x11c, +0x120, +0x124, +0x128 and selected +0x13c.

This establishes that each enrolled capture remains a standalone fingerprint object.

## 0x180018c70: pairwise relation builder

For the newly appended capture, 0x180018c70 iterates every previous slot.

For each previous/new pair:

- calls 0x18005aa40 to obtain up to 42 correspondence index pairs.
- resolves X/Y coordinates from the two 0x3c minutia arrays.
- requires more than four usable pair correspondences before geometric processing.
- calls 0x180059a20 to fit/evaluate a geometric transform and produce an inlier mask.
- counts surviving/inlier mask entries.
- when enough inliers exist, calls the already-mapped 0x180050c80 spatial/image similarity metric.
- accepts a strong pair relation according to inlier-count plus metric thresholds.

Relation record layout stored at context+0x1c0 + relation_index*0x1c:

- +0x00: accepted relation strength/inlier count, or -1 for rejected/unusable pair.
- +0x04..+0x1b: 24-byte geometric transform/relationship payload.

For accepted relations, the prior capture index is also written to an output list and the accepted-relation count increments.

The context +0x2c relation-record index increments once for each previous capture considered, whether accepted or rejected.

## 0x180019ad0 role

0x180019ad0 runs after the new capture has already been persisted and the pairwise relation graph updated. It groups/indexes captures based on slot +0x100 state and dispatches several coverage/topology helpers. It contributes packed enrollment feedback and graph-derived metrics; it is not the primitive that clones/persists the capture.

## Completion finalizer: 0x18005bf40

0x18005bf40 is called only when context+0x24 reaches context+0x28.

It operates over:

- capture slots at context+0x30,
- pair-relation records at context+0x1c0,
- existing pair transforms/counts,
- temporary graph/path matrices.

Static structure strongly indicates graph closure/finalization rather than minutia fusion:

- retrieves direct relation transforms in either direction.
- traverses capture indices and relation strengths.
- composes transforms through intermediate captures using 0x180043460.
- calls 0x18005d620 when a derived/indirect relation is being materialized or validated.
- marks connected captures via slot+0x100 = 1.

After this graph finalizer, 0x1800194f0 calls 0x180032630; for some sensor families that wrapper dispatches additional type-specific final processing.

## Current next target

0x18005d620 is the most useful next function because it sits exactly where 0x18005bf40 converts an indirect graph path plus composed transform into a persistent/validated relation. Close its ABI and writes before going deeper into unrelated coverage helpers.

## Safety

Static analysis only. No fingerprint image, enrolled template, PSK, OTP, calibration payload, goodix.dat, Windows biometric database, or unit-specific private biometric data was read, published, or committed.
