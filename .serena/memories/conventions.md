# CS2FOW coding and safety conventions

- Files are UTF-8 with LF endings and a final newline. C++ headers/sources use tabs (width 4); Python uses two spaces; follow `.editorconfig`.
- Match existing snake_case naming for files, functions, types, and variables. Constants use `k_` prefixes. Preserve public administrator command names beginning `cs2fow_`.
- C++ is C++20 with strict warnings; keep comments short and focused on safety constraints or non-obvious CS2 engine behavior.
- Preserve fail-open behavior at every boundary: unknown binary/gamedata, invalid map/BVH/report, stale snapshots/results, incomplete pose/capsules, deadline exhaustion, lifecycle mismatch, or missing paired transmit pointers must keep players visible.
- Keep engine reads on the game thread; workers receive copied values and never live engine pointers. CheckTransmit must remain allocation-free and non-blocking with no file I/O, process work, or BVH traversal.
- Only change a verified entity bit pair: set matching dont_transmit before clearing a set primary bit; never alter full-update snapshots or unrelated objects.
- Tests: native suites are named `*_tests.cpp` and registered in `tests/test_suites.h`/`tests/test_main.cpp`; Python tests are `test_*.py`. Assert expected values before actual values.
- Prefer small, behavior-preserving changes. Avoid magic numbers, preserve fixed-capacity/thread-ownership assumptions, and update docs/config/Studio alignment when a public behavior or sampling recipe changes.
- Treat `third_party/` as vendored; do not casually edit it and keep notices/licenses synchronized when packaging changes.
