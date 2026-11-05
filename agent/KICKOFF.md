# Kickoff prompt (paste this to the agent on a fresh start)

You are starting the project from zero. The previous attempt was a failed mock-based scaffold; nothing from it is reusable except lessons.

1. Read `AGENTS.md` fully, then `milestones.json` and `STATUS.json`. Do not read anything else yet.
2. Restate the mission in 3 lines (it must say: native custom QEMU machine `cepheus`, stock kernel + stock DTB + stock userspace, stubs only from the allowlist).
3. Start milestone **R0** only. Plan in <= 5 lines, then do it.
   - R0: run `scripts/doctor.sh`. If tools are missing, list the exact install command and ask me before installing anything.
4. Follow the milestone protocol: `set R0 IN-PROGRESS`, produce positive + negative evidence with `tools/capture.py`, run `tools/milestone.py verify R0`, run `make ci`.
5. Report using the template in AGENTS.md and STOP. Do not begin R1 until I say so.

If anything in `milestones.json` looks wrong, write it in `docs/PROPOSALS.md` and stop. Never edit the spec. Never create success markers. If blocked, say BLOCKED with the concrete reason.
