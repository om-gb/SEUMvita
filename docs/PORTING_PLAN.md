# SEUM → PS Vita porting plan

All numbers come from the real game data (Unity 2018.3.7f1, Mono, x86 Windows
build). They were produced by `tools/asset_inventory.py` and `tools/level_budget.py`.

## 1. What the game actually is (measured)

| | Count | Notes |
|---|---|---|
| Levels | **168** (`Resources/Levels/Level_N`) + DLC bundle | Each level is one FBX-model prefab plus a `.sap` data file. The 12 Unity scenes are only menus and loaders. |
| Triangles per level | median **91k**, p90 242k, max 5.0M | 115 levels are ≤150k, 38 are 150–300k, **15 are >300k** (worst: 119 at 5.0M, 135 at 1.1M, 118 at 840k) |
| Textures used per level | median **31 MiB** as shipped, **8 MiB** at half-res DXT | Whole game: 1,428 textures, mostly DXT1/DXT5, many 2048² |
| Lights per level | avg **11** realtime | |
| Particle systems per level | avg **47** | |
| Colliders | 15k MeshCollider, 11k primitive colliders | Movement collides with real mesh geometry |
| Gameplay script types | **102** distinct, ~40 matter | Top ones: CollisionRule, SpinAnimation, PulseAnimation, EdgeProximity, CrackedPlatform, Mine, TriggerObject, MovingPlatform, Sensor, Gate, SpeedCapsule, GhostPlatform, Powerup, RingTrigger, DartGun, SpikesAnimated, Saw, AxeAnimation… |
| Player controller | `FPSInputController` (2.2k lines) + `CharacterMotor` (780 lines) | Built on Unity's CharacterController. This is the "feel" of the game. |
| Physics queries | Raycast, SphereCast, CapsuleCast, OverlapSphere/Capsule, CheckCapsule | No heavy rigidbody simulation (107 Rigidbodies in the whole game) |
| Audio | 400 clips, 58 min, all Vorbis | Music tracks are about 4 min each |
| Shaders | 109 | Mostly Unity Standard/Legacy plus about 20 custom (glow, additive, multitexture, decal) |
| Third-party | Rewired, iTween, Colorful, SMAA, Battlehub runtime editor, Steamworks | |

**Verdict:** the port is feasible. Typical levels fit the Vita once textures are
halved and lighting is simplified. The real work is reimplementing the gameplay
code and the player controller faithfully. Only 15 levels need geometry reduction.

## 2. Approach

There is no Unity runtime for Vita homebrew, and running Mono plus the Unity
engine is not an option. So the plan is a **reimplementation**: a small C++
engine on vitaGL that plays SEUM's data, with the C# gameplay logic ported to C++.

```
PC/container                                Vita
-----------------------------------------   -------------------------------------
Seum_Data (user's own copy)                 seum.vpk  (engine only, no game data)
   │                                           │
   ▼  tools/export/ (Python + UnityPy)        ▼
 per-level  .lvl   meshes, materials,       ux0:data/seum/
            colliders, objects + params        levels/*.lvl
 textures   .dds   half-res DXT1/5             tex/*.dds
 audio      .ogg   as shipped                  audio/*.ogg
 ui         .dds   menu art from *Psd          ui/*.dds
```

- **The same engine builds for Linux** (SDL2 + desktop GL) **and Vita** (SDL2 +
  vitaGL). I can run it in this workspace under a virtual display, take
  screenshots, and compare against expected behaviour before anything touches
  the console. The Vita is only needed for performance and controls.
- **Bring your own data.** The VPK contains no SEUM assets. The exporter turns a
  legitimately owned copy into the Vita data folder. This is the standard,
  legally safer way homebrew ports are shared. Note: the copy provided contains
  a Steam crack (`Plugins/SOVEREIGN.*`, `steam_api.svrn`). None of the Steam or
  Plugins files are used. Use your own Steam copy's `Seum_Data` for anything
  you share.

## 3. System-by-system plan

| System | Unity original | Vita version |
|---|---|---|
| **Rendering** | Forward renderer, ~11 realtime lights, post effects | 960×544, target **30 fps locked** (60 as a stretch goal on light levels). One directional light per pixel; other lights **baked into vertex colours** by the exporter. Frustum culling per mesh, plus distance culling. |
| **Shaders** | 109 | About 8 hand-written CG shaders, precompiled to `.gxp`: lit-diffuse, lit-diffuse+vertex-colour, unlit, additive-glow, alpha-blend, multitexture, decal, skybox. Every material is mapped to one of them by the exporter. |
| **Post effects** | SMAA, Colorful, ImageEffects | Dropped. MSAA 2x if the frame budget allows. |
| **Textures** | 31 MiB per level as shipped | Half resolution, DXT1/DXT5 kept (vitaGL supports DXT natively), mipmaps kept, so about 8 MiB per level, loaded per level. |
| **Heavy levels** | 15 levels >300k tris | Offline decimation (meshoptimizer) of decorative meshes only; collision meshes are never decimated. Level 119 (5M) needs individual attention. |
| **Particles** | ~47 systems per level | Simple CPU billboard particles, capped per system, emitters culled by distance. |
| **Collision** | PhysX MeshCollider + CharacterController | Per-level static BVH over collision triangles (built offline), plus a capsule-sweep **reimplementation of CharacterController**: skin width, slope limit, step offset, collision flags. Primitive colliders for triggers and moving objects. Only the query types listed in §1 are needed. |
| **Player controller** | FPSInputController + CharacterMotor | Line-by-line port with the same fixed timestep, so jump heights, air control and speed match. This is what speedrunners notice, so it gets its own test harness (§5). |
| **Gameplay objects** | ~40 important component types | Ported one by one in order of how often they're used. The exporter writes each object's serialized fields; a C++ component reads them. |
| **Animations** | Spin/Pulse/Chain/Axe scripts, iTween, 2.9k Animators | Scripted animations are ported directly. Animator clips are baked to keyframes. 652 skinned meshes (character, hands) use GPU skinning, kept simple. |
| **Input** | Rewired, mouse look | SceCtrl directly. Right stick look with acceleration curve, deadzone and sensitivity options; optional gyro aim (SceMotion). Mouse look is the biggest design risk for feel. |
| **Audio** | 400 Vorbis clips | Music streamed from OGG; SFX decoded at load. SoLoud or OpenAL-soft (both already installed). |
| **UI / HUD** | ~30 "Psd" menus drawn from textures | Rebuilt in a small immediate-mode UI using the original menu art, laid out for 960×544. Timer, HUD, level select, options, pause, results. |
| **Saves** | PlayerPrefs | `ux0:data/seum/save.dat`: best times, unlocks, options. |
| **Cut** | | Level editor, Workshop, Battlehub editor, Steam/achievements, mods, leaderboards, video player (videos may return later via SceAvPlayer). |

### Vita memory budget (per level, target)

| | Budget |
|---|---|
| Textures (half-res DXT) | ~8–16 MiB, CDRAM/VRAM |
| Level geometry (~100k tris) | ~6 MiB |
| Collision BVH | ~4 MiB |
| Audio (SFX resident, music streamed) | ~20 MiB |
| Engine + code + UI | ~30 MiB |
| **Total** | **well under 128 MiB**; the Vita gives apps ~365 MiB RAM + 128 MiB CDRAM |

## 4. Milestones

| # | Deliverable | How we know it's done |
|---|---|---|
| **M0 Exporter** | `tools/export/` writes `.lvl/.dds/.ogg` for every level | All 168 levels export; a validator checks counts against the Unity data |
| **M1 Level viewer** | Engine renders a level with a free-fly camera | Linux screenshots look right; on the Vita, a typical level (~90k tris) holds 30 fps |
| **M2 Movement** | Player controller + collision | Harness numbers match the Unity code's values; level 1 can be run start to finish |
| **M3 Gameplay** | Top ~40 components, level flow, timer, level select | The first world is fully playable |
| **M4 Front end** | Menus, HUD, audio, saves, options | Boots to the main menu, saves best times |
| **M5 Whole game** | Remaining components, heavy-level decimation, DLC levels | All 168 levels finish; none drops below 30 fps |
| **M6 Polish** | Controls tuning, loading times, LiveArea art | Ready for release (engine VPK + exporter) |

M0–M2 are where most of the risk is. After M2, the remaining work is mostly
breadth: more component types and more levels, not unknowns.

## 5. Testing loop

- **Linux build in this workspace.** Headless runs (Xvfb + Mesa) for rendering
  screenshots and an automated "replay" harness that feeds recorded inputs and
  checks player position and time at checkpoints.
- **Movement parity.** Expected jump height, run speed and air acceleration are
  derived from `CharacterMotor` and `FPSInputController` constants and asserted
  in tests.
- **Vita** (once you install the plugins from `vita-deploy.zip`): vita-agent-use
  installs and launches builds and captures logs and crashes; psp2-tooling
  profiles CPU and GPU on the slow levels.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Movement doesn't feel identical | Line-by-line port, same fixed timestep, parity tests, compare against the PC version |
| Stick aiming worse than mouse | Tunable curves, gyro option, possibly gentle assist on rings and gates |
| Heavy levels too slow | Decimation, more aggressive culling, lower draw distance per level |
| CharacterController edge cases (steps, slopes, edges) | Reimplement its documented algorithm; test on the `SteepMarker`/`EdgeProximity` levels |
| `.sap` level-data format undocumented | Its loader is in the decompiled `Level.cs`; port that reader directly |
