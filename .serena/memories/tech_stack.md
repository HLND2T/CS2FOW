# CS2FOW technology and pinned toolchain

- Native code: C++20, compiled for x86-64 Windows/Linux; strict warnings enabled.
- Plugin integration: Metamod:Source 2.x plus pinned CS2 HL2SDK/gamedata; outputs `cs2fow.dll` (Windows) or `cs2fow.so` (Linux).
- Build system: AMBuild 2.2+ via `AMBuildScript` and `AMBuilder`; `configure.py` wires local dependency paths.
- Dependency source of truth: `build-dependencies.json`; commits for AMBuild, Metamod, HL2SDK manifests, and HL2SDK are fixed SHA-1s. ValveResourceFormat/VRF is pinned to 19.2 with SHA-256 archives per platform. SteamRT3 Linux builds use the pinned image digest in that file.
- Compilers: MSVC x64 on Windows; GCC/G++ x86-64 in Linux/SteamRT3. Native geometry code uses AVX-capable CPUs at runtime and SSE4.1 compiler flags on non-MSVC builds.
- Supporting tooling: Python 3 standard-library scripts for bootstrap/build/package/checks; Bash and PowerShell wrappers; Node.js syntax/runtime checks for the local Visibility Studio.
- Vendored code under `third_party/` (miniz, cgltf, picosha2, masked occlusion culling, generated protobuf); keep license/notice files synchronized.
