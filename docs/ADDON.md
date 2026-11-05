# Add-on: display, touch, root, apps, debug

Apply (USER ONLY, agents must not touch `milestones.json`):
```
cp -r addon/* .                          # new files only; nothing is overwritten
python3 tools/apply_addon.py             # dry run: shows what will change
python3 tools/apply_addon.py --apply     # AGENTS.md section 13 + KICKOFF.md + milestones + relock + ROADMAP.md
make ci && git status                    # review, then commit
```
Re-running is safe (idempotent). Originals are kept as `*.pre-addon.bak` (do not commit them). All new guest milestones are `patterns_status: DRAFT`: patterns must come from real guest output first (docs/PROPOSALS.md), then you finalize and relock.

## Decisions only you can make (AGENTS.md blocks them otherwise)
1. **Display controller.** The stub allowlist has `gpu_adreno` (rendering) but no display category. The stock DTB selects the Qualcomm display stack (MDSS/DPU + DSI panel), so by the rules it must be NATIVE. That is a large model; D1 may end `BLOCKED`. Options if you want to relax: add a `display_panel` stub category (edit AGENTS.md + mockscan ALLOW yourself), or keep native-only.
2. **Host GPU.** The guest GPU is Adreno 640 with the vendor GL stack; nothing in stock userspace can send work to your PC's GPU. So: guest rendering = `gpu_adreno` software fallback on CPU (TCG on the i5-2400 profile), and your GPU only presents/scales the window (`build-qemu-gfx.sh`, `run-display.sh`). Real acceleration needs virtio-gpu (virgl/venus) with a kernel containing the virtio-gpu driver and a Mesa GL stack in the guest. That is NOT stock kernel/userspace (AGENTS.md section 1, 3), so it is not implemented here.
3. **Network.** The stock DTB has no virtio-net and the Wi-Fi chip lives behind firmware domains. Pick a path via a proposal before N1 (for example a native USB/Ethernet-class model, or a documented stub).
4. **Google services (GMS): resolved, no action needed.** Observed on the owner's phone (same ROM): `pm list packages -s com.google.android.gms` lists GMS as a system app (the `/data/app` path from `pm path` is only its update), and GSF is at `/system/system_ext/priv-app/GoogleServicesFramework`. So GMS and GSF come from the stock images (`super.img`); the emulator gets them from the real image and `config/firmware.lock.json` stays unchanged. The `/data` copy of GMS/Play Store on the phone is user state and is not part of the emulator. Whether an app such as Pou works is still decided only by A1's real `pm`/`dumpsys` output.
5. **Root.** Patched boot must be reflected honestly by the boot-chain stub (`boot_rom_chain` hands off the real state; no forced green/locked values).

## Tools
- `tools/qemu_screens.py` + `tools/screencheck.py`: real screendumps, pixel stats, frame diff. Wrap with `tools/capture.py` for evidence.
- `scripts/gdb-attach.sh`, `scripts/doctor-gfx.sh`, `scripts/build-qemu-gfx.sh`, `scripts/run-display.sh`.
