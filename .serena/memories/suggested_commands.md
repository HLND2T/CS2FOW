# Commands normally run from the repository root (Windows host unless noted)

- Full Windows pipeline (bootstrap, configure/build, native tests, Python tests, Studio checks, import verification, ZIP): `.scripts\build-windows.ps1`.
- Windows host -> pinned Steam Runtime 3 Linux container: `.scripts\build-steamrt3.ps1` (requires Docker, or WSL with Docker).
- Native Linux/SteamRT3 pipeline: `bash scripts/build-linux.sh`; add `--install-tools` only when the container must install apt tools.
- SDK-independent Python packaging tests: `python -m unittest discover -v tests`.
- Visibility Studio checks: `python scripts/check_studio.py` (runtime alignment, BVH8, movement, smoke, HE, malformed-input checks).
- Package already-built artifacts selectively: `python package.py windows-x86_64`, `python package.py linux-x86_64`, or `python package.py official-maps`; no target builds all three and writes `packages/SHA256SUMS.txt`.
- Low-level configure/build (normally driven by `scripts/build.py`): run `python scripts/bootstrap.py --platform <windows|linux>`, configure from a platform build directory with `configure.py --hl2sdk-root ... --hl2sdk-manifests ... --mms-path ...`, then invoke AMBuild's `python -c "from ambuild2.run import cli_run; cli_run()"`.
- On Windows use PowerShell path syntax for wrappers; run Python commands from the checkout root. Do not substitute unpinned dependency sources.
