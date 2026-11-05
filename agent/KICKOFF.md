<!-- ADDON-KICKOFF -->
# Resume prompt (paste to the agent at the start of EVERY session)

1. Read `AGENTS.md` fully (including section 13), then run `python3 tools/milestone.py list` and read `STATUS.json`. Read nothing else yet.
2. Restate the mission in 3 lines: native custom QEMU machine `cepheus`; stock kernel + stock DTB + stock userspace from the real images; stubs only from the allowlist.
3. Name the current milestone: the first in list order with all `depends` VERIFIED and status not VERIFIED. If it is IN-PROGRESS or BLOCKED, quote the note in `STATUS.json` and continue from it.
4. If its patterns are DRAFT: write proposed patterns from real observed output in `docs/PROPOSALS.md` and STOP.
5. Otherwise: plan in <= 5 lines, `set <ID> IN-PROGRESS`, branch `m/<ID>-<slug>`, make the smallest real change, capture positive and negative evidence with `tools/capture.py`, run `tools/milestone.py verify <ID>`, run `make ci`.
6. Report with the template in AGENTS.md (section 12) and STOP. Do not start the next milestone until the owner says so.

Hard reminders: never edit the spec or locked tools; never print success markers; never hardcode guest output; if unsure, run it; if you cannot run it, say NOT RUN; if stuck after 2 serious attempts, BLOCKED with the concrete reason. Usage limit is tight: read only what the step needs, batch tool calls, report in <= 40 lines.
