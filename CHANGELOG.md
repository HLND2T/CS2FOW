# Changelog

## 0.3.6

- Updated the strict Windows and Linux gamedata fingerprints and private addresses for CS2 build `24537688` (`1.41.7.4`). Unknown binaries remain fail-open.
- Added `cs2fow_check_update` for an immediate update check, including clear results when the server is current, an update is already prepared, or automatic updates are disabled.
- Freshly baked and validated all 23 official maps from build `24537688`. Five changed map sources (`cs_shelter`, `de_boulder`, `de_cache`, `de_debris`, and `de_fachwerk`) now have new matching geometry.

## 0.3.5

- Added verified automatic updates, enabled by default with `cs2fow_auto_update 1`. CS2FOW checks GitHub's stable releases, requires an exact Windows/Linux package and release manifest, verifies both SHA-256 digests and the current CS2 server-binary fingerprint, and stages the complete platform package outside the game loop.
- Install prepared updates only on the next full server restart through a separate bootstrap binary. Preserve known configuration values and all map bakes, keep backups of the previous configuration and plugin binary, restore Linux tool permissions, and leave the current installation running when any check or filesystem operation fails.

## 0.3.4

- Ship `mp_playerid 1` by default so CS2 does not display an enemy name over that player's stale last-transmitted position while CS2FOW is hiding them.
- Report whether target IDs are safe in `cs2fow_status`, and make `cs2fow_check_config` explain how to correct an unrestricted `mp_playerid` setting.
- Keep visibility decisions, transmitted entity groups, gunshot behavior, performance settings, strict CS2 compatibility checks, and fail-open behavior unchanged from 0.3.3.

## 0.3.3

- Revalidated every private Windows and Linux gamedata value against the current public CS2 `1.41.7.3` binaries. The strict gate now accepts both exact verified Linux files Valve distributed for that build, whose required functions and layouts are identical, while continuing to reject every unknown fingerprint.
- Centralized every CS2FOW setting behind a committed runtime snapshot. `cs2fow.cfg` now loads transactionally, retains the previous known-good settings until its final marker, rolls back incomplete loads after five seconds, and defers worker-thread changes until the next map.
- Added `cs2fow_help`, `cs2fow_reload`, `cs2fow_check_config`, and `cs2fow_metrics`. `cs2fow_status` is now a short operator dashboard with explicit health, configuration, map, protection, player/pair, p99, snapshot-age, and next-action information; the former detailed counters remain in `cs2fow_metrics`.
- Extracted strict CS2/OS/AVX/gamedata/schema capability checks into a structured compatibility component without weakening the exact server-binary gate or fail-open behavior.
- Made Visibility Studio runtime-only: Preview and Play now use the real nineteen animated capsule bindings, eight padded AABB corners, muzzle, viewing origins, smoke/HE rules, and native LOS order. The legacy fifteen-point editor, preset, import/export, rays, and static fallback were removed.
- Centralized pinned Metamod, HL2SDK, AMBuild, VRF, and Steam Runtime 3 inputs in one dependency manifest. Shared Windows/Linux/SteamRT3 scripts now perform bootstrap, tests, ABI/import checks, and packaging for GitHub CI, GitLab CI, and local builds.
- Increased the default reveal hold to 1000 ms to cover brief LOS gaps such as the CT-to-T angle through Dust II mid doors; operators can still tune it with `cs2fow_visibility_hold_ms`.
- Made Valve's full nineteen-capsule silhouette the primary LOS decision. The eight padded AABB corners and weapon muzzle are now forgiving fallbacks when the capsule silhouette is fully blocked; the redundant chest probe was removed.
- Preserved runtime ConVar names, package layout, and strict fail-open compatibility enforcement.

## 0.3.2

- Updated strict Windows and Linux gamedata for CS2 build `24442510` (`1.41.7.3`).
- Rebuilt against the latest HL2SDK `159cddd`; Metamod:Source remains current at `2667e8e`.

## 0.3.1

- Restored eight padded AABB corner checks as a fast, forgiving visibility fallback before the full animated-capsule test. The runtime now tries reveal-hold reuse, chest, AABB corners, muzzle, and finally capsules, so an obvious visible point avoids the more expensive capsule pass.
- Increased the default reveal hold from 16 ms to 47 ms (about three 64-tick server ticks) to reduce edge flicker and short pop-outs.
- Added the AABB corner samples to the temporary in-game LOS debug view and aligned Visibility Studio with the runtime's current LOS order, padding, hold, cache, validation, and pose behavior.
- Rebuilt against Metamod:Source `2667e8e` and HL2SDK `c9e9477` while retaining Steam Runtime 3 compatibility.

## 0.3.0

- Replaced the fifteen hand-tuned runtime LOS dots and eight AABB corners with Valve's nineteen live animated hitbox capsules. Visibility now evaluates the capsule silhouette through a bounded CPU depth buffer, preserves the muzzle/smoke/HE rules, and fails open on invalid capture, uncertainty, or a 75 ms worker budget.
- Added a configurable 1-4-thread visibility pool (two by default), fair budget rotation, a safe visible-ray prepass, reveal-hold reuse, and a larger verified occluder cache that compacts proven blocker sets for 32-player servers. Status now separates wall latency from aggregate worker activity and reports recent tail latency, prepass, hold, and cache behavior.

## 0.2.6-preview

- Removed the private CS2 debug-overlay calls that could corrupt the client HUD and spam missing-texture errors.

## 0.2.5-preview

- Made all fifteen tuned body samples follow each player's current animation. If CS2 cannot provide a safe pose, visibility falls back to the existing fixed samples.
- Added a separate `bones` line to `cs2fow_status` for the game-thread cost of capturing animated body points and the current animated/fallback player counts.
- Made visibility and automatic-baker startup fail open when their worker threads cannot be created, and made large BVH8 loads cancellable so map changes and shutdown do not wait on obsolete work.
- Reduced repeated runtime work by calculating each player's target samples once per cycle, skipping smoke capture when disabled, rejecting smoke volumes outside a ray early, listing each VPK once, and using a table-based streaming CRC32.
- Retried unavailable bone lookups, checked POSIX process setup failures, preserved native Windows paths and empty process arguments, and limited AVX code generation to the ray-traversal functions that require it.
- Tightened VPK source-path parsing, retained useful direct/nested lookup errors, and preserved native path encoding for temporary files and Unicode GLB/BVH8 paths.
- Expanded Visibility Studio from a point editor into a local 64 Hz first-person runtime simulator with direct BVH8 loading, map collision, navigation, bots, weapons, grenades, smoke, HE and bullet clearing, sounds, particles, and Real/Debug visibility.
- Made Studio use the runtime's animated body samples, AABB corners, muzzle point, ping-scaled viewing origins, wall decisions, smoke decisions, and ray counts; added interpolation for actors, grenades, LOS points, skeletons, AABBs, and debug geometry.
- Added 16 Hz LOS/BVH diagnostics, consistent depth-independent debug overlays, per-bot visibility-gate counts, and a 33% orange BVH fill with a 16% black outline.
- Added a pinned local Studio asset pipeline, compact player-animation exports, CS2 navigation export, optional baker surface sidecars, and automated checks for BVH8 traversal, movement, collision, smoke, HE, navigation, and runtime-layout consistency.
- Removed generated .NET `bin`/`obj` output from version control, ignored future generated output, and pinned Studio's Node dependencies.
- Pointed project and release downloads to the temporary GitLab home.

## 0.2.4-preview

- Rebuilt against the current Metamod:Source and HL2SDK so CS2FOW commands and settings register correctly after the July 17 CS2 tooling update.
- Tightened the upper eye origin from 24 to 16 units and changed ping preload to a 48-128 unit table: every 25 ms adds 10 units until the 200 ms cap. AABB side/top padding remains 8 units.
- Automatically treat every other living player as an enemy when `mp_teammates_are_enemies 1` is active.
- Reduced the lifecycle fail-open window from 3 seconds to 1 second and removed the separate 1.5-second visual warmup while preserving the complete-group baseline check.
- Updated Visibility Studio with a second SAS model 256 units away and the same stationary origins, target samples, and ray count used by the runtime.
- Replaced velocity/lookahead prediction with a ping-scaled W/S/diagonal intention origin, added a permanent feet origin, and reduced rays by 37.5% to 62.5% per player pair (from 192-384 to 120-144).

## 0.2.3-preview

- Verified that CS2 build `24248951` keeps the same private runtime layout and updated the strict Windows/Linux server fingerprints.
- Rebaked `cs_shelter`, `de_boulder`, `de_eldorado`, and `de_fachwerk` after their mounted map sources changed in the same update.

## 0.2.2-preview

- Verified the private runtime layout and updated the strict Windows/Linux fingerprints for CS2 build `24209309`.
- Removed the obsolete Valve string-token database import dropped by that update.
- Rewrote the README for ordinary server owners and added new visibility, smoke, HE, and map demonstrations.

## 0.2.1-preview

- Stopped generic owner/effect links from pulling independent gameplay entities into a hidden player's visual group.
- Kept planted C4, dropped objectives, grenade projectiles, infernos, sounds, and unknown entities independent so player culling cannot hide core gameplay state.
- Kept explicit player visuals together: pawn, known carried weapons (including carried C4), wearables, and a currently carried hostage prop.
- Simplified `cs2fow_entity` evidence to direct visual-group membership.

## 0.2.0-preview

- Added default-on smoke occlusion from CS2's live voxel grid, with copied worker data and smoke-only fail-open behavior.
- Added wall-safe, configurable 2.5-second visibility channels through smoke disturbed by HE grenades.
- Fixed HE event discovery, post-initialization listener registration, and detonation-position reading without making ordinary smoke depend on HE support.
- Prevented an old HE event from clearing a smoke that detonated later by ordering both on CS2 game time.
- Matched visible smoke timing more closely by delaying initial occlusion and revealing fading smoke 0.5 seconds earlier.
- Added optional teammate visibility filtering with the same wall, smoke, prediction, and full-group rules used for enemies.
- Reorganized the runtime into map/game-state, worker, transmit, and automatic-baker responsibilities without intentionally changing proven visibility behavior.
- Updated CheckTransmit hiding to set CS2's matching `dont_transmit` bit before clearing a set primary transmit bit; missing lists fail open, while full updates and the other mask storage remain untouched.
- Bundled `sv_enable_donttransmit 0` as the compatibility default and automatically execute `cs2fow.cfg` after convar registration and at every map start; paired-list handling also supports mode `1`.
- Let visible enemies return through ordinary snapshots instead of waiting for CS2 to schedule a full update.
- Tuned movement preload to a 75 ms base plus 1.5 times recipient RTT, capped at 375 ms and 96 units per player, with a smooth 75-100 speed ramp.
- Made left/right shoulder origins scale from 24 to 128 units with recipient RTT through public tuning controls.
- Kept safe movement up to baked walls, replaced merged target boxes with separate current/future boxes, and corrected stale-result age to use snapshot capture time.
- Added fixed-size `cs2fow_entity` evidence for entity bits actually hidden by CS2FOW, including direct and owner/effect-linked membership.
- Hardened player lifecycles, visual-group identity, linked entities, stale results, and fail-open resets.
- Bound private gamedata to verified Windows and Linux server binaries and rejected unsafe player numbers before ray casting.
- Added snapshot-capture and CheckTransmit timings, full networked-edict linked-visual coverage, and accurate active-HE status wording.
- Captured bounded VRF and automatic-baker error output so failures include their useful final messages.
- Made Linux bake cancellation and timeout terminate and reap the complete baker/VRF process tree.
- Added validated BVH8 version 3 files with rooted-tree, reachability, depth, triangle, and streaming CRC checks plus verified atomic replacement; older or structurally invalid bakes are rejected.
- Restricted VPK version 2 embedded entries to the declared file-data section instead of accepting undeclared footer bytes.
- Added a machine-readable `--inspect-bvh8` command and require every official-map bake/report pair to match before packaging.
- Kept checksums from sequential platform packaging, required all three final archives, and bundled exact cgltf, ValveResourceFormat, native-library, and .NET redistribution notices.
- Prevented the LOS editor from exporting blank/duplicate names, invalid coordinates, or zero points.
- Moved all Workshop VPK discovery and extraction into the C++ baker, including the public `--list-maps` command.
- Added held-weapon muzzle sampling alongside body and axis-aligned bounding box target points.
- Split and expanded map/BVH and visibility/transmit tests, package verification, and the line-of-sight point editor checks.
- Added a plain-language code tour and corrected operator documentation.

## 0.1.2-preview

- Further hardened CheckTransmit player lifecycle checks.
- Hide pawn, current weapons, wearables, and carried hostage prop as one group.
- Preserve fail-open behavior when live player state is uncertain.

## 0.1.1-preview

- Hardened CheckTransmit against invalid indexes, stale player state, and stale weapon handles.
- Built Linux packages against SteamRT3 Sniper for CS2 server compatibility.
- Added CI checks for glibc, libstdc++, and C++ ABI requirements.

## 0.1.0-preview

First public preview of CS2FOW.

- Native Metamod plugin for server-side CS2 visibility culling.
- Offline and automatic map baker for official, custom, and Workshop maps.
- BVH8 runtime map data with AVX traversal.
- Smooth reveal envelope to reduce corner pop-in.
- Windows x86_64 and Linux x86_64 packages.
- Optional official map prebakes as a separate release asset.
