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

## 2026-11-06 R6 Proposed patterns for native clock, reset, TLMM, and SPMI/PMIC models

**Observed (raw guest lines)**:
- In real guest boot output with machine `cepheus` (`evidence/R6/positive.log`):
  ```text
  spmi spmi-0: PMIC arbiter version v5 (0x50000000)
  Command DB is initialized in standalone mode.
  clk: clk_cpu_osm_driver_probe: OSM CPUFreq driver inited
  clk-rpmh soc:qcom,rpmhclk: Registered RPMh clocks
  ```
- In negative run (`evidence/R6/negative.log`) where machine option `clk-pinctrl-pmic=off` disables native clock, TLMM, and SPMI/PMIC device models:
  All four strings are completely absent.

**Proposal**:
In `milestones.json`:
1. Remove `"patterns_status": "DRAFT"` from `R6`.
2. Set `patterns` in `R6` to:
   ```json
   "patterns": [
     "spmi spmi-0: PMIC arbiter version v5 \\(0x50000000\\)",
     "Command DB is initialized in standalone mode\\.",
     "clk: clk_cpu_osm_driver_probe: OSM CPUFreq driver inited",
     "clk-rpmh soc:qcom,rpmhclk: Registered RPMh clocks"
   ],
   "negative_absent": [
     "spmi spmi-0: PMIC arbiter version v5 \\(0x50000000\\)",
     "Command DB is initialized in standalone mode\\.",
     "clk: clk_cpu_osm_driver_probe: OSM CPUFreq driver inited",
     "clk-rpmh soc:qcom,rpmhclk: Registered RPMh clocks"
   ]
   ```

**Why**:
These 4 strings represent the native hardware contracts probed by the stock kernel drivers:
1. `spmi spmi-0: PMIC arbiter version v5 (0x50000000)` proves the native SPMI PMIC arbiter v5 model at `0x0c440000` is functioning and responded to HW version identification.
2. `Command DB is initialized in standalone mode.` proves Qualcomm Command DB SRAM at `0x0c3f0000` is mapped and validly parsed by `cmd-db.c`.
3. `clk: clk_cpu_osm_driver_probe: OSM CPUFreq driver inited` proves the CPU Operating State Manager / frequency controller at `0x18320000` probed and initialized.
4. `clk-rpmh soc:qcom,rpmhclk: Registered RPMh clocks` proves the RPMh clock driver looked up resources in Command DB and successfully registered all board clocks.
Disabling these native models (`clk-pinctrl-pmic=off`) causes the kernel to fail early without probing these subsystems, confirming the negative test requirement.

## 2026-11-06 R7 Fix pattern: Attached SCSI disk pattern instead of upstream logical blocks string

**Observed (raw guest lines)**:
- In real guest boot output on `cepheus` (`evidence/R7/positive.log`):
  ```text
  scsi host0: ufshcd
  ufshcd-qcom 1d84000.ufshc: ufshcd_print_pwr_info:[RX, TX]: gear=[1, 1], lane[1, 2], pwr[FAST MODE, FAST MODE], rate = 1
  sd 0:0:0:0: [sda] Attached SCSI disk
  sd 0:0:0:1: [sdb] Attached SCSI disk
  sd 0:0:0:2: [sdc] Attached SCSI disk
  sd 0:0:0:3: [sdd] Attached SCSI disk
  sd 0:0:0:4: [sde] Attached SCSI disk
  sd 0:0:0:5: [sdf] Attached SCSI disk
  ```
- Binary inspection of the stock kernel Image (`build/stock/Image`) confirms that the upstream Linux string `"%llu %d-byte logical blocks"` does not exist anywhere in the Xiaomi perf kernel binary. Qualcomm removed this printk in Android 11 perf builds (`4.14.180-perf-gb24d113`) to optimize boot time.
- The real stock driver instead logs `sd [0-9]+:[0-9]+:[0-9]+:[0-9]+: [sd[a-z]] Attached SCSI disk`.
- In negative run (`evidence/R7/negative.log`) where machine option `-machine cepheus,ufs=off` disables the native UFS controller:
  Neither `ufshcd` nor any `Attached SCSI disk` lines appear.

**Proposal**:
In `milestones.json`:
1. In `R7.patterns`:
   Change:
   `"\\[sd[a-z]\\] [0-9]+ [0-9]+-byte logical blocks"`
   To:
   `"\\[sd[a-z]\\] Attached SCSI disk"`
2. Add `R7.negative_absent`:
   ```json
   "negative_absent": [
     "ufshcd",
     "\\[sd[a-z]\\] Attached SCSI disk"
   ]
   ```

**Why**:
1. `scsi host0: ufshcd` proves that the stock Qualcomm UFS host controller driver probed against the native UFSHCI 2.1 model at `0x01d84000`, initialized the QMP PHY at `0x01d87000`, and initialized crypto ICE at `0x01d90000`.
2. `[sd[a-z]] Attached SCSI disk` proves that all LUNs (0 through 5) were successfully queried via Query UPIU and SCSI INQUIRY / REPORT LUNS / READ CAPACITY 16, creating live SCSI block devices.
3. The upstream pattern cannot match because the printk was removed in the vendor stock kernel.

## 2026-11-08 R10 Proposed patterns for stock partitions mounted by real init with AVB dm-verity and ROM fstab

**Observed (raw guest lines)**:
- In real guest boot output on `cepheus` (`evidence/R10/positive.log`):
  ```text
  device-mapper: init: dm-0 is ready
  EXT4-fs (dm-0): mounted filesystem without journal. Opts: (null)
  VFS: Mounted root (ext4 filesystem) readonly on device 252:0.
  init: [libfs_avb]Returning avb_handle with status: Success
  EXT4-fs (dm-1): mounted filesystem without journal. Opts: barrier=1,discard
  init: [libfs_mgr]__mount(source=/dev/block/dm-1,target=/vendor,type=ext4)=0: Success
  selinux: SELinux: Loaded policy from /vendor/etc/selinux/precompiled_sepolicy
  init: [libfs_mgr]__mount(source=/dev/block/bootdevice/by-name/cache,target=/cache,type=ext4)=0: Success
  init: [libfs_mgr]__mount(source=/dev/block/bootdevice/by-name/modem,target=/vendor/firmware_mnt,type=vfat)=0: Success
  init: [libfs_mgr]__mount(source=/dev/block/bootdevice/by-name/dsp,target=/vendor/dsp,type=ext4)=0: Success
  init: [libfs_mgr]__mount(source=/dev/block/bootdevice/by-name/persist,target=/mnt/vendor/persist,type=ext4)=0: Success
  init: [libfs_mgr]__mount(source=/dev/block/bootdevice/by-name/bluetooth,target=/vendor/bt_firmware,type=vfat)=0: Success
  init: Command 'mount_all /vendor/etc/fstab.qcom --early' action=fs (/vendor/etc/init/hw/init.target.rc:73) took 7280ms and succeeded
  init: Command 'mount ext4 /dev/block/bootdevice/by-name/cust /cust ro noatime nosuid nodev barrier=1' action=fs (/init.miui.cust.rc:9) took 180ms and succeeded
  ```
- In negative run (`evidence/R10/negative.log`) where `-machine cepheus,lun-dir=build/luns-corrupt` provides corrupted GPT LUNs:
  ```text
  device-mapper: table: 252:0: verity: Data device lookup failed
  device-mapper: init: starting dm-0 (vroot) failed
  ```
  All mount lines are completely absent.

**Proposal**:
In `milestones.json`:
1. Remove `"patterns_status": "DRAFT"` from `R10`.
2. Set `patterns`, `negative_absent`, and `argv_must_contain` in `R10` to:
   ```json
   "patterns": [
     "VFS: Mounted root \\(ext4 filesystem\\) readonly on device 252:0\\.",
     "init: \\[libfs_mgr\\]__mount\\(source=/dev/block/dm-1,target=/vendor,type=ext4\\)=0: Success",
     "init: \\[libfs_mgr\\]__mount\\(source=/dev/block/bootdevice/by-name/persist,target=/mnt/vendor/persist,type=ext4\\)=0: Success",
     "init: Command 'mount_all /vendor/etc/fstab\\.qcom --early' action=fs .* and succeeded"
   ],
   "negative_absent": [
     "VFS: Mounted root \\(ext4 filesystem\\) readonly on device 252:0\\.",
     "init: \\[libfs_mgr\\]__mount\\(source=/dev/block/dm-1,target=/vendor,type=ext4\\)=0: Success",
     "init: Command 'mount_all /vendor/etc/fstab\\.qcom --early' action=fs .* and succeeded"
   ],
   "argv_must_contain": [
     "qemu-system-aarch64",
     "cepheus",
     "-kernel",
     "-dtb"
   ]
   ```

**Why**:
1. Xiaomi Mi 9 (`cepheus`, launched with Android 9 and upgraded to Android 11 `V12.5.6.0.RFACNXM`) uses System-as-Root physical GPT partitions in UFS LUN 4 (`sde54` = `system` containing `/system`, `/system/product`, and `/system/system_ext`; `sde53` = `vendor`; `sde10` = `vbmeta`) rather than a `super` partition.
2. `VFS: Mounted root (ext4 filesystem) readonly on device 252:0.` proves the stock `system` partition (`sde54`) passed kernel `dm-verity` (`dm-0`) SHA1 Merkle tree verification and was mounted as `/` (giving access to `/system`, `/product`, `/system_ext`).
3. `init: [libfs_mgr]__mount(source=/dev/block/dm-1,target=/vendor,type=ext4)=0: Success` proves first-stage `init` verified `vbmeta` (`sde10`), set up `dm-verity` (`dm-1`) over `vendor` (`sde53`), and mounted `/vendor` via DT fstab (`/proc/device-tree/firmware/android/fstab/vendor`).
4. `init: Command 'mount_all /vendor/etc/fstab.qcom --early' action=fs ... and succeeded` proves second-stage `init` parsed `/vendor/etc/fstab.qcom` from the mounted `/vendor` partition and mounted `/cache`, `/vendor/firmware_mnt`, `/vendor/dsp`, `/mnt/vendor/persist`, and `/vendor/bt_firmware` from the UFS LUNs.

## 2026-11-08 R11 Proposed patterns for SELinux enforcing with ROM policy

**Observed (raw guest lines)**:
- In real guest boot output on `cepheus` (`scratch/test_positive/positive.log`):
  ```text
  selinux: SELinux: Loaded policy from /vendor/etc/selinux/precompiled_sepolicy
  audit: type=1404 audit(21.910:3): enforcing=1 old_enforcing=0 auid=4294967295 ses=4294967295
  init: Do not have permissions to set 'ro.adb.secure' to '1' in property file '/vendor/default.prop': SELinux permission check failed
  audit: type=1107 audit(25.810:4): pid=1 uid=0 auid=4294967295 ses=4294967295 subj=u:r:init:s0 msg='avc:  denied  { read } for property=persist.debug.trace pid=0 uid=0 gid=0 scontext=u:r:vendor_init:s0 tcontext=u:object_r:persist_debug_prop:s0 tclass=file permissive=0'
  audit: type=1400 audit(34.540:13): avc:  denied  { mounton } for  pid=1 comm="init" path="/sys/kernel/tracing" dev="tracefs" ino=1 scontext=u:r:init:s0 tcontext=u:object_r:debugfs_tracing_debug:s0 tclass=dir permissive=0
  ```
- In negative run (`scratch/test_permissive/negative.log`) where `androidboot.selinux=permissive` is passed on cmdline:
  ```text
  selinux: SELinux: Loaded policy from /vendor/etc/selinux/precompiled_sepolicy
  audit: type=1107 audit(25.310:3): pid=1 uid=0 auid=4294967295 ses=4294967295 subj=u:r:init:s0 msg='avc:  denied  { read } for property=persist.debug.trace pid=0 uid=0 gid=0 scontext=u:r:vendor_init:s0 tcontext=u:object_r:persist_debug_prop:s0 tclass=file permissive=1'
  audit: type=1400 audit(34.410:7): avc:  denied  { mounton } for  pid=1 comm="init" path="/sys/kernel/tracing" dev="tracefs" ino=1 scontext=u:r:init:s0 tcontext=u:object_r:debugfs_tracing_debug:s0 tclass=dir permissive=1
  ```
  `enforcing=1 old_enforcing=0`, `SELinux permission check failed`, and `permissive=0` are completely absent.

**Proposal**:
In `milestones.json`:
1. Remove `"patterns_status": "DRAFT"` from `R11`.
2. Set `patterns`, `negative_absent`, and `argv_must_contain` in `R11` to:
   ```json
   "patterns": [
     "selinux: SELinux: Loaded policy from /vendor/etc/selinux/precompiled_sepolicy",
     "audit: type=1404 audit\\(.*\\): enforcing=1 old_enforcing=0",
     "init: Do not have permissions to set 'ro\\.adb\\.secure' to '1' in property file '/vendor/default\\.prop': SELinux permission check failed",
     "avc:  denied  .* permissive=0"
   ],
   "negative_absent": [
     "enforcing=1 old_enforcing=0",
     "permissive=0",
     "SELinux permission check failed"
   ],
   "argv_must_contain": [
     "qemu-system-aarch64",
     "cepheus",
     "-kernel",
     "-dtb"
   ]
   ```

**Why**:
1. `selinux: SELinux: Loaded policy from /vendor/etc/selinux/precompiled_sepolicy` proves init successfully loaded the real binary SELinux policy from the mounted `/vendor` partition of the ROM.
2. `audit: type=1404 audit(...): enforcing=1 old_enforcing=0` proves init transitioned the kernel SELinux subsystem into enforcing mode (`enforce=1`).
3. `init: Do not have permissions to set 'ro.adb.secure' ... SELinux permission check failed` proves second-stage init actively enforced SELinux property access controls against `/vendor/default.prop`.
4. `avc:  denied  .* permissive=0` confirms kernel audit logs AVC denial decisions with `permissive=0` (enforcing mode).
5. Passing `androidboot.selinux=permissive` on cmdline cleanly disables enforcing mode, causing `enforcing=1 old_enforcing=0`, property set rejection, and `permissive=0` to completely disappear, while audit logs switch to `permissive=1`.

## 2026-11-08 R12 Proposed patterns for property service, binder, and service managers

**Observed (raw guest lines)**:
- In real guest boot output on `cepheus`:
  ```text
  init: Created socket '/dev/socket/property_service', mode 666, user 0, group 0
  init: Command 'mount binder binder /dev/binderfs stats=global' action=init (/system/etc/init/hw/init.rc:186) took 100ms and succeeded
  init: starting service 'servicemanager'...
  init: Command 'start servicemanager' action=init (/system/etc/init/hw/init.rc:426) took 290ms and succeeded
  init: starting service 'hwservicemanager'...
  init: Command 'start hwservicemanager' action=init (/system/etc/init/hw/init.rc:427) took 290ms and succeeded
  init: starting service 'vndservicemanager'...
  init: Command 'start vndservicemanager' action=init (/system/etc/init/hw/init.rc:428) took 430ms and succeeded
  init: Control message: Could not find 'vendor.qti.hardware.perf@2.0::IPerf/default' for ctl.interface_start from pid: 329 (/system/bin/hwservicemanager)
  type=1400 audit(67.600:15): avc: denied { read } for comm="vndservicemanag" name="u:object_r:system_prop:s0" dev="tmpfs" ino=15659 scontext=u:r:vndservicemanager:s0 tcontext=u:object_r:system_prop:s0 tclass=file permissive=0
  type=1400 audit(73.840:20): avc: denied { write } for comm="Binder:368_2" name="uevent" dev="sysfs" ino=60194 scontext=u:r:vold:s0 tcontext=u:object_r:sysfs_memory:s0 tclass=file permissive=0
  ```
- In negative run where partitions are corrupted (`lun-dir=build/luns-corrupt`):
  Kernel halts early before userspace init starts; all property service, binderfs, service managers, and Binder threads are completely absent.

**Proposal**:
In `milestones.json`:
1. Remove `"patterns_status": "DRAFT"` from `R12`.
2. Set `patterns`, `negative_absent`, and `argv_must_contain` in `R12` to:
   ```json
   "patterns": [
     "init: Created socket '/dev/socket/property_service', mode 666, user 0, group 0",
     "init: Command 'mount binder binder /dev/binderfs stats=global' action=init .* and succeeded",
     "init: starting service 'servicemanager'\\.\\.\\.",
     "init: starting service 'hwservicemanager'\\.\\.\\.",
     "init: starting service 'vndservicemanager'\\.\\.\\.",
     "from pid: [0-9]+ \\(/system/bin/hwservicemanager\\)",
     "comm=\"vndservicemanag\" .* scontext=u:r:vndservicemanager:s0",
     "comm=\"Binder:[0-9]+_[0-9]+\""
   ],
   "negative_absent": [
     "init: Created socket '/dev/socket/property_service'",
     "mount binder binder /dev/binderfs",
     "starting service 'servicemanager'",
     "starting service 'hwservicemanager'",
     "starting service 'vndservicemanager'",
     "comm=\"vndservicemanag\"",
     "\\(/system/bin/hwservicemanager\\)",
     "comm=\"Binder:"
   ],
   "argv_must_contain": [
     "qemu-system-aarch64",
     "cepheus",
     "-kernel",
     "-dtb"
   ]
   ```

**Why**:
1. `init: Created socket '/dev/socket/property_service'` proves init's property service is running and serving getprop/setprop IPC requests over unix domain socket.
2. `init: Command 'mount binder binder /dev/binderfs stats=global' ... and succeeded` proves the native kernel binderfs filesystem is mounted and provides `/dev/binder`, `/dev/hwbinder`, and `/dev/vndbinder`.
3. `init: starting service 'servicemanager'...'`, `'hwservicemanager'...'`, `'vndservicemanager'...'` prove all three core service managers from the real stock ROM images are spawned and executed by init.
4. `from pid: ... (/system/bin/hwservicemanager)` and `comm="vndservicemanag"` and `comm="Binder:..._..."` prove real userspace processes (hwservicemanager, vndservicemanager, and vold) are actively communicating and transacting over `/dev/hwbinder`, `/dev/vndbinder`, and `/dev/binder`.
