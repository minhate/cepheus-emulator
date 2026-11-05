import json, os, shutil, struct, subprocess, sys, tempfile, unittest, zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
import screencheck  # noqa: E402


def ppm(path, w, h, pixels):
    Path(path).write_bytes(b"P6\n# c\n%d %d\n255\n" % (w, h) + bytes(c for p in pixels for c in p))


class ScreenCheck(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.t, True)

    def test_stats_and_diff_from_pixels(self):
        R, B = (255, 0, 0), (0, 0, 255)
        ppm(self.t / "a.ppm", 2, 2, [R, R, R, B])
        ppm(self.t / "b.ppm", 2, 2, [R, R, R, R])
        a, b = screencheck.read_ppm(self.t / "a.ppm"), screencheck.read_ppm(self.t / "b.ppm")
        s = screencheck.stats(*a)
        self.assertEqual((s["distinct"], s["nonblank"]), (2, 0.25))
        self.assertEqual(screencheck.stats(*b)["nonblank"], 0.0)
        self.assertEqual(screencheck.diff(a, b), (1, 0.25))

    def test_png_roundtrip_and_bad_input(self):
        ppm(self.t / "a.ppm", 2, 1, [(1, 2, 3), (4, 5, 6)])
        w, h, px = screencheck.read_ppm(self.t / "a.ppm")
        screencheck.write_png(self.t / "a.png", w, h, px)
        data = (self.t / "a.png").read_bytes()
        self.assertTrue(data.startswith(b"\x89PNG"))
        i = data.index(b"IDAT")
        n = struct.unpack(">I", data[i - 4:i])[0]
        self.assertEqual(zlib.decompress(data[i + 4:i + 4 + n]), b"\x00" + px)
        (self.t / "bad.ppm").write_bytes(b"P5\n1 1\n255\n\x00")
        self.assertEqual(screencheck.main([str(self.t / "bad.ppm")]), 2)


class Merge(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.t, True)
        shutil.copy(REPO / "milestones.addon.json", self.t / "milestones.addon.json")
        add_ids = {m["id"] for m in json.loads((REPO / "milestones.addon.json").read_text())["milestones"]}
        base = json.loads((REPO / "milestones.json").read_text())
        base["milestones"] = [m for m in base["milestones"] if m["id"] not in add_ids]  # work before or after merging
        (self.t / "milestones.json").write_text(json.dumps(base, indent=2) + "\n")

    def run_merge(self, *a):
        return subprocess.run([sys.executable, str(REPO / "tools" / "merge_milestones.py"), *a],
                              env=dict(os.environ, CEPHEUS_ROOT=str(self.t)), capture_output=True, text=True)

    def ids(self):
        return [m["id"] for m in json.loads((self.t / "milestones.json").read_text())["milestones"]]

    def test_dry_run_apply_idempotent(self):
        before = self.ids()
        self.assertEqual(self.run_merge().returncode, 0)
        self.assertEqual(self.ids(), before)
        self.assertEqual(self.run_merge("--apply").returncode, 0)
        after = self.ids()
        self.assertEqual(after[:len(before)], before)
        self.assertTrue({"D1", "D2", "D3", "F1", "M1", "N1", "A1", "G1"} <= set(after))
        self.assertTrue((self.t / "milestones.json.pre-addon.bak").exists())
        self.assertEqual(self.run_merge("--apply").returncode, 0)
        self.assertEqual(self.ids(), after)

    def test_unknown_dependency_rejected(self):
        add = json.loads((self.t / "milestones.addon.json").read_text())
        add["milestones"][0]["depends"] = ["NOPE"]
        (self.t / "milestones.addon.json").write_text(json.dumps(add))
        r = self.run_merge("--apply")
        self.assertEqual(r.returncode, 1)
        self.assertNotIn("G1", self.ids())


class Apply(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.t, True)
        for d in ("agent", "docs"):
            (self.t / d).mkdir()
        add_ids = {m["id"] for m in json.loads((REPO / "milestones.addon.json").read_text())["milestones"]}
        base = json.loads((REPO / "milestones.json").read_text())
        base["milestones"] = [m for m in base["milestones"] if m["id"] not in add_ids]
        (self.t / "milestones.json").write_text(json.dumps(base, indent=2) + "\n")
        shutil.copy(REPO / "milestones.addon.json", self.t / "milestones.addon.json")
        shutil.copy(REPO / "agent" / "AGENTS_ADDON.md", self.t / "agent" / "AGENTS_ADDON.md")
        shutil.copy(REPO / "agent" / "KICKOFF_ADDON.md", self.t / "agent" / "KICKOFF_ADDON.md")
        (self.t / "AGENTS.md").write_text("# AGENTS\n\n## 12. Report template\nkeep me\n")
        (self.t / "agent" / "KICKOFF.md").write_text("OLD KICKOFF\n")

    def run_apply(self, *a):
        return subprocess.run([sys.executable, str(REPO / "tools" / "apply_addon.py"), *a],
                              env=dict(os.environ, CEPHEUS_ROOT=str(self.t)), capture_output=True, text=True)

    def test_dry_run_changes_nothing_then_apply_is_idempotent(self):
        r = self.run_apply()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual((self.t / "agent" / "KICKOFF.md").read_text(), "OLD KICKOFF\n")
        self.assertNotIn("## 13.", (self.t / "AGENTS.md").read_text())
        r = self.run_apply("--apply")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        ag = (self.t / "AGENTS.md").read_text()
        self.assertTrue(ag.startswith("# AGENTS") and "keep me" in ag)
        self.assertEqual(ag.count("ADDON-SECTION:BEGIN"), 1)
        self.assertIn("super.img", ag)
        self.assertIn("ADDON-KICKOFF", (self.t / "agent" / "KICKOFF.md").read_text())
        self.assertEqual((self.t / "agent" / "KICKOFF.md.pre-addon.bak").read_text(), "OLD KICKOFF\n")
        self.assertTrue((self.t / ".milestones.sha256").exists())
        self.assertIn("| A1 |", (self.t / "docs" / "ROADMAP.md").read_text())
        self.assertEqual(self.run_apply("--apply").returncode, 0)
        ag2 = (self.t / "AGENTS.md").read_text()
        self.assertEqual(ag2.count("ADDON-SECTION:BEGIN"), 1)
        self.assertEqual((self.t / "agent" / "KICKOFF.md.pre-addon.bak").read_text(), "OLD KICKOFF\n")
        chk = subprocess.run([sys.executable, str(REPO / "tools" / "milestone.py"), "check"],
                             env=dict(os.environ, CEPHEUS_ROOT=str(self.t)), capture_output=True, text=True)
        self.assertEqual(chk.returncode, 0, chk.stdout + chk.stderr)


if __name__ == "__main__":
    unittest.main()
