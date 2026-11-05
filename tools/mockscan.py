#!/usr/bin/env python3
"""Anti-mock scanner + stub registry enforcement. Exit 1 on any finding."""
import json, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(os.environ.get("CEPHEUS_ROOT") or Path(__file__).resolve().parents[1])
SUFFIX = {".py", ".sh", ".c", ".h", ".cc", ".cpp", ".S", ".rc", ".rs", ".go", ".patch", ".mk"}
SKIP_DIRS = {".git", "build", "firmware", "evidence", "__pycache__", ".venv", "docs", "tests", "lab", "out"}
SKIP_FILES = {"tools/mockscan.py"}
ALLOW = {"trustzone_qsee", "modem_dsp", "gpu_adreno", "camera_isp", "sensors_biometrics_nfc_gnss", "boot_rom_chain"}
STUB_MARK = re.compile(r"STUB\(([a-z_]+)\)")
RULES = [
    (re.compile(r"echo\s+[\"'][^\"'\n]*(VERIFIED|PASSED|SUCCESS)"), "verdict printed by our own script"),
    (re.compile(r"[\"'][A-Z][A-Z0-9_]*_(VERIFIED|PASSED)\b"), "verdict marker string"),
    (re.compile(r"\"reproducible\"\s*:\s*true"), "hardcoded reproducibility claim"),
    (re.compile(r"\"verified\"\s*:\s*true"), "hardcoded verified flag"),
    (re.compile(r"class\s+\w*(Mock|Fake|Simulator|Simulated)\w*"), "mock/fake/simulator class"),
    (re.compile(r"[\"']package:(com|android)\."), "hardcoded package-manager output"),
    (re.compile(r"setprop\s+sys\.boot_completed\s+1"), "boot_completed set by hand"),
    (re.compile(r"echo\s+[\"']?(HAL_ONLINE|REGISTERED_SERVICES|SELINUX_MODE)"), "fake service/HAL/SELinux status"),
]


def files():
    for dp, dns, fns in os.walk(ROOT):
        dns[:] = [d for d in dns if d not in SKIP_DIRS]
        for fn in fns:
            p = Path(dp) / fn
            rel = p.relative_to(ROOT).as_posix()
            if p.suffix in SUFFIX or fn == "Makefile":
                if rel not in SKIP_FILES:
                    yield p, rel


def main() -> int:
    findings = []
    marked = {}
    for p, rel in files():
        try:
            text = p.read_text(errors="replace")
        except OSError:
            continue
        for i, line in enumerate(text.splitlines(), 1):
            for rx, msg in RULES:
                if rx.search(line):
                    findings.append(f"{rel}:{i}: {msg}")
            m = STUB_MARK.search(line)
            if m:
                marked.setdefault(rel, set()).add(m.group(1))

    reg_path = ROOT / "stubs" / "REGISTRY.json"
    entries = json.loads(reg_path.read_text()).get("stubs", []) if reg_path.exists() else []
    registered = {}
    for e in entries:
        path, cat, reason = e.get("path", ""), e.get("category", ""), (e.get("reason") or "").strip()
        if cat not in ALLOW:
            findings.append(f"stubs/REGISTRY.json: {path}: category {cat!r} not in allowlist")
        if len(reason) < 20:
            findings.append(f"stubs/REGISTRY.json: {path}: reason missing or too short")
        if not (ROOT / path).is_file():
            findings.append(f"stubs/REGISTRY.json: {path}: file does not exist")
        registered[path] = cat
    for path, cats in marked.items():
        if path not in registered:
            findings.append(f"{path}: has STUB marker but is not in stubs/REGISTRY.json")
        elif registered[path] not in cats:
            findings.append(f"{path}: registry category {registered[path]!r} != marker {sorted(cats)}")
    for path, cat in registered.items():
        if (ROOT / path).is_file() and path not in marked and (ROOT / path).suffix in SUFFIX:
            findings.append(f"{path}: registered stub lacks a STUB({cat}) marker")
    sdir = ROOT / "stubs"
    if sdir.is_dir():
        for p in sdir.rglob("*"):
            rel = p.relative_to(ROOT).as_posix()
            if p.is_file() and p.name not in ("README.md", "REGISTRY.json") and rel not in registered:
                findings.append(f"{rel}: file in stubs/ is not registered")

    if (ROOT / ".git").exists():
        try:
            tracked = subprocess.run(["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True, check=True).stdout.split("\n")
        except (OSError, subprocess.CalledProcessError):
            tracked = []
        for t in filter(None, tracked):
            if t == ".env" or (t.startswith(("firmware/input/", "firmware/extracted/", "build/")) and not t.endswith(".keep")):
                findings.append(f"{t}: must never be tracked")

    for f in findings:
        print("MOCKSCAN:", f)
    print(f"mockscan: {len(findings)} finding(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
