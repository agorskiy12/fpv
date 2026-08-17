# FPV Drone — Project Progress

Written 2026-08-17, at commit `ca4fea9` (46 commits). This is the pick-up-later document: what the
game is, what actually works, where everything lives, and — most importantly — the things that
will waste your time if you rediscover them the hard way.

---

## 1. What the game is

A **wartime FPV drone operator** game in Unreal Engine 5.8. You fly a heavy, badly-balanced
kamikaze quad in acro mode and strike targets. It reached this design through two pivots (racing
sim → target hunting → two-sided battle); `docs/GAME_DESIGN.md` is the canonical design doc and
supersedes the earlier planning docs.

**The intended match:** Russia vs NATO. Pick a side, 10 minutes, highest score wins.

The decisions that define it:

| Decision | Detail |
|---|---|
| **Primary objective** | Find and kill the **hidden enemy FPV operator** — worth 5000, dwarfing everything else |
| **How you find them** | By **radio signal strength** only. No map marker. Fly around and read the meter |
| **No HUD by default** | You identify units by **uniform colour**. Russians wear a red or white sleeve stripe, NATO yellow or green |
| **Civilians** | Move around, and killing them **costs you points**. Symmetric — the enemy is penalised too |
| **Friendly fire** | Possible and penalised |
| **Your drone** | Can be shot down, including by infantry |
| **Death** | You see your own body |
| **The operator** | Cannot move yet. Later: exit FPV, run, hide, relaunch |

Placeholder visuals until real models: **orange cube = FPV operator, red cube = Russian soldier,
green cube = NATO soldier.** Banners mark each side — Russian tricolour, and **Ukrainian** colours
for the NATO side.

---

## 2. Running it

```bash
git clone git@github.com:agorskiy12/fpv.git
```

Double-click `LaunchGame.bat` or `LaunchEditor.bat` in the repo root. To build from a shell:

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" FPVDroneEditor Win64 Development -Project="C:/Users/jsdev/Desktop/work/fpv/FPVDrone.uproject" -WaitMutex -FromMsBuild
```

Then in-game, open the console (`~`) and run `fpv.SpawnTestTargets` to build the whole scene.

**Toolchain:** UE 5.8, MSVC 14.50.35737, Windows SDK 10.0.26100. Visual Studio needs the
`Microsoft.Net.Component.4.8.SDK` component — the targeting pack alone is not enough and the build
fails with `Could not find NetFxSDK install dir`.

---

## 3. Where the level actually is — read this first

**The game has been running in `/Engine/Maps/Templates/OpenWorld`, an engine template. Not our
map.** This was only discovered on 2026-08-17 and is still unresolved.

- `Config/DefaultEngine.ini` sets **no default map at all**, so the engine falls back to its template
- `Content/NewMap.umap` is **0 MB** — empty, and never loaded by the game
- The template's landscape gives **750 m of usable ground**. At the drone's ~130 km/h that is about
  eleven seconds across

So the landscape you can see was never ours, and the map is far too small for the game.

**Recommended fix (tested, not yet applied):** use
`Content/MWLandscapeAutoMaterial/Maps/LandscapeAutoMaterial_Desert_Example` as the base map. It was
launched with the mission spawner and worked — the runway placed, the deck was found, and terrain
scatter reached 3.3 km with zero misses, so it has real sculpted terrain with the auto-material and
grass already applied. It is also the right arid-airfield look.

The alternative is sculpting a landscape in `NewMap` and assigning a material instance of
`MTL_MWAM_AutoMaterial_MASTER` (or starting from the Desert instance). More control, more work.

---

## 4. What works

**Flight.** Full acro-mode model: Betaflight `applyActualRates` curve, PID rate controller,
thrust-to-weight 3.6, drag tuned to roughly 130 km/h top speed. Runs on physics with
`bAccelChange=true` so handling is independent of mass and inertia.

**Airframe instability.** The drone is deliberately hard to fly — it is a heavy quad carrying a
bomb, not a tuned racer. Driven by one dial, `AirframeInstability` (0 = tuned racer, 1 = improvised
bomber), currently **0.35**. Uses **Perlin** noise, not white noise: white noise gets cancelled by
the rate loop and does nothing.

**RC transmitter.** A real **TBS Tango 2** over USB works end to end — HID enumeration, direct
report decoding, channel mapping, calibration wizard. Defaults are Tango 2: `throttle=8 yaw=5
roll=7 pitch=6`.

**Targets and combat.** Vehicles, soldiers, hidden operators, enemy UAVs, a transport helicopter,
structures. Blast damage with linear falloff measured to the body surface. Niagara explosions,
size-graded by blast radius. Ragdoll destruction via physics assets.

**Factions.** `IFactionMember` interface plus a single `GetAttitude` function. Neutral is
contagious — a neutral on either side of a pair makes the pair neutral.

**Signal sensing.** The HUD meter reads distance to the nearest hostile operator. Falloff is
**linear on purpose** — inverse-square has an almost flat far-field gradient, so you could not tell
which way you were moving, which is the entire mechanic.

**Terrain scenery.** ~2,030 instanced meshes — background mountains, rock, scrub — from the MW
pack. Measures how far ground actually extends and sizes itself to fit, so it works on a 750 m
template or a 6 km landscape. The mountain ring sits at the boundary and hides the edge of the
world.

**Ground conforming.** Everything is traced onto whatever surface is actually beneath it, at spawn
and (for units that move) every tick.

---

## 5. Source map

All in `Source/FPVDrone/`. 46 files, the large ones noted.

| File | What it does |
|---|---|
| `FPVWarGameMode.cpp` (1254) | **The big one.** Round and strike sequence, blast application, signal queries, and `fpv.SpawnTestTargets` which builds the entire scene |
| `FPVHUD.cpp` (723) | Canvas HUD — signal meter, channel monitor, device picker, calibration wizard |
| `FPVDronePawn.cpp/.h` (512/347) | The flight model, instability, warhead, camera shake |
| `RCDeviceRegistry.cpp` (494) | HID enumeration and direct report decoding |
| `DroneTarget.cpp/.h` (387/197) | Base target — health, score, destruction, `StickToGround` |
| `RCChannelMapping.cpp` (288) | Channel assignment, inversion, Tango 2 defaults |
| `SoldierTarget.cpp` (287) | Infantry patrol, alerting, uniform colours |
| `AtmosphereController.cpp` (264) | Sky and lighting setup |
| `EnemyDroneTarget.cpp` (216) | Enemy UAV loitering and fleeing |
| `HelicopterTarget.cpp` (221) | Orbiting transport heli |
| `RCChannelMonitor.cpp` (185) | Axis logging and observed ranges |
| `ExplosionEffect.cpp` (178) | Niagara selection by size, light flash, radial impulse |
| `DebrisChunk.cpp` (137) | Destruction debris |
| `VehicleTarget.cpp` (135) | Vehicle routes along the runway |
| `TerrainScatter.cpp/.h` (127/76) | Instanced scenery scattering |
| `BannerMarker.cpp` (120) | Faction banners from stacked coloured cubes |
| `FPVFactions.cpp/.h` (68/73) | `EFaction`, `EFactionAttitude`, `IFactionMember`, `GetAttitude` |
| `OperatorTarget.cpp/.h` (58/56) | The hidden operator and its signal |
| `StrikeCamera.cpp` (82) | Kill-cam |
| `ImpactShake.h` (71) | Trauma-based camera shake |

**Modules:** Core, CoreUObject, Engine, InputCore, EnhancedInput, PhysicsCore, Niagara. Windows
adds RawInput and InputDevice.

Enhanced Input actions and mapping contexts are **built in C++ at runtime** — there are no Input
Action assets. This is why respawn reuses the pawn instead of respawning it: possession, camera and
input bindings all stay intact.

---

## 6. Key tunables

In `FPVDronePawn.h`:

| Value | Current | Notes |
|---|---|---|
| `MaxRates` | 800/800/600 °/s | roll / pitch / yaw |
| `ThrustToWeightRatio` | 3.6 | |
| `AirframeInstability` | **0.35** | Was 0.65 — too wobbly, cut hard. Live-tune with `fpv.Instability` |
| `DisturbanceStrength` | **13** | Was 60. At 60 it peaked ~66 rad/s² against ~12 for a normal correction, so it overrode the sticks entirely |
| `CameraVibrationDegrees` | 0.2 | |
| `BlastRadius` | 1400 | |
| `BlastDamage` | 300 | |
| `ArmingImpactSpeed` | 450 | |
| `ArmingAltitude` | 150 | |
| `bInvertRollAxis` | true | **Unverified — see gotchas** |
| `bInvertPitchAxis` | true | **Unverified** |
| `bInvertYawAxis` | false | **Unverified** |

Operator (`OperatorTarget`): `ScoreValue` 5000, `SignalRange` 45000 (450 m), `SignalDecayTime` 4 s,
`AlertRadius` 0 — it never breaks cover.

---

## 7. Console commands

**Scene**
- `fpv.SpawnTestTargets` — build the whole mission scene
- `fpv.SpawnGroundPlane 0|1` — placeholder ground slab. Auto-skipped when the level has its own ground
- `fpv.ApplyAtmosphere` — re-apply sky and lighting

**Tuning**
- `fpv.Instability [0..1]` — airframe instability, live
- `fpv.ExplosionScale [n]` — explosion visual size

**Factions**
- `fpv.SetFaction russia|nato|neutral`
- `fpv.Factions` — list every unit and its attitude toward you

**HUD**
- `fpv.ShowTargetMarkers 0|1` — **default 0 by design.** Identification is meant to be visual
- `fpv.ShowChannels 0|1`, `fpv.ShowDevices 0|1`

**RC transmitter**
- `fpv.Calibrate` / `fpv.CalibrateCancel` — the wizard
- `fpv.SetChannel throttle 8` / `fpv.InvertChannel pitch 1`
- `fpv.SelectDevice 1`, `fpv.RefreshDevices`, `fpv.ReRegisterDevice`
- `fpv.LogChannels 0|1`, `fpv.ResetChannelRanges`

---

## 8. Gotchas — the expensive ones

**Fab assets are invisible on disk until you hit Save All.** They show in the Content Browser but
the `.uasset` files do not exist yet, so `LoadObject` fails and the code takes a fallback path. This
cost time on the runway, the soldier textures and the van meshes. If an asset "is definitely there"
but will not load, hit **Save All** first. The van pack also shipped a raw `vans.FBX` needing manual
import.

**Never place a scaled asset without checking its bounds.** The runway imported at **79 km** long,
directly on top of the player start. The symptom was "everything is shaking and the drone is not
flying" — collision entrapment, not an input bug. Everything imported is now normalised to a target
size from its own bounds. Pivots are just as often at a corner as at the centre, so centre on all
three axes, not just Z.

**`RecreatePhysicsState()` after setting a mesh or scale at runtime.** Without it the collision body
still describes the default, and traces find nothing. The "no collision" message this produces is
actively misleading.

**Height-based placement rules break on real terrain.** A rule like "reject anything above 200 cm"
works only while the ground is a flat slab at a known Z. Measurement showed it discarding 42% of
scrub onto perfectly good hillside. Reject by *surface identity* and slope instead.

**Interpolate state transitions on anything moving.** The helicopter appeared to be two
helicopters: `bFleeing` snapped the orbit radius 15% (~11 m) in a single frame and TAA rendered the
teleport as a ghost. Same defect existed on the UAVs. Both fixed with interpolation and hysteresis.

**Chained random-turn arcs are a random walk** — the UAVs wandered off the map. They need a leash
back toward a centre.

**Diagnose by absence too.** The Tango 2 was invisible through three sequential faults, and the last
one — `ParseInputData` never being entered at all — was found by noticing that a warning which
*should* have fired never did.

**Wildcard RawInput device configs bind fewer axes than no config at all**, because `SetupBindings`
breaks on the first match.

**`FAutoConsoleCommandWithArgs` does not exist.** Use `FAutoConsoleCommand` with
`FConsoleCommandWithArgsDelegate`.

**`GenericTeamAgentInterface.h` is in AIModule, not Engine.** `docs/TEAMS_DESIGN.md` originally
claimed adopting it "costs a single function"; that was wrong and the doc has been corrected. We
use a plain `uint8 ToTeamIndex` instead.

---

## 9. Known issues

- **The map.** See section 3 — we are in an engine template with 750 m of ground.
- **Drone axis signs unverified.** `bInvertRollAxis` / `bInvertPitchAxis` / `bInvertYawAxis` are a
  physics-frame correction that has never been confirmed against real flight. If the drone rolls
  the wrong way, start here.
- **Soldier meshes have no textures.** The pack shipped without them and without source files — a
  pack limitation, not a Save All problem.
- **Repo size.** `Content/` is ~1.1 GB. GitHub warns on push:
  `LandscapeAutoMaterial_MountainRange_Example.umap` is 54 MB, over their 50 MB recommendation. The
  three MW example maps are ~128 MB of sample content that will never ship — gitignoring them is
  the easy win.
- **Instability and explosion scale are untuned by feel.** Both have live console dials; they need a
  real flying session and a number.

---

## 10. What's next

**Finish the duel (Phase 1).** The core loop is still incomplete:

1. Enemy FPV operator AI that hunts *your* operator — the other half of the duel
2. Death cam showing your own body
3. Round timer and score

**Then the match:** 10-minute rounds, side selection, civilians, the scoring table with its
penalties.

**Level work:**
- Decide the map (section 3) and move the mission onto it
- Remove the procedural ground slab entirely once a real Landscape is in place
- A `PropScatter` actor for systematic prop placement — offered, not built
- Friendly radio chatter revealing your team's plans and positions

**The MW pack** (`Content/MWLandscapeAutoMaterial/`, 701 MB, 100 files) is worth knowing: the
`LGT_MWAM_*` assets are **Landscape Grass Types**, so the auto-material scatters its own grass and
stones onto terrain with no code at all. Assign the material and it populates itself. Three example
maps ship (Desert, Island, MountainRange) — open the Desert one to see the target look.
