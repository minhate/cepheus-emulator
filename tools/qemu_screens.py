#!/usr/bin/env python3
"""Run QEMU with a monitor socket, take two real screendumps, analyze them with screencheck.
Wrap with tools/capture.py to record evidence. Do not pass your own -monitor.
Usage: qemu_screens.py --out DIR [--delay 60] [--delay2 20] [--between "mouse_move 5 5"]... [--png] -- qemu-system-aarch64 -machine cepheus ..."""
import argparse, os, socket, subprocess, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import screencheck  # noqa: E402

PROMPT = b"(qemu) "


def read_prompt(s, timeout=30.0):
    s.settimeout(timeout)
    buf = b""
    while not buf.endswith(PROMPT):
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
    return buf.decode("utf-8", "replace")


def connect(path, proc, timeout=60.0):
    end = time.time() + timeout
    while time.time() < end:
        if proc.poll() is not None:
            return None
        if os.path.exists(path):
            try:
                s = socket.socket(socket.AF_UNIX)
                s.connect(path)
                read_prompt(s)
                return s
            except OSError:
                pass
        time.sleep(0.2)
    return None


def mon(s, cmd):
    s.sendall(cmd.encode() + b"\n")
    return read_prompt(s)


def wait(proc, secs):
    end = time.time() + secs
    while time.time() < end:
        if proc.poll() is not None:
            return False
        time.sleep(0.1)
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--delay", type=float, default=60)
    ap.add_argument("--delay2", type=float, default=20)
    ap.add_argument("--between", action="append", default=[])
    ap.add_argument("--png", action="store_true")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args(argv)
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if not cmd:
        ap.error("qemu command required after --")
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    sock = out / "mon.sock"
    if sock.exists():
        sock.unlink()
    proc = subprocess.Popen(cmd + ["-monitor", f"unix:{sock},server=on,wait=off"], stdin=subprocess.DEVNULL)
    rc = 0
    try:
        s = connect(str(sock), proc)
        if s is None:
            print(f"qemu_screens: monitor not reachable (qemu rc={proc.poll()})", flush=True)
            return 3
        if not wait(proc, a.delay):
            print(f"qemu_screens: qemu exited early rc={proc.returncode}", flush=True)
            return 3
        shots = []
        for i in (1, 2):
            p = out / f"shot{i}.ppm"
            print(f"qemu_screens: monitor> screendump {p}: {mon(s, f'screendump {p}').strip()}", flush=True)
            shots.append(str(p))
            if i == 1:
                for c in a.between:
                    print(f"qemu_screens: monitor> {c}: {mon(s, c).strip()}", flush=True)
                if not wait(proc, a.delay2):
                    print("qemu_screens: qemu exited before second shot", flush=True)
                    return 3
        rc = screencheck.main(shots + (["--png-dir", str(out)] if a.png else []))
        try:
            s.sendall(b"quit\n")
        except OSError:
            pass
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
    return rc


if __name__ == "__main__":
    sys.exit(main())
