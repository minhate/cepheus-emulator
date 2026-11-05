# Setup GitHub (repo baru, private)

```bash
cd cepheus-emulator
git init -b main
git add -A && git status          # cek: tidak ada .env / firmware / build
git commit -m "chore: initial scaffold"
gh repo create cepheus-emulator --private --source=. --push    # atau buat manual di web
git checkout -b m/R0-host-doctor
```

Model branch: `main` selalu hijau (CI). Satu branch per milestone: `m/<ID>-<slug>`. Merge lewat PR setelah `make ci` hijau.

Catatan:
- Branch protection di repo private butuh plan berbayar (GitHub Pro/Team). Kalau free, andalkan CI + disiplin PR.
- CI (`.github/workflows/ci.yml`) hanya menjalankan `make ci` (mockscan, cek status/evidence, unit test tools). CI tidak butuh firmware dan tidak boot QEMU.
- `.env` dan `firmware/` di-gitignore. Jangan pernah di-commit.
