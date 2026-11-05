#!/usr/bin/env python3
"""USER ONLY. One-shot, idempotent: merge add-on milestones, insert AGENTS.md section 13,
replace the stale agent/KICKOFF.md (from agent/KICKOFF_ADDON.md), relock the spec, regenerate docs/ROADMAP.md.
Dry run by default; --apply writes. Backups: *.pre-addon.bak (only on first write)."""
import argparse, json, os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(os.environ.get("CEPHEUS_ROOT") or Path(__file__).resolve().parents[1])
HERE = Path(__file__).resolve().parent
BEGIN, END = "<!-- ADDON-SECTION:BEGIN -->", "<!-- ADDON-SECTION:END -->"
KICK = "<!-- ADDON-KICKOFF -->"


def backup(p: Path):
    b = Path(str(p) + ".pre-addon.bak")
    if p.exists() and not b.exists():
        shutil.copy(p, b)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--apply", action="store_true")
    a = ap.parse_args(argv)
    section = (ROOT / "agent" / "AGENTS_ADDON.md").read_text()
    kick_src = (ROOT / "agent" / "KICKOFF_ADDON.md").read_text()
    kp = ROOT / "agent" / "KICKOFF.md"
    agents_p = ROOT / "AGENTS.md"
    agents = agents_p.read_text()
    spec = json.loads((ROOT / "milestones.json").read_text())
    add = json.loads((ROOT / "milestones.addon.json").read_text())
    have = {m["id"] for m in spec["milestones"]}
    todo_ms = [m["id"] for m in add["milestones"] if m["id"] not in have]
    has_section = BEGIN in agents and END in agents
    kick_done = kp.exists() and KICK in kp.read_text()
    print(f"milestones to add: {', '.join(todo_ms) or '-'}")
    print(f"AGENTS.md section 13: {'update in place' if has_section else 'append'}")
    print(f"agent/KICKOFF.md: {'already replaced' if kick_done else 'replace (backup kept)'}")
    if not a.apply:
        print("dry run only. Re-run with --apply.")
        return 0
    # 1. AGENTS.md section (insert or refresh between markers)
    backup(agents_p)
    if has_section:
        head, rest = agents.split(BEGIN, 1)
        tail = rest.split(END, 1)[1]
        agents = head + section.rstrip("\n") + tail
    else:
        agents = agents.rstrip("\n") + "\n\n" + section
    agents_p.write_text(agents)
    # 2. KICKOFF.md: replace the stale one (backup on first write)
    if not kick_done:
        backup(kp)
        kp.write_text(kick_src)
    gi = ROOT / ".gitignore"
    cur = gi.read_text() if gi.exists() else ""
    if "*.pre-addon.bak" not in cur:
        gi.write_text(cur.rstrip("\n") + "\n*.pre-addon.bak\n")
    # 3. milestones + relock + roadmap
    sys.path.insert(0, str(HERE))
    import merge_milestones
    if merge_milestones.main(["--apply", "--addon", str(ROOT / "milestones.addon.json")]) != 0:
        return 1
    env = dict(os.environ, CEPHEUS_ROOT=str(ROOT))
    for args in (["relock", "--yes"], ["roadmap"]):
        r = subprocess.run([sys.executable, str(HERE / "milestone.py"), *args], env=env, cwd=ROOT)
        if r.returncode != 0:
            print(f"apply_addon: milestone.py {' '.join(args)} failed")
            return 1
    print("done. NEXT (you): make ci, then review `git status` and commit.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
