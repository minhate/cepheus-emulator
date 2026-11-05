# cepheus-emulator

Emulator **native** Xiaomi Mi 9 (`cepheus`, SM8150): custom machine QEMU `cepheus` yang boot **kernel + DTB stock** dan userspace Android stock dari image asli. Hardware dimodelkan native; hanya TrustZone, modem/DSP, GPU, kamera, sensor/biometrik, dan boot ROM chain yang boleh di-stub (lihat `AGENTS.md`).

## Mulai (dari nol)
1. Extract zip ini. Taruh `.env` lama lu di root (di-gitignore). Contoh: `.env.example`.
2. Taruh arsip fastboot ROM cepheus lu di `firmware/input/`.
3. Setup git/GitHub: ikuti `docs/SETUP_GITHUB.md`.
4. Buka AI agent di folder ini, paste isi `agent/KICKOFF.md`.
5. Agent mengerjakan milestone satu per satu. Lu yang menyetujui lanjut ke berikutnya.

## Cara kerja anti-mock
- `milestones.json` (spec, dikunci hash) + `STATUS.json` (status). Status `VERIFIED` hanya bisa lewat `tools/milestone.py verify`, yang mengecek bukti.
- Bukti = log mentah dari `tools/capture.py` (positif + negatif yang sengaja dirusak), dicek pola, hash, dan argv (harus `cepheus`, bukan `virt`).
- `tools/mockscan.py` menolak echo "VERIFIED", output hardcoded, class Mock/Fake/Simulator, dan stub di luar allowlist/registry.
- `make ci` menjalankan semuanya (juga di GitHub Actions).

## Perintah
`make doctor` · `make status` · `make ci` · `make qemu` (butuh `CEPHEUS_ALLOW_NETWORK=1`) · `make roadmap`

## Milestone DRAFT (R6, R10–R17)
Pola buktinya belum final karena butuh output asli dari kernel/userspace. Agent menulis usulan di `docs/PROPOSALS.md`, lu review, lalu `python3 tools/milestone.py relock --yes` (hanya lu).
