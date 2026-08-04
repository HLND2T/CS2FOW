<div align="center">

<img src="docs/cs2fow-logo.png" width="760" alt="CS2FOW">

### Server-side anti-wallhack for Counter-Strike 2 community servers

[![Protection](https://img.shields.io/badge/protection-Walls%20%7C%20Smoke-6f42c1?style=for-the-badge)](#the-protection-boundary)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-5c7cfa?style=for-the-badge)](#quickstart)
[![License](https://img.shields.io/badge/license-MIT-2ea44f?style=for-the-badge)](LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy_Me_a_Coffee-Support_Development-FFDD00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=000000)](https://buymeacoffee.com/karola3vax)

**A wallhack cannot draw a player your server never sent.**

CS2FOW stops your server from sending an enemy's live position when walls or smoke completely hide them. It runs entirely on the server, players install nothing, and uncertain visibility stays visible.

[Watch it work](#showcase) · [Install](#quickstart) · [Learn how it works](#how-it-works) · [Pair it with CS2AC](#cs2fow-and-cs2ac)

</div>

## Showcase

Every map gets a lightweight 3D copy of its solid walls. CS2FOW uses that copy to check who can actually see whom.

<table>
<tr>
<td width="50%" align="center">
<img src="docs/ancient.gif" width="100%" alt="CS2FOW hiding players behind solid map geometry on Ancient"><br>
<strong>Ancient &mdash; A site</strong>
</td>
<td width="50%" align="center">
<img src="docs/smokeandhegrenade.gif" width="100%" alt="CS2FOW hiding players behind smoke and revealing them through an HE-cleared opening"><br>
<strong>Smoke &mdash; HE-cleared opening</strong>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/cache.gif" width="100%" alt="CS2FOW operating around the Cache A site"><br>
<strong>Cache &mdash; A site</strong>
</td>
<td width="50%" align="center">
<img src="docs/dust2b.gif" width="100%" alt="CS2FOW operating around the Dust II B site"><br>
<strong>Dust II — B site</strong>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/dust2long.gif" width="100%" alt="CS2FOW operating across Dust II long sightlines"><br>
<strong>Dust II — Long</strong>
</td>
<td width="50%" align="center">
<img src="docs/mirageaside.gif" width="100%" alt="CS2FOW operating around the Mirage A site"><br>
<strong>Mirage &mdash; A site</strong>
</td>
</tr>
</table>

### CS2FOW and CS2AC

<div align="center">

<a href="https://github.com/karola3vax/CS2AC">
<img src="docs/cs2ac-logo.png" width="760" alt="CS2AC">
</a>

**CS2FOW hides unseen positions. CS2AC detects cheating behavior.**

<sub>They solve different problems, run entirely on the server, and can protect the same CS2 community server together.</sub>

</div>

## Quickstart

You need a Windows x64 or Linux x64 CS2 dedicated server running [Metamod:Source](https://www.sourcemm.net/) 2.x, the plugin loader that lets CS2 load server extensions. Your CPU must support AVX, a common instruction set CS2FOW uses for fast geometry calculations.

1. Open this repository's **Releases** tab and choose the matching Windows or Linux package.
2. Extract it directly into the server's `game/csgo` folder without rearranging anything. The package begins with the `addons`, `cfg`, and `tools` folders.
3. Start the server and load a map.
4. Run `meta list`, then `cs2fow_status`.

That is it. Players install nothing.

The first time you load a map, `cs2fow_status` may say that an automatic bake is running. A bake is the one-time step that turns the map's solid walls into fast visibility data. Everyone stays visible until it finishes and passes its checks. The optional official-maps ZIP includes ready-made data, so those maps skip this first wait.

CS2FOW uses a compatibility file called gamedata to locate the exact parts of the CS2 server program it needs. Before using it, the plugin confirms the program by file size and CRC32, a checksum that acts like a digital fingerprint. If Valve ships an unknown update, CS2FOW stays off until matching gamedata is installed rather than guessing inside server memory.

CS2FOW checks GitHub's stable releases for updates after startup and every six hours. It downloads only the package for the server's operating system, verifies GitHub's SHA-256 checksum and CS2FOW's release manifest, and refuses packages that do not explicitly support the server's current binary fingerprint. A verified update is prepared in the background and installed on the next full server restart; the running installation is never hot-swapped. Set `cs2fow_auto_update 0` and run `cs2fow_reload` to disable future checks. An update prepared before this was disabled may still install on the next restart.

## The protection boundary

CS2FOW keeps its hands off as much of the game as possible. It hides only this small visual group:

- the player pawn, which is the in-game entity representing a living player;
- active, last, and carried weapons, including the carried C4 bomb;
- wearable items, such as gloves and equipment attached to the model;
- the visible hostage model currently carried by the player.

Everything that matters on its own stays on its own. A planted C4 bomb, dropped objective, dropped weapon, flying grenade, burning Molotov fire, sound, or unknown game object does not disappear just because the player who once owned it is hidden. Your server still controls movement, collisions, whether bullets hit, damage through walls, and game rules exactly as before.

CS2FOW does not filter HLTV, Valve's built-in match broadcast system, or spectators, dead players, and your own player. Teammates stay visible by default. Free-for-all (FFA) mode is detected automatically through `mp_teammates_are_enemies`; when it is `1`, every other living player is treated as an enemy. You can also set `cs2fow_filter_teammates 1` to apply the same check to teammates in normal team modes, which can remove their on-screen markers and radar information while hidden.

Live smoke can block those imaginary sight lines too. By default, a high-explosive (HE) grenade opens a viewing channel 100 game units wide through affected smoke for 2.5 seconds, but only if the smoke was already there when the grenade exploded. A wall still wins, and another overlapping smoke can still block the view.

## How it works

1. **Load the map:** CS2FOW finds the installed map's VPK archive, the game file containing the map data, and reads its physics data, which describes the collision shapes of solid surfaces.
2. **Bake the walls:** the baker turns thousands of collision triangles, the small flat pieces that describe solid surfaces, into a compact search index called a BVH8. This lets CS2FOW skip most walls and quickly find the few that could block a view.
3. **Take a picture:** on CS2's main game thread, where live game state is safe to read, CS2FOW copies each player's current pose. CS2 stores that pose as bones, which position the animated model, and nineteen hitbox capsules, which are rounded cylinders fitted around the body. CS2FOW copies those capsules together with the player's position, size, movement buttons, view direction, ping, and held weapon. This frozen copy is called a snapshot. If it is incomplete or untrustworthy, the player stays visible.
4. **Test the whole body:** after honoring any short hold that keeps a recently seen player visible, the background worker compares the three-dimensional outline of all nineteen capsules with the baked walls and live smoke.
5. **Try forgiving fallbacks:** only when the full body is blocked, the worker checks eight padded corners of the player's axis-aligned bounding box (AABB), the simple rectangular box around the player, and then the held weapon's muzzle.
6. **Choose visible or hidden:** any clear part of the body or fallback point shows the whole player. Missing data, uncertainty smaller than one screen pixel, or a check that runs out of its allowed time also shows the player rather than risking an incorrect hide.
7. **Control the outgoing update:** before CS2 sends its next network update, a function named `CheckTransmit` builds the list of game entities—objects such as players and weapons—each player should receive. CS2FOW marks every verified hidden entity as `dont_transmit`, meaning "do not send," in both of Valve's matching network lists.

The worker gets a copy of the numbers, never live CS2 objects. In other words, it reads a photograph instead of reaching back into the moving game.

### Baked map geometry

<table>
<tr>
<td width="52%">
<p>The baker strips the map's fixed collision geometry down to the walls CS2FOW needs for sight checks. Before using the result, the plugin validates the BVH8 file, the source map's fingerprint (file size and CRC checksum), and the bake report.</p>
<p>Your server can bake installed maps automatically. You can also prepare public Workshop maps through the <a href="https://cs2fow-bake-service.onrender.com/">CS2FOW Map Baker</a>.</p>
</td>
<td width="48%" align="center">
<img src="docs/scan_cbbl.png" width="100%" alt="Static cobblestone collision mesh used by CS2FOW">
</td>
</tr>
</table>

## Configuration

The plugin runs `cfg/cs2fow.cfg` when it loads and again before the background visibility worker starts for each map. Configuration is all-or-nothing: the previous known-good settings remain active until the final `cs2fow_config_loaded` line confirms that the whole file ran. An interrupted file, missing confirmation line, or five-second timeout restores the previous settings; an initial failure keeps the built-in defaults. Do not remove or move that final line.

Use `cs2fow_status` as the short dashboard. It reports the health state, configuration, map, enabled protection, player count, number of viewer-to-target pairs being checked, the time taken by the slowest 1% of recent worker checks (p99), snapshot age, and one next action when intervention is needed. Use `cs2fow_metrics` for the complete performance and activity counters.

```text
cs2fow_help           list administrator commands
cs2fow_status         show concise health and protection state
cs2fow_metrics        show complete performance and activity counters
cs2fow_reload         safely reload all settings or keep the previous ones
cs2fow_check_config   explain settings that deserve attention
cs2fow_check_update   check for a new version without waiting
```

Direct console changes still work immediately. A successful reload applies changes that are safe while a map is running; `cs2fow_worker_threads` starts on the next map, and status shows both the configured and currently running worker counts until then. Out of the box, wall and smoke filtering and automatic updates are on, while teammate filtering is off. Valve's `sv_enable_donttransmit` setting controls how CS2 marks network entities that should not be sent; CS2FOW uses the safer compatibility mode `0` by default and also supports mode `1`.

If you need to see exactly which game objects CS2FOW removed from outgoing network updates:

```text
cs2fow_debug 1              start silent evidence collection
cs2fow_entity               list buffered records, newest first
cs2fow_entity <index>       show records for one game entity index
cs2fow_entity clear         clear the evidence buffer
```

The debug buffer is a small history kept in server memory. It records only game entities CS2FOW actually removed from an outgoing network update. Turning debug off stops collecting new evidence, but keeps what is already there until a reset or you clear it.

<details>
<summary><strong>Complete configuration reference</strong></summary>

| Setting | Default | Meaning |
| --- | ---: | --- |
| `sv_enable_donttransmit` | `0` | Choose how CS2 marks network entities that should not be sent. CS2FOW supports both modes and defaults to the safer compatibility mode. |
| `mp_playerid` | `1` | Show target IDs for teammates only, preventing enemy names from appearing over a hidden player's stale client position. |
| `cs2fow_auto_update` | `1` | Download verified compatible stable updates from GitHub Releases and install them on the next full server restart. |
| `cs2fow_enable` | `1` | Turn filtering on whenever all required data passes its safety checks. |
| `cs2fow_smoke_occlusion` | `1` | Let live smoke block sight. If CS2FOW cannot safely read the smoke data, smoke steps aside while wall protection keeps working. |
| `cs2fow_he_clear_radius_units` | `100` | Set how wide an HE-opened viewing channel is. Use `0` to turn HE clearing off. |
| `cs2fow_he_clear_seconds` | `2.5` | Set how long an HE-opened viewing channel lasts. Use `0` to turn HE clearing off. |
| `cs2fow_filter_teammates` | `0` | Give living teammates the same visibility checks as enemies. FFA mode is detected automatically. |
| `cs2fow_update_interval_ms` | `1` | Wait at least this many milliseconds before sending another picture of the players to the worker. |
| `cs2fow_worker_threads` | `2` | Background visibility workers, from 1 to 4. Changes apply the next time a map loads. |
| `cs2fow_shoulder_base_units` | `48` | Start the left/right shoulder and movement-intention points this far from the player's eye. |
| `cs2fow_shoulder_rtt_scale` | `0.4` | Add this many units per millisecond of round-trip time (RTT), commonly called ping, updated in 25 ms steps. |
| `cs2fow_max_shoulder_units` | `128` | Never push those network-delay-adjusted viewing points farther out than this. |
| `cs2fow_visibility_hold_ms` | `1000` | Once a player becomes visible, keep them visible for about one second to cover brief line-of-sight gaps and prevent flicker. |
| `cs2fow_debug` | `0` | Save evidence about game objects CS2FOW actually removed from outgoing updates. It does not spam the console. |
| `cs2fow_debug_los_player` | `0` | Temporarily draw one player's line-of-sight (LOS) checks: rounded body capsules, weapon muzzle, and rectangular AABB corners. Player slots are numbered from `1`. Keep `0` during normal play. |

If you are keeping an older custom config, copy the commented `0.3.5` file and reapply your values. The internal `cs2fow_config_loaded` command must be the last command or CS2FOW will reject and roll back the file. Automatic updates merge known settings into the new commented layout and keep one pre-update configuration backup.

Automatic baking needs permission to write into `addons/cs2fow/data/maps`. On Linux, the packaged baker and ValveResourceFormat (VRF), the tool that reads map files, must also remain executable.

</details>

## FAQ

<details>
<summary><strong>What is CS2FOW?</strong></summary>

CS2FOW is an anti-wallhack plugin for Counter-Strike 2 community servers. If walls or live smoke completely hide a living player, your server can stop sending that player's live visuals to the opponent who cannot see them.

It is not a filter drawn over the screen. Everything happens on your server.

</details>

<details>
<summary><strong>Does it work in Premier or Valve matchmaking?</strong></summary>

No. You need a community or dedicated server running Metamod:Source, the plugin loader used by CS2 community servers. Only Valve could add something similar to official matchmaking.

</details>

<details>
<summary><strong>Do players install anything or risk a Valve Anti-Cheat (VAC) ban?</strong></summary>

Nothing. Players join your server like normal. CS2FOW does not modify, inject into, or even run inside their CS2 client.

</details>

<details>
<summary><strong>Can a cheat bypass it?</strong></summary>

A cheat cannot read an exact live enemy position if your server never included it in the network update. It can still listen for sounds, use teammate information, remember the last known position, or guess a common place to fire before seeing someone. CS2FOW cuts off the main source used by wallhacks; it does not make every kind of cheating impossible.

</details>

<details>
<summary><strong>What exactly gets hidden?</strong></summary>

CS2FOW hides only the known visuals that travel with a living player: the player model, carried weapons, wearables, and a hostage they are currently carrying. Anything unknown or independent stays visible.

You always receive yourself, dead players, spectators, and HLTV, Valve's built-in match broadcast system. Teammates also stay visible by default, but you can choose to apply the same visibility check to living teammates.

</details>

<details>
<summary><strong>Can players still wallbang a hidden enemy?</strong></summary>

Yes. Hidden does not mean deleted. The player is still fully present on your server, so movement, hit registration, bullet penetration, damage, and game rules keep working normally.

</details>

<details>
<summary><strong>Does it block radar cheats or sound ESP?</strong></summary>

It helps against radar cheats that need live enemy positions. Sound ESP is a cheat that turns audible clues into extra warnings or displays; CS2FOW does not silence footsteps or gunshots. It also does not hide bomb information, remove teammate knowledge, or erase every other clue the game provides.

</details>

<details>
<summary><strong>What about smokes, doors, breakables, and moving props?</strong></summary>

Smoke blocks sight too. CS2FOW copies the game's live smoke shape, which is stored as a 3D grid of tiny boxes called voxels. That lets it follow changing edges, overlapping smokes, growth, fading, and holes opened by grenades.

Doors, breakable objects, and moving props do not block CS2FOW yet. The baked map is a frozen copy of solid map geometry, so it only knows about walls that stay put.

</details>

<details>
<summary><strong>How does it avoid enemies appearing too late around corners?</strong></summary>

CS2FOW first checks Valve's nineteen animated hitbox capsules, rounded cylinders fitted around the player's body, as one complete three-dimensional shape. If that full shape is blocked, eight padded corners around the player's rectangular collision box and the held weapon's muzzle provide forgiving fallbacks. It looks from your eye, shoulders, above your eye, and feet; when you hold W or S, or move diagonally, one extra point follows that direction. The shoulder and movement points reach farther at higher ping, stop at baked walls, and a short visibility hold prevents flicker.

As soon as the background worker finds a clear view again, CS2FOW lets the player's next normal update through.

</details>

<details>
<summary><strong>Does it slow the server by checking every wall during every update?</strong></summary>

No. The baker turns the map's walls into a small search index called a BVH8, which lets CS2FOW skip most walls and quickly find possible blockers. A background worker draws imaginary sight lines through that data. When CS2 decides what to send over the network, its `CheckTransmit` function only picks up the finished visible-or-hidden answer.

</details>

<details>
<summary><strong>Do custom and Workshop maps work?</strong></summary>

Yes, as long as the map contains usable physics data. Your server can prepare an installed map automatically. For a public Workshop item, the [CS2FOW Map Baker](https://cs2fow-bake-service.onrender.com/) can give you a ready-to-use `.bvh8` wall index and its `.json` verification report.

</details>

<details>
<summary><strong>What happens when a map changes?</strong></summary>

CS2FOW checks the map file's fingerprint—its file size and CRC checksum—against the saved bake. If they do not match, it rejects the old data and keeps everyone visible. The automatic baker can then make a fresh copy.

</details>

<details>
<summary><strong>What does "fail open" mean?</strong></summary>

It means CS2FOW would rather show too much than hide the wrong player. If something is missing, old, or uncertain, your server sends the player normally.

</details>

## Honest limits

- Baked walls and live smoke can block sight. Doors, breakable objects, moving props, particles, projectiles, and other moving things cannot.
- CS2FOW uses the movement keys currently pressed rather than guessing future player positions. A fast target peeking a stationary player may still appear late.
- Extra compensation for network delay reduces corner pop-in by moving shoulder and movement-direction checks farther around corners. Smoother peeks cost a little more hidden-position information.
- The feet point can see through low gaps that the player's eyes cannot. It is intentionally included to reduce late reveals around low geometry.
- Sounds, bomb information, teammate information, last-known positions, and other clues that are not part of the player entity remain available.
- If CS2FOW does not recognize the CS2 server file, it disables itself instead of guessing private memory locations.
- CS2FOW does not filter during a full update, when CS2 refreshes a player's complete game state. It also leaves both outgoing network lists unchanged if CS2 does not provide them safely.
- Automated builds and tests cannot reproduce every detail of a live server's outgoing entity lists or guarantee against crashes in CS2's internal entity-copy code. Live-server testing is still necessary.

## Troubleshooting

**`cs2fow_status` says AVX is missing:** AVX is a CPU feature CS2FOW uses for fast geometry calculations. A physical CPU may support it while a virtual machine hides it, so check that the host exposes AVX fully to the guest.

**Automatic bake says permission denied on Linux:** restore permission to run the files, then check whether filesystem or container security rules block them:

```sh
chmod +x game/csgo/tools/cs2fow_baker
chmod +x game/csgo/tools/vrf/linux64/Source2Viewer-CLI
```

When the automatic baker or ValveResourceFormat (VRF) map reader fails, the error includes the newest 8 KiB, roughly 8,000 bytes, of their combined output. Cancelling a bake or hitting the timeout also stops every helper program it started, so nothing is left running in the background.

**The server program does not match:** your CS2 update and CS2FOW gamedata compatibility file do not belong together. Install a CS2FOW build verified for the current Valve server program. There is intentionally no "try it anyway" switch.

**A bake is rejected after a CS2 update:** Valve probably changed the map's source VPK archive. CS2FOW compares the saved and installed map fingerprints, then rebakes instead of trusting an old copy.

**You need to report a bug:** include your CS2FOW version, operating system, map, `cs2fow_status` output, nearby server logs, what the players were doing, and a short clip for visibility or pop-in problems. Those details turn "it broke" into something that can actually be reproduced.

## Building from source

The build scripts download exact tested versions of the plugin interface (Metamod:Source), CS2 development files (HL2SDK), build tool (AMBuild), map reader (ValveResourceFormat), and Valve's standard Linux build environment (Steam Runtime). They then compile, test, verify, and package the project.

Windows needs PowerShell, Python 3.8 or newer, and Visual Studio 2022 with the C++ workload:

```powershell
.\scripts\build-windows.ps1
```

Linux builds inside Steam Runtime 3, Valve's standard environment for Linux game software:

```sh
bash scripts/build-linux.sh
```

Both scripts produce installable ZIP files under `packages/`. See the [code tour](docs/CODE_TOUR.md#build-test-package-and-release) for the complete build, test, package, and release flow.

## Contributing

Run CS2FOW on a real server. Test it, [send reproducible reports](https://github.com/karola3vax/CS2FOW/issues), and include the CS2FOW version, operating system, map, `cs2fow_status` output, nearby logs, and a short clip for visibility problems.

If CS2FOW earns a place on your server, star the repository and share your clips. That helps more server owners find it and gives the project better real-world feedback.

## Developer tools

- [Code tour](docs/CODE_TOUR.md): follow the project layout, how work is divided between the game and background workers, safety rules, and build and release steps in plain language.
- [Visibility Studio](tools/visibility_point_editor/README.md): simulate the rounded body capsules, padded rectangular AABB checks, movement, maps, smoke, HE grenades, and visibility decisions locally.
- [CS2FOW Map Baker](https://cs2fow-bake-service.onrender.com/): prepare visibility data from a public Workshop map.
- [Bake Service source](https://github.com/karola3vax/CS2FOW-Bake-Service): inspect the public baking service itself.

Manual baker examples:

```text
cs2fow_baker --game <cs2-root> --map de_dust2 --output de_dust2.bvh8
cs2fow_baker --list-maps --vpk <outer_dir.vpk>
cs2fow_baker --inspect-bvh8 <file>
cs2fow_baker --game <cs2-root> --map workshop/123/de_example --vpk <outer_dir.vpk> --output de_example.bvh8
```

## License

CS2FOW is free and open-source software licensed under the [MIT License](LICENSE). Generated map files come from Counter-Strike 2 game data and are covered by [DATA_NOTICE](DATA_NOTICE). Dependencies keep their own licenses; see [Third-party notices](THIRD_PARTY_NOTICES).
