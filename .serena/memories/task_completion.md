# CS2FOW completion and verification gates

- For a local low-risk change, run the smallest relevant native/Python/Studio check and inspect `git diff --check`.
- For binary, packaging, ABI, release, or shared runtime behavior changes, run the full platform pipeline:
  - Windows: `.scripts\build-windows.ps1`
  - Linux/SteamRT3: `.scripts\build-steamrt3.ps1` or `bash scripts/build-linux.sh` inside the pinned runtime.
- The full pipeline must cover bootstrap, AMBuild compile, native `cs2fow_tests`, `python -m unittest discover -v tests`, Studio checks (unless intentionally skipped for the Linux wrapper), platform import/ABI verification, and package ZIP creation/verification.
- Packaging validates required files, licenses, safe/unique ZIP entries, Linux executable modes, official-map BVH8/report metadata, and SHA-256 checksums. Do not claim package success without the generated archive and checksum evidence.
- Changes to CheckTransmit, gamedata, schema/private engine compatibility, or transmit lifecycle require live CS2 server validation in addition to automated tests; CI cannot reproduce every snapshot condition.
- Before declaring completion, report exact commands and observed results. Do not claim tests/builds passed when a required command could not run.
- Creating tags, release manifests/notes, public releases, or Bake Service deployments is a separate explicitly approved task.
