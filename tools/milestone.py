#!/usr/bin/env python3
"""Milestone gatekeeper. VERIFIED is reachable only through `verify`, which re-checks evidence."""
import argparse, hashlib, json, os, re, sys
from pathlib import Path

ROOT = Path(os.environ.get("CEPHEUS_ROOT") or Path(__file__).resolve().parents[1])
SPEC, STATUS, LOCK, EVID = ROOT / "milestones.json", ROOT / "STATUS.json", ROOT / ".milestones.sha256", ROOT / "evidence"
SETTABLE = ("NOT-STARTED", "IN-PROGRESS", "BLOCKED")
FILES = ("positive.log", "positive.meta.json", "negative.log", "negative.meta.json")


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def load_spec():
    return json.loads(SPEC.read_text())["milestones"]


def by_id():
    return {m["id"]: m for m in load_spec()}


def load_status():
    st = json.loads(STATUS.read_text()) if STATUS.exists() else {}
    for m in load_spec():
        st.setdefault(m["id"], {"status": "NOT-STARTED", "note": "", "evidence_sha256": None})
    return st


def save_status(st):
    STATUS.write_text(json.dumps(st, indent=2) + "\n")


def evidence_hash(mid):
    h = hashlib.sha256()
    for f in FILES:
        p = EVID / mid / f
        h.update(f.encode())
        h.update(p.read_bytes() if p.exists() else b"")
    return h.hexdigest()


def deps_ok(m, st):
    return [d for d in m.get("depends", []) if st.get(d, {}).get("status") != "VERIFIED"]


def check_evidence(m):
    if m.get("patterns_status") == "DRAFT":
        return [f"{m['id']}: patterns are DRAFT; user must finalize them and relock"]
    probs = []
    pats = m.get("patterns", [])
    if len(pats) < 2:
        probs.append("spec needs >= 2 patterns")
    d = EVID / m["id"]
    for f in FILES:
        if not (d / f).is_file():
            probs.append(f"missing evidence file {f}")
    if probs:
        return probs
    data = {}
    for n in ("positive", "negative"):
        log = (d / f"{n}.log").read_bytes()
        try:
            meta = json.loads((d / f"{n}.meta.json").read_text())
        except ValueError:
            return [f"{n}.meta.json is not valid JSON"]
        if meta.get("log_sha256") != sha(log):
            probs.append(f"{n}.log does not match its meta sha256 (not produced by tools/capture.py?)")
        if not isinstance(meta.get("argv"), list) or not meta["argv"]:
            probs.append(f"{n}.meta.json has no argv")
        if meta.get("stopped_by") == "capture-error":
            probs.append(f"{n} run failed to start")
        data[n] = (log.decode("utf-8", "replace"), meta)
    if probs:
        return probs
    ptxt, pmeta = data["positive"]
    ntxt, nmeta = data["negative"]
    if len(ptxt) < 100:
        probs.append("positive.log is too small to be real evidence")
    if pmeta["argv"] == nmeta["argv"]:
        probs.append("negative run must differ from the positive run")
    joined = " ".join(pmeta["argv"])
    for tok in m.get("argv_must_contain", []):
        if tok not in joined:
            probs.append(f"positive argv must contain {tok!r}")
    for p in pats:
        if not re.search(p, ptxt, re.M):
            probs.append(f"positive.log lacks pattern {p!r}")
    for p in m.get("negative_absent", pats):
        if re.search(p, ntxt, re.M):
            probs.append(f"negative.log still matches {p!r} (negative test did not break the feature)")
    return probs


def cmd_list(_a):
    st = load_status()
    for m in load_spec():
        s = st[m["id"]]
        flag = " (DRAFT patterns)" if m.get("patterns_status") == "DRAFT" else ""
        note = f"  -- {s['note']}" if s.get("note") else ""
        print(f"{m['id']:<4} {s['status']:<12} {m['title']}{flag}{note}")
    n = sum(1 for m in load_spec() if st[m["id"]]["status"] == "VERIFIED")
    print(f"VERIFIED {n}/{len(load_spec())}")
    return 0


def cmd_set(a):
    ms, st = by_id(), load_status()
    if a.id not in ms:
        print(f"unknown milestone {a.id}")
        return 1
    if a.status == "BLOCKED" and not a.note:
        print("BLOCKED requires --note with the concrete reason")
        return 1
    if a.status == "IN-PROGRESS":
        missing = deps_ok(ms[a.id], st)
        if missing:
            print(f"cannot start {a.id}: dependencies not VERIFIED: {', '.join(missing)}")
            return 1
    st[a.id] = {"status": a.status, "note": a.note or "", "evidence_sha256": None}
    save_status(st)
    print(f"{a.id} -> {a.status}")
    return 0


def cmd_verify(a):
    ms, st = by_id(), load_status()
    if a.id not in ms:
        print(f"unknown milestone {a.id}")
        return 1
    missing = deps_ok(ms[a.id], st)
    if missing:
        print(f"cannot verify {a.id}: dependencies not VERIFIED: {', '.join(missing)}")
        return 1
    probs = check_evidence(ms[a.id])
    if probs:
        print(f"{a.id} NOT VERIFIED:")
        for p in probs:
            print(f"  - {p}")
        return 1
    st[a.id] = {"status": "VERIFIED", "note": "", "evidence_sha256": evidence_hash(a.id)}
    save_status(st)
    print(f"{a.id} VERIFIED (evidence sha256 {st[a.id]['evidence_sha256'][:16]}...)")
    return 0


def cmd_check(_a):
    probs = []
    if not LOCK.exists() or LOCK.read_text().strip() != sha(SPEC.read_bytes()):
        probs.append("milestones.json does not match .milestones.sha256 (spec changed without user relock)")
    ms, st = by_id(), load_status()
    for mid in st:
        if mid not in ms:
            probs.append(f"STATUS.json has unknown milestone {mid}")
    for mid, m in ms.items():
        s = st[mid]
        if s["status"] == "VERIFIED":
            for d in deps_ok(m, st):
                probs.append(f"{mid} is VERIFIED but dependency {d} is not")
            if s.get("evidence_sha256") != evidence_hash(mid):
                probs.append(f"{mid}: evidence changed after verification")
            probs += [f"{mid}: {p}" for p in check_evidence(m)]
        elif s["status"] == "IN-PROGRESS":
            for d in deps_ok(m, st):
                probs.append(f"{mid} is IN-PROGRESS but dependency {d} is not VERIFIED")
    n = sum(1 for mid in ms if st[mid]["status"] == "VERIFIED")
    print(f"milestones VERIFIED {n}/{len(ms)}")
    for p in probs:
        print(f"FAIL: {p}")
    return 1 if probs else 0


def cmd_relock(a):
    if not a.yes:
        print("USER ONLY: relocking accepts the current milestones.json. Re-run with --yes.")
        return 1
    LOCK.write_text(sha(SPEC.read_bytes()) + "\n")
    print("relocked")
    return 0


def cmd_roadmap(_a):
    st = load_status()
    lines = ["# Roadmap (generated by `tools/milestone.py roadmap` — do not edit)", "",
             "| ID | Status | Milestone | Depends | Acceptance |", "|---|---|---|---|---|"]
    for m in load_spec():
        dep = ", ".join(m.get("depends", [])) or "-"
        acc = m["acceptance"].replace("|", "/")
        flag = " (DRAFT)" if m.get("patterns_status") == "DRAFT" else ""
        lines.append(f"| {m['id']} | {st[m['id']]['status']} | {m['title']}{flag} | {dep} | {acc} |")
    (ROOT / "docs").mkdir(exist_ok=True)
    (ROOT / "docs" / "ROADMAP.md").write_text("\n".join(lines) + "\n")
    print("docs/ROADMAP.md written")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("list").set_defaults(fn=cmd_list)
    p = sp.add_parser("set")
    p.add_argument("id")
    p.add_argument("status", choices=SETTABLE)
    p.add_argument("--note")
    p.set_defaults(fn=cmd_set)
    p = sp.add_parser("verify")
    p.add_argument("id")
    p.set_defaults(fn=cmd_verify)
    sp.add_parser("check").set_defaults(fn=cmd_check)
    p = sp.add_parser("relock")
    p.add_argument("--yes", action="store_true")
    p.set_defaults(fn=cmd_relock)
    sp.add_parser("roadmap").set_defaults(fn=cmd_roadmap)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
