# Learning guide — how this reverse engineering fits together

This is a practical map for developers learning from the project rather than only reproducing one unit.

## 1. Separate layers

Do not treat "fingerprint driver" as one protocol.

The work has distinct layers:

1. USB packet framing;
2. Goodix message-pack framing;
3. Goodix command protocol;
4. TLS record bridge;
5. MCU state/configuration;
6. FDT finger presence state machine;
7. image transport and 12-bit packing;
8. sensor calibration/preprocessing;
9. feature extraction/matching;
10. libfprint/fprintd lifecycle.

Most failed experiments came from mixing two layers together.

## 2. ACK versus asynchronous data

Several Goodix commands first return an ACK and later return an event/data frame. A function can therefore have "command accepted" but "event never arrived" as two different outcomes.

FDT is the clearest example. Always log these separately.

## 3. Transport flags matter

Goodix pack flags include at least normal message protocol and TLS transport classes. The first successful 5135 image response used `0xb0`, not the initially assumed `0xb2`.

Do not validate only lengths; validate flags and the inner protocol too.

## 4. A timeout can be a state bug

A USB timeout does not automatically mean hardware failure. The 5135 `enable_chip` timeout and bad chip read were resolved by reproducing the correct activation-state order.

## 5. Preserve negative results

The v2/v3/v4 FDT failures were valuable because each removed one wrong interpretation. Keep failed hypotheses in docs so a new developer does not "rediscover" them.

## 6. Privacy is part of engineering

Fingerprint frames and templates are not ordinary debug fixtures. Public tests should use lengths, state flags, synthetic data, or locally supplied private fixtures. Logging functions that print command arguments can accidentally leak an OTP/config/biometric buffer.

## 7. Prefer parity before writes

The CFG70 phase succeeded because the Linux builder was required to match a private Windows reference byte-for-byte before command `0x90` was allowed. This is the model for future potentially state-changing work.
