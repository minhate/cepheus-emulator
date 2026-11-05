# qemu/
Our code is kept out-of-tree and applied onto a pinned QEMU checkout (`config/pins.env`).
- `overlay/` mirrors the QEMU tree (e.g. `overlay/hw/arm/cepheus.c`, `overlay/hw/char/qcom_geni_uart.c`). Copied over the checkout by `scripts/apply-overlay.sh`.
- `patches/*.patch` are changes to existing QEMU files (meson.build, Kconfig, MAINTAINERS-free). Applied by the same script (idempotent).
- Build: `make qemu` (needs `CEPHEUS_ALLOW_NETWORK=1` once for the clone). Binary: `build/qemu-build/qemu-system-aarch64`.
