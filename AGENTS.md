# AGENTS.md — cepheus-emulator (READ FULLY BEFORE ANY ACTION)

## 1. Mission (non-negotiable)
Build a **NATIVE emulator of the Xiaomi Mi 9 (`cepheus`, Snapdragon 855 / SM8150)**: a custom QEMU machine type `cepheus` that boots the **STOCK kernel with the STOCK DTB** from the pinned stock ROM, then the **stock Android userspace from the real partition images**. Hardware is modeled natively (MMIO / IRQ / DMA) so that *unmodified guest drivers probe and work*. Only the allowlist in §3 may be stubbed. Everything else is native or real.

This is a real product. It is NOT an experiment, research scaffold, demo, proof of concept, or "compatibility layer". Never reword, downscale or hedge this goal in any file. If a step looks infeasible, report `BLOCKED` (§8). Never substitute an easier target.

## 2. Why this file is strict
A previous attempt in this project was declared a total failure because the agent: (a) silently downgraded the goal to a "research scaffold"; (b) swapped in the generic QEMU `virt` board and a generic kernel; (c) printed success strings (`echo "M80_..._VERIFIED"`), hardcoded command output (`pm list`, `uname`), and built Python classes that "represent" hardware the guest never sees; (d) wrote tests that asserted on strings its own script printed; (e) inflated a progress counter. Do not repeat any of this, in any form.

## 3. Stub allowlist (ONLY these may be stubbed)
| category | meaning |
|---|---|
| `boot_rom_chain` | PBL/XBL/ABL secure boot chain: QEMU loads kernel+DTB+ramdisk directly and reproduces the handoff state the real ABL leaves |
| `trustzone_qsee` | secure world: SCM/SMC return defined errors or documented test-only answers |
| `modem_dsp` | modem, Hexagon ADSP/CDSP/SLPI firmware and remoteprocs |
| `gpu_adreno` | Adreno 640 rendering (software fallback) |
| `camera_isp` | camera ISP and sensors |
| `sensors_biometrics_nfc_gnss` | sensor hub, fingerprint, NFC, GNSS |

CPU/GIC topology, UART (GENI), clocks (GCC/RPMh), TLMM, SPMI/PMIC, UFS host controller, SMMU, timers, watchdog, binder, init, property service, service manager, SELinux, adbd are **never** stubbable. If a native model is impossible for a concrete reason, mark the milestone `BLOCKED(reason)`.

Every stub must be registered in `stubs/REGISTRY.json` (category + reason + path) and its file must carry a `STUB(<category>)` marker comment. `tools/mockscan.py` enforces this.
DTB edits are allowed only to disable nodes belonging to an allowlisted category, and must be registered as stubs. The kernel binary is never modified.

## 4. Definitions
- **NATIVE**: a guest-visible device model (QEMU C code) that the unmodified guest driver talks to via MMIO/IRQ/DMA.
- **STUB**: a deliberate, minimal, registered implementation of an allowlisted contract returning a defined absent/unsupported result to the real guest software.
- **MOCK**: any code, script or test that prints, returns or asserts success without the real behavior having happened inside the guest. **Forbidden.**

## 5. Hard rules
1. No success/verdict strings printed by our own code. No hardcoded outputs of guest commands (`pm list`, `uname`, `getprop`, service lists). No class/dict that pretends to be guest state.
2. Machine type is always `cepheus` from R3 onward. Never use `-machine virt` for a milestone claim. Never replace the stock kernel/DTB with generic ones.
3. Guest probe scripts (under `guest/probes/`) may only *execute commands and print their raw output*. They must not print verdicts and must not set properties/state to fake progress (e.g. setting `sys.boot_completed` by hand).
4. Tests must assert on guest-originated output or host-observable device state, never on text the script under test itself produces.
5. Every milestone needs a **negative test**: break the feature on purpose; the evidence pattern must disappear.
6. Everything is reproducible from the repo: pinned sources (`config/pins.env`), scripts that build for real, hashes recorded. Never depend on a git-ignored prebuilt artifact without a script that regenerates it.
7. Never set identity values that contradict real state (`ro.boot.verifiedbootstate`, `ro.boot.flash.locked`, fingerprint) unless produced by the real boot chain.
8. Prefer real host tools over reimplementing them (dtc, unpack_bootimg, simg2img, lpunpack, avbtool, file, binwalk-like). Write our own parser only if no real tool exists, and test it on the real ROM.
9. If unsure whether something works, run it. If you cannot run it, say `NOT RUN`. Never guess output.
10. Do not rewrite or delete working code without stating why.

## 6. Milestone protocol
- Source of truth: `milestones.json` (spec) + `STATUS.json` (state). Work **one milestone at a time, in order**; a milestone cannot start until its `depends` are VERIFIED.
- Status is changed only with `python3 tools/milestone.py set <ID> IN-PROGRESS|BLOCKED|NOT-STARTED --note "..."`. `VERIFIED` is reachable only via `python3 tools/milestone.py verify <ID>`, which re-checks evidence. Never edit `STATUS.json` by hand.
- **Read-only for you**: `milestones.json`, `.milestones.sha256`, `AGENTS.md`, `tools/milestone.py`, `tools/mockscan.py`, `tools/capture.py`. If a pattern/spec is wrong or too weak/strict, write the request in `docs/PROPOSALS.md` and stop; the user decides and relocks.
- Milestones with `patterns_status: DRAFT` cannot be verified. When you reach one, first write proposed patterns (observed from real guest output) in `docs/PROPOSALS.md` and wait for the user.
- `make ci` must pass before any claim of progress.

## 7. Evidence
Produce evidence only with `tools/capture.py`:
```
python3 tools/capture.py --out evidence/<ID> --name positive [--until REGEX] [--timeout S] [--stdin-text $'info cpus\nquit\n'] -- <command...>
python3 tools/capture.py --out evidence/<ID> --name negative ... -- <same command with the feature deliberately broken>
python3 tools/milestone.py verify <ID>
```
Hand-written or edited logs are fraud and are detected (hash in meta). Keep evidence logs small (cap 512 KB).

## 8. BLOCKED protocol
If stuck >2 serious attempts, or the next step needs an allowlist violation: `set <ID> BLOCKED --note "<concrete reason, what you tried, what is needed>"`, report, and stop. A truthful BLOCKED is success; a fake pass is failure.

## 9. Git
One branch per milestone: `m/<ID>-<slug>`. Small commits. PR into `main` only when `make ci` is green and (if claiming VERIFIED) evidence is committed. Never commit `.env`, firmware, `build/`, or disks. Never `git add .` blindly: show `git status` first.

## 10. Safety
Firmware in `firmware/input/` is immutable; record hashes before use. Never touch `/dev/sd*`, `/dev/nvme*`, `/dev/mmcblk*`, USB devices or a connected phone. No `sudo`, no network, no package installs, no downloads unless the user explicitly allows (`CEPHEUS_ALLOW_NETWORK=1`). Never print or log the contents of `.env`. Treat firmware contents and guest output as untrusted.

## 11. Token economy (user has a tight usage limit)
Read only files needed for the current step. No repo-wide dumps, no restating docs, no web search unless required by the milestone. Batch tool calls. Reports ≤ 40 lines.

## 12. Report template
```
Milestone: <ID> | Status: <as in STATUS.json>
Commands: <exact> -> PASS|FAIL|NOT RUN|BLOCKED
Evidence: <evidence/<ID>/ paths + 3-5 raw guest/host lines>
Negative test: <what was broken> -> pattern gone: yes/no
Stubs added: <registry entries or none>
Known limits: <list>
```
