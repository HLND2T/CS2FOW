# CS2FOW project map and invariants

- Purpose: server-side Counter-Strike 2 anti-wallhack Metamod plugin; hides only verified invisible player visual groups (pawn, carried weapons, wearables, carried hostage) from outgoing updates.
- Safety invariant: fail open. Missing, invalid, changed, stale, uncertain, timed-out, or incompatible data leaves entities visible/sent.
- Source map:
  - `src/plugin/`: plugin lifecycle/map callbacks (`plugin.cpp`), settings and transactional config (`settings.*`), live game-state capture (`game_state.cpp`), compatibility/gamedata checks (`runtime_compatibility.*`), background worker (`visibility_worker.*`), CheckTransmit filtering and evidence (`transmit.cpp`), automatic baking, updater.
  - `src/core/`: VPK/map-source validation, BVH8 traversal/format/building, visibility sampling and capsule/smoke occlusion, subprocess helpers.
  - `src/baker/`: command-line map baking and physics GLB import.
  - `tests/`: native assert-based suites; `tests/test_package.py`: packaging/policy tests.
  - `tools/visibility_point_editor/`: runtime-alignment Studio checks; `cfg/`, `gamedata/`, `data/maps/`: shipped runtime inputs.
- Runtime ownership:
  - Game thread may read live CS2 objects and copies pointer-free snapshots.
  - Visibility worker consumes copied data only, publishes immutable results, and owns BVH reads while the map worker is active.
  - CheckTransmit only validates current lifecycles and changes paired primary/dont_transmit bits; no BVH traversal, file/process I/O, blocking, or heap allocation in the hook.
  - Full-update snapshots and unsafe/missing network-list pointers are never modified.
- Build inputs are pinned; see `mem:tech_stack`. Common commands are in `mem:suggested_commands`; style/safety rules in `mem:conventions`; completion gates in `mem:task_completion`.
