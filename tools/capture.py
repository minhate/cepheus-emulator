#!/usr/bin/env python3
"""Run a command and record raw output + metadata as milestone evidence.

Usage: capture.py --out evidence/R4 --name positive|negative [--timeout S]
                  [--until REGEX] [--max-bytes N] [--stdin-text TEXT] -- cmd...
Writes <out>/<name>.log and <out>/<name>.meta.json (with sha256 of the log).
"""
import argparse, hashlib, json, os, queue, re, shutil, signal, subprocess, sys, threading, time
from datetime import datetime, timezone
from pathlib import Path


def sha256_bytes(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def sha256_file(p: str):
    try:
        h = hashlib.sha256()
        with open(p, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", required=True, choices=["positive", "negative"])
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--until", help="stop the command once this regex appears in the output")
    ap.add_argument("--max-bytes", type=int, default=524288)
    ap.add_argument("--stdin-text", help="text fed to stdin (e.g. QEMU monitor commands)")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args(argv)
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if not cmd:
        ap.error("command required after --")

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    until = re.compile(a.until, re.M) if a.until else None
    started = time.time()
    started_utc = datetime.now(timezone.utc).isoformat()
    buf = bytearray()
    truncated = False
    stopped = "exit"
    exit_code = None

    try:
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE if a.stdin_text is not None else subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True,
        )
    except OSError as e:
        buf += f"capture: failed to start: {e}\n".encode()
        stopped = "capture-error"
        proc = None

    if proc is not None:
        if a.stdin_text is not None:
            try:
                proc.stdin.write(a.stdin_text.encode())
                proc.stdin.close()
            except (BrokenPipeError, OSError):
                pass
        q: "queue.Queue[bytes|None]" = queue.Queue()

        def reader():
            fd = proc.stdout.fileno()
            while True:
                try:
                    chunk = os.read(fd, 65536)
                except OSError:
                    chunk = b""
                q.put(chunk or None)
                if not chunk:
                    return

        threading.Thread(target=reader, daemon=True).start()
        deadline = started + a.timeout
        while True:
            try:
                chunk = q.get(timeout=0.2)
            except queue.Empty:
                chunk = b""
            if chunk is None:
                break
            if chunk:
                room = a.max_bytes - len(buf)
                if room > 0:
                    buf += chunk[:room]
                if len(chunk) > room:
                    truncated = True
                if until and until.search(buf.decode("utf-8", "replace")):
                    stopped = "until"
                    break
            if time.time() > deadline:
                stopped = "timeout"
                break
        if stopped in ("until", "timeout"):
            for sig in (signal.SIGTERM, signal.SIGKILL):
                try:
                    os.killpg(proc.pid, sig)
                except ProcessLookupError:
                    break
                try:
                    proc.wait(timeout=3)
                    break
                except subprocess.TimeoutExpired:
                    continue
        else:
            proc.wait()
        exit_code = proc.returncode

    log_path = out / f"{a.name}.log"
    log_path.write_bytes(bytes(buf))
    resolved = shutil.which(cmd[0]) if cmd else None
    meta = {
        "schema": 1,
        "name": a.name,
        "argv": cmd,
        "started_utc": started_utc,
        "duration_s": round(time.time() - started, 3),
        "exit_code": exit_code,
        "stopped_by": stopped,
        "bytes": len(buf),
        "truncated": truncated,
        "log_sha256": sha256_bytes(bytes(buf)),
        "binary_sha256": sha256_file(resolved) if resolved else None,
    }
    (out / f"{a.name}.meta.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(f"capture: {a.name} stopped_by={stopped} bytes={len(buf)} -> {log_path}")
    return 0 if stopped != "capture-error" else 2


if __name__ == "__main__":
    sys.exit(main())
