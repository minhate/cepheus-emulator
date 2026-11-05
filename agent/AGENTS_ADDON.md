<!-- ADDON-SECTION:BEGIN -->

## 13. Current track, standing decisions, add-on milestones (added by the user; does not weaken anything above)

### 13.1 Order of work
- R0 is VERIFIED. Run `python3 tools/milestone.py list` and take the **first milestone in that order whose `depends` are all VERIFIED**. One at a time (§6). Never batch, never skip ahead.
- Add-on milestones **G1, D1, D2, D3, F1, M1, N1, A1** are real milestones: same protocol, same §5 rules, same evidence. They are not optional extras.
- Exception: G1 (GDB) may be taken any time after R3 is VERIFIED when bring-up needs a debugger.
- If your usage limit is nearly out, save a handoff with `python3 tools/milestone.py set <ID> IN-PROGRESS --note "<5 lines: done, next command, blocker>"` and stop. Do not leave work only in chat.

### 13.2 Stock image map (confirm each line at R1 from the REAL archive; never assume)
- `boot.img`: kernel Image, DTB(s), ramdisk, cmdline (R2).
- `super.img` (sparse, dynamic partitions): system, vendor, product, system_ext, odm as present (R8, R10). Unpack only with real tools (simg2img, lpunpack). Never hand-build a filesystem or list of packages.
- `vbmeta*.img`, `dtbo.img`: AVB state and DTB overlays, handled per the real AVB state, never faked.
- **GMS and GSF are system apps inside `super.img`** (confirmed by the owner on a phone with the same ROM: `pm list packages -s` lists GMS; GSF is under `system_ext/priv-app`). Do not inject, fake or "install" GMS to make something pass; the guest gets it from the real image. Copies under `/data` on the owner's phone are user state, not ROM.
- The pinned build is China Stable `V12.5.6.0.RFACNXM`. Changing the pin needs the owner's approval in `docs/PROPOSALS.md`.

### 13.3 Standing decisions (owner-made; do not reopen, do not work around)
1. **Display (D1, D2):** native only. There is no display stub category. If a native display-controller model is truly impossible, `BLOCKED(reason)` with what you tried. Do not add `simple-framebuffer` or any DTB node (DTB edits are only for disabling allowlisted nodes, §3).
2. **Host GPU:** it may only present/scale the window (`scripts/build-qemu-gfx.sh`, `scripts/run-display.sh`). Guest rendering is the `gpu_adreno` software fallback. No virtio-gpu/virgl/venus, no non-stock kernel, no Mesa swap: those break §1 and §3.
3. **Network (N1):** write a proposal in `docs/PROPOSALS.md` first (stock DTB has no virtio-net; Wi-Fi hardware is not on the allowlist). Do not start N1 without the owner's decision.
4. **Root (M1):** Magisk and any APK are supplied by the owner under `firmware/input/` (or a path the owner names). Record sha256. Never download, never commit. A patched boot image must be reflected truthfully in what the `boot_rom_chain` stub hands to the kernel (no forced green/locked values, §5.7).
5. **Boot from flash (F1):** the launcher boots from the kernel/ramdisk extracted back out of `boot_a` in the real GPT/LUN image with real tools, not from a side file.

### 13.4 Evidence rules for display/input/app milestones
- Display: `python3 tools/capture.py ... -- python3 tools/qemu_screens.py --out <dir> ... -- <qemu cmd with -machine cepheus>`. The evidence is the pixel-derived `screen ...` / `frame-diff ...` lines plus guest-side output. Negative = display model disabled.
- Touch: guest `getevent` lines from the real kernel input stack, injected via the QEMU monitor (`--between`).
- Apps (A1): real `adb install`, `pm path`, `am start`, `dumpsys`. No hardcoded package lists.
- Debug (G1): `scripts/gdb-attach.sh` batch output from the real vCPU; negative = QEMU started without `-s`.

### 13.5 Read-only for you (in addition to §6)
`tools/screencheck.py`, `tools/qemu_screens.py`, `tools/merge_milestones.py`, `tools/apply_addon.py`, `milestones.addon.json`, `agent/AGENTS_ADDON.md`. Need a change: `docs/PROPOSALS.md`, then stop.

### 13.6 DRAFT patterns
For a DRAFT milestone, write the proposed patterns in `docs/PROPOSALS.md` copied from **real observed output**, then stop and wait for the owner to finalize and relock. Never guess a pattern.

<!-- ADDON-SECTION:END -->
