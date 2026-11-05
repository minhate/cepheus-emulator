#!/usr/bin/env python3
"""USER ONLY. Append add-on milestones to milestones.json without touching existing ones.
Dry run by default. --apply writes (backup: milestones.json.pre-addon.bak). Then relock yourself."""
import argparse, json, os, shutil, sys
from pathlib import Path

ROOT = Path(os.environ.get("CEPHEUS_ROOT") or Path(__file__).resolve().parents[1])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--addon", default=str(ROOT / "milestones.addon.json"))
    ap.add_argument("--apply", action="store_true")
    a = ap.parse_args(argv)
    spec_p = ROOT / "milestones.json"
    base = json.loads(spec_p.read_text())
    add = json.loads(Path(a.addon).read_text())
    have = {m["id"] for m in base["milestones"]}
    new = [m for m in add["milestones"] if m["id"] not in have]
    skipped = [m["id"] for m in add["milestones"] if m["id"] in have]
    ids = have | {m["id"] for m in new}
    errs = [f"{m['id']} depends on unknown {d}" for m in new for d in m.get("depends", []) if d not in ids]
    if errs:
        print("\n".join("ERROR: " + e for e in errs))
        return 1
    print(f"new milestones: {', '.join(m['id'] for m in new) or '-'}")
    print(f"already present (skipped): {', '.join(skipped) or '-'}")
    if not a.apply:
        print("dry run only. Re-run with --apply.")
        return 0
    shutil.copy(spec_p, str(spec_p) + ".pre-addon.bak")
    base["milestones"].extend(new)
    spec_p.write_text(json.dumps(base, indent=2) + "\n")
    print("applied. NEXT (you): python3 tools/milestone.py relock --yes && python3 tools/milestone.py roadmap && make ci")
    return 0


if __name__ == "__main__":
    sys.exit(main())
