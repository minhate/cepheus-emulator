# cepheus-emulator

[![CI](https://github.com/minhate/cepheus-emulator/actions/workflows/ci.yml/badge.svg)](https://github.com/minhate/cepheus-emulator/actions/workflows/ci.yml)
![Target](https://img.shields.io/badge/Target-Xiaomi%20Mi%209%20(cepheus)-blue)
![SoC](https://img.shields.io/badge/SoC-Qualcomm%20SM8150%20(Snapdragon%20855)-green)
![Arch](https://img.shields.io/badge/Arch-AArch64%20(ARM64)-orange)
![Kernel](https://img.shields.io/badge/Kernel-Stock%204.14%20OEM%20%2B%20DTB-purple)
![Verification](https://img.shields.io/badge/Verification-Anti--Mock%20%2F%20Evidence--Backed-red)

A native, high-fidelity system emulator for the **Xiaomi Mi 9 (`cepheus`)** powered by the **Qualcomm Snapdragon 855 (SM8150)** platform.

Unlike typical research scaffolds, compatibility layers, or synthetic containers, `cepheus-emulator` implements a dedicated QEMU machine type (`-machine cepheus`) designed to execute the **unmodified stock OEM kernel (4.14)**, the **stock Device Tree Blob (DTB)**, and **real stock Android userspace images** directly extracted from vendor fastboot firmware.

---

## Architecture & Design Principles

The emulator models hardware at the MMIO, IRQ, and DMA boundaries so that guest drivers interact with realistic virtual hardware without kernel-level binary modifications.

```
+-----------------------------------------------------------------+
|                   Stock Android Userspace                       |
|           (init, property_service, binder, system_server)       |
+-----------------------------------------------------------------+
|               Stock Linux Kernel 4.14 (OEM aarch64)             |
|                 + Unmodified Vendor Device Tree                 |
+-----------------------------------------------------------------+
|                      QEMU: -machine cepheus                     |
|                                                                 |
|  [Native Models]                      [Allowlisted Stubs]       |
|  - 8-core Kyro 485 (1+3+4) Topology   - Boot ROM handoff (ABL)  |
|  - ARM GICv3 + Arch Timers + PSCI     - TrustZone / QSEE SCM    |
|  - Qualcomm GENI Serial (Earlycon)    - Hexagon DSP remoteprocs |
|  - Clocks (GCC / RPMh) & TLMM GPIO    - Adreno 640 SW fallback  |
|  - SPMI & PMIC subsystem              - Camera ISP & Sensors    |
|  - UFS 2.1 Host Controller + SMMU                               |
+-----------------------------------------------------------------+
|                     Host Machine (Linux x86_64)                 |
+-----------------------------------------------------------------+
```

### 1. Native Models vs. Allowlisted Stubs

To ensure engineering rigor, the codebase strictly distinguishes between native hardware models and allowlisted stubs:

| Category | Policy | Description |
|---|---|---|
| **Core Platform** | **Native Only** | CPU topology, GICv3, PSCI, timers, Qualcomm GENI UART, GCC/RPMh clocks, TLMM pinctrl, SPMI/PMIC, UFS 2.1 host controller, and SMMU are modeled natively in QEMU C code. |
| **Android Subsystems** | **Native Only** | Kernel binder, dynamic partitions (`dm-linear` over `super.img`), Android init, SELinux enforcing mode, property service, and `adbd`. |
| **Allowlisted Stubs** | **Strict Allowlist** | Boot ROM handoff (`boot_rom_chain`), TrustZone/QSEE (`trustzone_qsee`), Hexagon DSPs/Modem (`modem_dsp`), Adreno 640 graphics rendering (`gpu_adreno`), Camera ISP (`camera_isp`), and Biometrics/Sensors (`sensors_biometrics_nfc_gnss`). |

Every stub must be documented and registered in [`stubs/REGISTRY.json`](file:///media/husen/HDD/Backup%20Data%20DLL/mi9-kgsl-root-research/mi9-emulator/cepheus-emulator/stubs/REGISTRY.json) and marked with `STUB(<category>)`. Any unregistered stub or unauthorized mock will be rejected by our automated audit tooling.

### 2. Empirical Verification (Anti-Mock Framework)

We enforce a zero-tolerance policy against fake mocks, simulated outputs, and synthetic pass strings:
* **Hash-locked Specifications**: Milestones and their acceptance regex patterns are locked in [`milestones.json`](file:///media/husen/HDD/Backup%20Data%20DLL/mi9-kgsl-root-research/mi9-emulator/cepheus-emulator/milestones.json) and validated by SHA-256.
* **Differential Evidence**: Every milestone requires **both positive evidence** (verifying the behavior in the guest) and **negative evidence** (breaking the feature deliberately to ensure the pattern disappears).
* **Automated Static Scanning**: [`tools/mockscan.py`](file:///media/husen/HDD/Backup%20Data%20DLL/mi9-kgsl-root-research/mi9-emulator/cepheus-emulator/tools/mockscan.py) runs on every CI build to block synthetic status prints, hardcoded command responses, or mock classes.

---

## Roadmap & Milestones

Progress is tracked sequentially through verifiable milestones:

| ID | Title | Scope | Status |
|:---:|---|---|:---:|
| **R0** | Host Toolchain Ready | Toolchain doctor & dependency verification | `IN-PROGRESS` |
| **R1** | Firmware Imported & Hash-locked | Stock ROM extraction & checksum validation | `NOT-STARTED` |
| **R2** | Stock Kernel & DTB Extraction | Decompile DTB and parse kernel version | `NOT-STARTED` |
| **R3** | QEMU Machine `cepheus` Skeleton | Custom machine type with 8 CPUs & SM8150 memory map | `NOT-STARTED` |
| **R4** | Stock Kernel Boots with Native GENI UART | Early console logs on native Qualcomm GENI serial | `NOT-STARTED` |
| **R5** | GICv3, Arch Timer, PSCI, 8-CPU SMP | Multicore bringup under SMP | `NOT-STARTED` |
| **R6** | Native Clock/Reset, TLMM, SPMI/PMIC | Subsystem clock gates and PMIC regulators | `NOT-STARTED` |
| **R7** | Native UFS 2.1 Host Controller | SCSI disk probe & LUN enumeration | `NOT-STARTED` |
| **R8** | Real GPT / LUN Partition Mapping | Partition tables exposed from stock images | `NOT-STARTED` |
| **R9** | Real Android Init (Stage 1 & 2) | Execution of Android init from ramdisk | `NOT-STARTED` |
| **R10** | Dynamic Partitions (dm-linear) | Mount system/vendor/product from `super.img` | `NOT-STARTED` |
| **R11** | SELinux Enforcing Mode | Load stock SELinux policy without permissive hacks | `NOT-STARTED` |
| **R12** | Property Service & Service Managers | Hardware & VND service managers over Binder | `NOT-STARTED` |
| **R13** | Framework Bringup (`system_server`) | Zygote and Android system services startup | `NOT-STARTED` |
| **R14** | `sys.boot_completed` State | Legitimate boot completion flag from init | `NOT-STARTED` |
| **R15** | Stock `adbd` over Virtual Transport | Interactive ADB shell into the guest | `NOT-STARTED` |
| **R16** | MIUI System Apps Launch | Stock application launch validation | `NOT-STARTED` |
| **R17** | VM Snapshots & Performance Profiling | Snapshot/restore, GDB stubs, and benchmarking | `NOT-STARTED` |

For granular acceptance criteria, see [`docs/ROADMAP.md`](file:///media/husen/HDD/Backup%20Data%20DLL/mi9-kgsl-root-research/mi9-emulator/cepheus-emulator/docs/ROADMAP.md).

---

## Getting Started

### Prerequisites

* **Operating System**: Linux (Ubuntu 22.04 LTS / Debian 12 recommended)
* **Architecture**: x86_64 host with hardware virtualization support
* **Core Build Tools**:
  * GCC or Clang (C11/GNU11 support)
  * Python 3.10+
  * GLib 2.0 (`libglib2.0-dev`)
  * Device Tree Compiler (`device-tree-compiler` / `dtc`)
  * Android Host Tools (`android-sdk-libsparse-utils`, `unpackbootimg`, `simg2img`)
  * Meson & Ninja

### Installation & Environment Check

1. **Clone the repository**:
   ```bash
   git clone https://github.com/minhate/cepheus-emulator.git
   cd cepheus-emulator
   ```

2. **Verify host dependencies**:
   Run the environment doctor script to identify missing host tools:
   ```bash
   make doctor
   ```

3. **Verify CI suite and baseline tests**:
   ```bash
   make ci
   ```

4. **Prepare firmware images**:
   Place your official Xiaomi Mi 9 fastboot archive (e.g., `cepheus_global_images_...tgz` or `V12.5.6.0.RFACNXM`) into the input directory:
   ```bash
   cp /path/to/stock_rom.tgz firmware/input/
   ```
   > [!IMPORTANT]
   > Files in `firmware/input/` are immutable. They are checked against checksum locks in [`config/firmware.lock.json`](file:///media/husen/HDD/Backup%20Data%20DLL/mi9-kgsl-root-research/mi9-emulator/cepheus-emulator/config/firmware.lock.json) and are strictly git-ignored.

---

## Makefile Cheatsheet

| Target | Description |
|---|---|
| `make doctor` | Inspects host toolchain, compilers, libraries, and utilities. |
| `make ci` | Runs syntax checks, mock scanning, milestone consistency, and test suite. |
| `make status` | Displays the current status of all project milestones. |
| `make roadmap` | Regenerates `docs/ROADMAP.md` from the canonical spec. |
| `make test` | Runs the Python test suite. |
| `make mockscan` | Scans codebase for forbidden mock patterns and stub anomalies. |
| `make qemu` | Fetches, patches, and compiles the custom QEMU binary (requires network). |

---

## Contributing

We welcome contributions from systems programmers, kernel engineers, and emulator developers.

Please review our [**Contributing Guidelines**](CONTRIBUTING.md) before submitting code.

Key requirements:
1. **Milestone-driven**: All development happens in dedicated milestone branches (`m/<ID>-<slug>`).
2. **Empirical Evidence**: Claims of progress must be accompanied by raw logs captured via `tools/capture.py`.
3. **No Mocks**: PRs introducing synthetic success indicators, hardcoded responses, or unregistered stubs will fail CI immediately.

---

## Repository Layout

```
cepheus-emulator/
├── agent/            # Agent kickoff directives and operational protocols
├── config/           # Pinned source versions and firmware hashes
├── docs/             # Technical specifications, target facts, and proposals
├── evidence/         # Cryptographically signed positive & negative test logs
├── firmware/         # Input archives and extracted partitions (git-ignored)
├── guest/            # Guest-side probe binaries and initramfs sources
├── qemu/             # QEMU patches, machine definition overlay, and build logic
├── scripts/          # Automation scripts (doctor, unpack, build)
├── stubs/            # Allowlisted stub implementations and REGISTRY.json
├── tests/            # Automated test suite for host tooling
├── tools/            # Milestone verifier, mock scanner, and evidence capturer
├── Makefile          # Unified development interface
└── milestones.json   # Canonical specification of project milestones
```

---

## Legal & Security Disclaimer

This project is an independent engineering effort focused on system emulation and reverse engineering for security research and software preservation.
* **No Proprietary Assets**: This repository does not distribute proprietary Qualcomm or Xiaomi bootloaders, kernel binaries, or Android system images. Users must extract and provide images from officially obtained device firmware.
* **License**: Code in this repository is licensed under the **GNU General Public License v2 (GPL-2.0)** to maintain compatibility with QEMU.
