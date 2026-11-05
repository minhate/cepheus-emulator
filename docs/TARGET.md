# Target facts (starting hypotheses — the stock DTB and stock kernel are the authority)

These are from memory and must be **confirmed from the extracted stock DTB** (R2). If this file and the DTB disagree, the DTB wins; fix this file.

- Device: Xiaomi Mi 9, codename `cepheus`. SoC: Qualcomm SM8150 (Snapdragon 855).
- CPU: 8 cores in 3 clusters (1 prime + 3 gold, Cortex-A76 class; 4 silver, Cortex-A55 class). QEMU CPU types per core are chosen from `qemu-system-aarch64 -cpu help` of the pinned QEMU; a machine can instantiate different CPU types per core. Document any deviation.
- Interrupt controller: GICv3 (qcom topology from DTB), arch timer, PSCI.
- Console: Qualcomm GENI serial (earlycon `msm_geni_serial` at `0xa90000` per previous cmdline; confirm in DTB).
- Storage: UFS 2.1 host controller, multiple LUNs, GPT per LUN. Needs a native UFS HC model + SMMU as required by the stock kernel.
- Clocks/regulators/pins: GCC/RPMh clocks, TLMM pinctrl, SPMI PMIC. Model only what the stock kernel actually requests, natively.
- Stock kernel: 4.14 (OEM, arm64). Stock ROM pin: see `config/firmware.lock.json` (default `V12.5.6.0.RFACNXM`, Android 11 / MIUI 12.5, user-supplied).

## Boot path
1. QEMU loads stock kernel `Image`, stock DTB, stock ramdisk, cmdline from the stock `boot.img` header (plus required `androidboot.*`). This is the `boot_rom_chain` stub: reproduce the state real ABL hands to the kernel (DTB pointer, EL, memory layout) — derive it from the DTB and kernel docs, not from guesses.
2. Kernel probes natively modeled devices. Probes for allowlisted hardware may fail or be disabled via registered DTB edits; every failing probe is logged and classified (allowlisted vs missing native model).
3. Real Android init runs from the stock ramdisk and real `super` partitions.

## Guest tooling
Guest probe initramfs/binaries are cross-built (`aarch64-linux-gnu-gcc`) from sources in `guest/`. Probes only run commands and print raw output.
