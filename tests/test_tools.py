import json, os, shutil, subprocess, sys, tempfile, unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PY = sys.executable


def run(tool, *args, root):
    env = dict(os.environ, CEPHEUS_ROOT=str(root))
    return subprocess.run([PY, str(REPO / "tools" / tool), *args], cwd=root, env=env, capture_output=True, text=True)


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)


class MockScan(Base):
    def test_flags_verdict_echo_and_unregistered_stub(self):
        (self.tmp / "scripts").mkdir()
        (self.tmp / "scripts" / "bad.sh").write_text('echo "M80_BOOT_VERIFIED: ok"\n/system/bin/setprop sys.boot_completed 1\n')
        (self.tmp / "scripts" / "stub.c").write_text("/* STUB(modem_dsp) */\n")
        r = run("mockscan.py", root=self.tmp)
        self.assertEqual(r.returncode, 1)
        self.assertIn("verdict printed", r.stdout)
        self.assertIn("boot_completed set by hand", r.stdout)
        self.assertIn("not in stubs/REGISTRY.json", r.stdout)

    def test_clean_and_registered_stub_pass(self):
        (self.tmp / "scripts").mkdir()
        (self.tmp / "stubs").mkdir()
        (self.tmp / "scripts" / "ok.sh").write_text("ls /proc\n")
        (self.tmp / "scripts" / "modem.c").write_text("/* STUB(modem_dsp) */\n")
        (self.tmp / "stubs" / "REGISTRY.json").write_text(json.dumps({"stubs": [
            {"path": "scripts/modem.c", "category": "modem_dsp", "reason": "no modem firmware is emulated for this device"}]}))
        r = run("mockscan.py", root=self.tmp)
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_bad_category_rejected(self):
        (self.tmp / "stubs").mkdir()
        (self.tmp / "x.c").write_text("/* STUB(uart) */\n")
        (self.tmp / "stubs" / "REGISTRY.json").write_text(json.dumps({"stubs": [
            {"path": "x.c", "category": "uart", "reason": "pretending the uart exists to move faster"}]}))
        r = run("mockscan.py", root=self.tmp)
        self.assertEqual(r.returncode, 1)
        self.assertIn("not in allowlist", r.stdout)

    def test_repo_itself_is_clean(self):
        r = subprocess.run([PY, str(REPO / "tools" / "mockscan.py")], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)


class Capture(Base):
    def test_capture_records_log_and_hash(self):
        r = run("capture.py", "--out", "ev", "--name", "positive", "--", PY, "-c", "print('hello 42')", root=self.tmp)
        self.assertEqual(r.returncode, 0, r.stderr)
        meta = json.loads((self.tmp / "ev" / "positive.meta.json").read_text())
        import hashlib
        self.assertEqual(meta["log_sha256"], hashlib.sha256((self.tmp / "ev" / "positive.log").read_bytes()).hexdigest())
        self.assertIn("hello 42", (self.tmp / "ev" / "positive.log").read_text())

    def test_until_stops_early_and_timeout(self):
        code = "import time;print('ready',flush=True);time.sleep(30)"
        run("capture.py", "--out", "ev", "--name", "positive", "--until", "ready", "--timeout", "20", "--", PY, "-c", code, root=self.tmp)
        self.assertEqual(json.loads((self.tmp / "ev" / "positive.meta.json").read_text())["stopped_by"], "until")
        run("capture.py", "--out", "ev", "--name", "negative", "--timeout", "1", "--", PY, "-c", "import time;time.sleep(30)", root=self.tmp)
        self.assertEqual(json.loads((self.tmp / "ev" / "negative.meta.json").read_text())["stopped_by"], "timeout")


class Milestone(Base):
    def spec(self, ms):
        (self.tmp / "milestones.json").write_text(json.dumps({"schema": 1, "milestones": ms}))
        run("milestone.py", "relock", "--yes", root=self.tmp)

    def cap(self, mid, name, text):
        run("capture.py", "--out", f"evidence/{mid}", "--name", name, "--", PY, "-c", f"print({text!r} * 20)", root=self.tmp)

    T0 = {"id": "T0", "title": "t", "acceptance": "a", "depends": [], "patterns": [r"hello \d+", "world"], "argv_must_contain": ["-c"]}
    T1 = {"id": "T1", "title": "t", "acceptance": "a", "depends": ["T0"], "patterns": [], "patterns_status": "DRAFT"}

    def test_full_flow_and_tamper_detection(self):
        self.spec([self.T0, self.T1])
        self.cap("T0", "positive", "hello 42 world ")
        self.cap("T0", "negative", "nothing here ")
        self.assertEqual(run("milestone.py", "verify", "T0", root=self.tmp).returncode, 0)
        self.assertEqual(run("milestone.py", "check", root=self.tmp).returncode, 0)
        self.assertEqual(run("milestone.py", "set", "T1", "IN-PROGRESS", root=self.tmp).returncode, 0)
        r = run("milestone.py", "verify", "T1", root=self.tmp)
        self.assertEqual(r.returncode, 1)
        self.assertIn("DRAFT", r.stdout)
        log = self.tmp / "evidence" / "T0" / "positive.log"
        log.write_text(log.read_text() + "forged\n")
        self.assertEqual(run("milestone.py", "check", root=self.tmp).returncode, 1)

    def test_weak_negative_rejected(self):
        self.spec([self.T0])
        self.cap("T0", "positive", "hello 42 world ")
        self.cap("T0", "negative", "hello 43 world x")
        r = run("milestone.py", "verify", "T0", root=self.tmp)
        self.assertEqual(r.returncode, 1)
        self.assertIn("negative test did not break", r.stdout)

    def test_handwritten_log_rejected(self):
        self.spec([self.T0])
        d = self.tmp / "evidence" / "T0"
        d.mkdir(parents=True)
        (d / "positive.log").write_text("hello 42 world " * 20)
        (d / "negative.log").write_text("x" * 200)
        (d / "positive.meta.json").write_text(json.dumps({"argv": ["a", "-c"], "log_sha256": "0" * 64}))
        (d / "negative.meta.json").write_text(json.dumps({"argv": ["b"], "log_sha256": "0" * 64}))
        r = run("milestone.py", "verify", "T0", root=self.tmp)
        self.assertEqual(r.returncode, 1)
        self.assertIn("capture.py", r.stdout)

    def test_cannot_set_verified_or_skip_deps(self):
        self.spec([self.T0, self.T1])
        self.assertEqual(run("milestone.py", "set", "T0", "VERIFIED", root=self.tmp).returncode, 2)
        self.assertEqual(run("milestone.py", "set", "T1", "IN-PROGRESS", root=self.tmp).returncode, 1)
        self.assertEqual(run("milestone.py", "set", "T0", "BLOCKED", root=self.tmp).returncode, 1)

    def test_spec_change_without_relock_fails_check(self):
        self.spec([self.T0])
        (self.tmp / "milestones.json").write_text(json.dumps({"schema": 1, "milestones": [dict(self.T0, patterns=["a", "b"])]}))
        r = run("milestone.py", "check", root=self.tmp)
        self.assertEqual(r.returncode, 1)
        self.assertIn("relock", r.stdout)


if __name__ == "__main__":
    unittest.main()
