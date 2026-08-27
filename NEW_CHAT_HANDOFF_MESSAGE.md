# Copy/paste this into the next ChatGPT conversation

افتح مشروع GitHub العام `scorx187/goodix-27c6-5135-linux-research` وابدأ من `AI_START_HERE.md` فقط، ثم اقرأ `docs/CURRENT_STATUS_2026-08-27.md` و`docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md` و`docs/FDT_5135_PROOF_2026-08-27.md` و`docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md` و`docs/FAILURES_AND_RECOVERIES_2026-08-27.md` و`docs/SAFETY.md` بالكامل قبل أي تجربة.

المشروع هدفه تشغيل حساس البصمة Goodix USB `27c6:5135` على Linux/libfprint بدون تخريب Windows Hello أو تغيير firmware أو إعادة provisioning للـPSK.

الجهاز المثبت:

```text
VID:PID   27c6:5135
Firmware  GF_HC460SEC_APP_12508
Chip raw  a2042500
Chip ID   0x2504
Profile   ChicagoHS / ChicagoHU
Type      12
Geometry  80x64 = 5120 pixels
USB IN    0x81 bulk
USB OUT   0x01 bulk
```

لا تعيد حل المراحل المنتهية. الحالة الحالية:

```text
USB transport                         ✅
CFG70 exact reconstruction            ✅
command 0x90 upload                   ✅
factory TLS 1.2 PSK                   ✅
encrypted NOP                         ✅
verified 5135 activation sequence     ✅
FDT manual 0x36                       ✅
FDT-down 0x32 ACK/event               ✅
FDT-up 0x34 ACK/event                 ✅
image 0x20 ACK                        ✅
TLS image transport                   ✅
TLS image decrypt                     ✅
Goodix image framing                  ✅
image CRC domain                      ⏳ NEXT
12-bit decode to 80x64                ⏳ NEXT
matcher/libfprint                     ⏳ LATER
```

مهم جدًا: الـfactory PSK تم استرجاعه محليًا من بيانات Windows الخاصة بصاحب الجهاز وهو شغال. لا تطلبه ولا تطبعه ولا ترفعه ولا تعيد كتابته للحساس. TLS المثبت:

```text
TLS 1.2
PSK-AES128-GCM-SHA256
identity Client_identity
```

الـCFG70 blocker انتهى: Windows command `0x90` طوله بالضبط 224 bytes، Linux يعيد بناء CFG70 من live OTP ويطابق private Windows runtime reference byte-for-byte، والـchecksum مثبت، وتم upload واحد ونجح. لا تنشر full OTP أو full 224-byte runtime config أو unit-specific config hash.

سلسلة activation الصحيحة للحساس مهمة جدًا. إذا رجع register0 = `06000000` أو `enable_chip(True)` timeout، لا تعتبر الحساس خربان. السلسلة المثبتة:

```text
NOP
0xd4 TLS_SUCCESSFULLY_ESTABLISHED (transient activation state)
NOP
0x96 ENABLE_CHIP true
NOP
firmware_version
0xa2 reset(True, False, 20)
read register 0x0000
```

بعدها رجع `a2042500` ثلاث مرات متتالية.

FDT مثبت كاملًا. `goodix.dat` الخاص بالجهاز لا يُرفع، لكن layout المثبت:

```text
OTP64 + FDT12 + NAV3200 + IMAGE10240 + CRC4 = 13520
```

FDT12 هو six duplicated manual threshold bytes. Manual mode:

```text
0x36 payload = 0d01 + private seed12
reply = irq:u16le + touchflag:u16le + six raw u16le zone values
```

FDT-down thresholds:

```text
threshold = floor(raw_zone / 2)
encoded = 80 xx
0x32 payload = 0801 + regs12 + timestampLE
```

FDT-up:

```text
0x34 payload = 0a02 + regs12
```

نجح Linux down/up. الـup الحالي استخدم private per-unit thresholds مأخوذة من Windows trace؛ generic up derivation غير مثبت بعد.

أهم تقدم حالي: أول image transport نجح. التسلسل:

```text
finger held after FDT-down
send 0x20 payload 01 00
normal ACK PASS
second Goodix frame flags = 0xb0
second frame length = 7722
TLS record offset = 0
OpenSSL decrypt = 7693 plaintext bytes
```

ثم فحصنا metadata فقط بدون طباعة أي biometric bytes:

```text
total plaintext       7693
command               0x20
declared protocol len 7690
trailer               0x88
checksum=True         FAIL
checksum=False        PASS
protocol payload      7689
```

إذن image message uses no-checksum Goodix protocol path with trailer `0x88`.

من upstream `goodix-fp-dump/driver_51x0.py` يوجد:

```python
tool.decode_image(tls_server.stdout.read(...)[8:-5])
```

وعندنا:

```text
7693 - 8 - 5 = 7680
7680 * 8 / 12 = 5120 pixels = 80x64
```

أقوى structure حالي:

```text
3-byte Goodix protocol header
5-byte image metadata
7680 packed 12-bit pixels
4-byte image CRC
1-byte protocol trailer 0x88
= 7693
```

المهمة الحالية مباشرة: لا تطلب مني إعادة capture ولا رفع ملف البصمة. عندي private capture محلي غالبًا تحت root لأن probe اشتغل بـsudo. استخدم `scripts/inspect_private_image_capture_5135.py` من repo أو جهز نسخة محسنة منه. المطلوب:

1. افحص CRC32/MPEG candidate domains والendianness محليًا بدون طباعة bytes.
2. حدد exact CRC domain.
3. فك packed 7680 باستخدام upstream `tool.decode_image` 6-byte -> 4x12-bit algorithm.
4. تأكد output = 5120 pixels وكل pixel 0..4095.
5. احفظ PGM محلي فقط mode 0600، ولا تطلب مني أرفعه أو ألصق محتواه.
6. بعد نجاح الصورة ننتقل لـimage metadata / calibration / preprocessing ثم matcher ثم libfprint.

قواعد الخصوصية الإلزامية:

- لا تطلب أو تنشر plaintext PSK.
- لا تنشر PSK files/hashes.
- لا تنشر full OTP.
- لا تنشر fingerprint images/raw/templates.
- لا تنشر `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`.
- لا تنشر proprietary Goodix DLL/EXE/CAT.
- لا تطلب full memory dump.
- لا تنشر full per-device 224-byte runtime config أو unit-specific config hash.
- لا firmware erase/flash.
- لا PSK write/provision.
- لا تشغل destructive 5117 scripts.

ملاحظة: `device.disconnect()` كثير يعطي timeout بعد نجاح العملية؛ لا تعتبره فشل للعملية السابقة. sysfs authorized 0/1 toggle طريقة مثبتة لاستعادة USB transport.

أسلوب العمل: عطيني بلوك terminal واحد كل مرة وأنا ألصق الناتج. لا تكرر أسئلة معلومة. بعد أي milestone مهم حدّث GitHub مباشرة حتى ما يضيع تقدمنا مع حد المحادثة.

إذا كان GitHub connector شغال في المحادثة الجديدة، افحص المستودع الحالي أولًا ولا تستبدل الملفات بشكل أعمى. إذا أعطيتك checkpoint ZIP أو git bundle من المحادثة السابقة، قارنه مع `main`، ادمج التغييرات الآمنة، ثم commit/push وأعطني SHA النهائي قبل متابعة التجارب.
