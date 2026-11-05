# Proposals (agent writes here; only the user decides)

Use for: wrong/weak milestone patterns, DRAFT pattern proposals, scope questions. One entry per item:

```
## <date> <milestone> <title>
Observed (raw guest/host lines): ...
Proposal: ...
Why: ...
```

## 2026-11-06 R1 Fix pattern: replace super.img with system.img for Xiaomi Mi 9 (cepheus)

**Observed (raw host lines)**:
- In ROM partition table (`rawprogram4.xml`):
  `<program filename="vendor.img" label="vendor" physical_partition_number="4" .../>`
  `<program filename="system.img" label="system" physical_partition_number="4" .../>`
- In fastboot flashing script (`flash_all.sh`):
  `fastboot flash vendor images/vendor.img`
  `fastboot flash system images/system.img`
- No `super.img` exists in stock fastboot ROM `cepheus_images_V12.5.6.0.RFACNXM`.

**Proposal**:
In `milestones.json`:
1. In `R1.acceptance`:
   Change:
   `"Importer copies the user's stock archive contents into firmware/extracted using real tools, prints real sha256 of archive and key images (boot, super, vbmeta, dtbo) and compares them to config/firmware.lock.json. Negative: tampered copy compares unequal."`
   To:
   `"Importer copies the user's stock archive contents into firmware/extracted using real tools, prints real sha256 of archive and key images (boot, system, vendor, vbmeta, dtbo) and compares them to config/firmware.lock.json. Negative: tampered copy compares unequal."`

2. In `R1.patterns`:
   Change:
   `"^super\\.img sha256 [0-9a-f]{64} "`
   To:
   `"^system\\.img sha256 [0-9a-f]{64} "`

3. In `R10.acceptance` (for later):
   Change dynamic partition assumption to direct physical GPT partition mounting from UFS LUN 4.

**Why**:
Xiaomi Mi 9 (`cepheus`, Snapdragon 855) launched natively with Android 9 (non-dynamic partitions). In MIUI updates up to Android 11 (`V12.5.6.0.RFACNXM`), it retained discrete GPT partitions (`system.img`, `vendor.img`, `cust.img`) on UFS LUN 4 instead of retrofitted dynamic partitions (`super.img`). Faking a `super.img` would violate AGENTS.md §2 and §5 ("no mocks", "hardware is modeled natively").
