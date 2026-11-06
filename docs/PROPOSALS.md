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


## 2026-11-06 R5 Fix pattern: GICv3 redistributor pattern instead of legacy GICv2 SPIs implemented

**Observed (raw guest lines)**:
- In real guest boot output on `cepheus`:
  ```text
  GICv3: GIC: Using split EOI/Deactivate mode
  GICv3: no VLPI support, no direct LPI support
  GICv3: CPU0: found redistributor 0 region 0:0x0000000017a60000
  GICv3: CPU1: found redistributor 100 region 0:0x0000000017a80000
  ...
  GICv3: CPU7: found redistributor 700 region 0:0x0000000017b40000
  SMP: Total of 8 processors activated.
  ```
- Grep across the entire stock kernel Image (`build/stock/Image`) confirms the string `SPIs implemented` does not exist anywhere in the binary. That string belonged to legacy GICv2 (`drivers/irqchip/irq-gic.c`), whereas GICv3 in Linux kernel (`drivers/irqchip/irq-gic-v3.c`) prints `GICv3: CPU%d: found redistributor %lx region %d:%pa`.

**Proposal**:
In `milestones.json`:
In `R5.patterns`:
Change:
`"GICv3: [0-9]+ SPIs implemented"`
To:
`"GICv3: CPU0: found redistributor"`

**Why**:
The stock kernel Image 4.14 uses ARM GICv3 where distributor and per-CPU redistributor discovery is logged via `GICv3: CPU%d: found redistributor...`. The pattern `"GICv3: [0-9]+ SPIs implemented"` cannot match real stock kernel output because that string is absent from the stock Linux 4.14 GICv3 driver.
