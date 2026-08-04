# Repository Guidelines

## Project Structure & Module Organization

Core C++20 code lives under `src/`: `plugin/` handles Metamod integration and runtime filtering, `core/` holds reusable geometry and file logic, and `baker/` builds map data. Native tests are in `tests/`. Runtime files live in `cfg/`, `gamedata/`, and `data/maps/`; build automation is in `scripts/`; and `tools/visibility_point_editor/` contains Visibility Studio. Treat `third_party/` as vendored and keep notices synchronized.

## Build, Test, and Development Commands

- `.\scripts\build-windows.ps1` bootstraps dependencies, builds, tests, verifies imports, and writes a ZIP to `packages/`.
- `.\scripts\build-steamrt3.ps1` performs the Linux build from Windows using the pinned Steam Runtime 3 container.
- `bash scripts/build-linux.sh` runs the equivalent native Linux pipeline.
- `python -m unittest discover -v tests` runs SDK-independent packaging tests.
- `python scripts/check_studio.py` validates Studio alignment, BVH8, movement, smoke, HE, and malformed inputs.

Build dependencies are defined in `build-dependencies.json`; do not substitute unpinned SDK or tool versions without an intentional compatibility update.

## Coding Style & Naming Conventions

Follow `.editorconfig`: UTF-8, LF endings, a final newline, tabs of width 4 for `.cpp`/`.h`, and two spaces for Python. C++ is compiled as C++20 with strict warnings. Match existing snake_case files, functions, types, and variables; use `k_` prefixes for constants and preserve public `cs2fow_*` command names. Keep comments focused on safety constraints or non-obvious engine behavior.

## Testing Guidelines

Name native test files `*_tests.cpp` and Python tests `test_*.py`. Register native suites through `tests/test_suites.h` and `tests/test_main.cpp`. Cover success, boundaries, malformed input, lifecycle, and fail-open paths. Run a full platform build for binary, packaging, ABI, or release changes. Transmit changes also require live-server validation because CI cannot reproduce every CS2 snapshot condition.

## Commit & Pull Request Guidelines

History uses short, imperative, title-case subjects such as `Fix SteamRT3 build from literal paths`. Keep commits focused. Pull requests should explain behavior and safety impact, list exact test commands and results, identify platforms, and link issues. Include logs and a clip or screenshots for visibility, pop-in, or Studio UI changes. Release tags, manifests, notes, and deployments require separate approval.

## Architecture & Safety Notes

Preserve the fail-open policy: uncertain, invalid, changed, or stale data must leave players visible. Workers consume copied data, never live engine pointers. Do not add file I/O, BVH traversal, heap allocation, or blocking work to `CheckTransmit`.
