# Getting real art into the game

The project deliberately ships with no imported content — every target and the drone itself are
assembled from engine primitives at construction. That was the right call for getting the
flight model and transmitter working, and it is the wrong call the moment you want it to look
like anything.

The code now accepts real assets everywhere it matters. Nothing below requires a code change.

## Where the assets come from

**Fab**, Epic's asset store, is built into the editor (`Window → Fab`, plugin already enabled).
You are signed in through the launcher already. Everything else is a distant second.

Two things on Fab are free and directly useful here:

- **Quixel Megascans** — surfaces, rubble, concrete, scrap, vegetation. Free for use in Unreal.
  This is what makes ground and buildings stop looking like grey boxes, and it is the single
  biggest visual win available for zero money.
- **Epic's own free packs**, which rotate. Look for vehicle packs, modular industrial and
  warehouse sets, and Niagara VFX collections.

Outside Fab, **Poly Haven** (CC0) is worth knowing for HDRIs — a good sky alone changes the feel
of the whole scene, and it is a single drag into the level.

## What to get first, in order of impact

| Priority | What | Why |
|---|---|---|
| 1 | A Niagara explosion pack | Every kill currently draws a debug sphere. Highest impact per minute spent. |
| 2 | Ground and building materials (Megascans) | The floor is a grey plane; nothing reads as a place yet |
| 3 | Modular industrial / warehouse set | Gives structures, substations and pipelines real shapes |
| 4 | A vehicle pack | Cars are the best target in the game and currently look like boxes |
| 5 | Sky / HDRI | Cheap, and does a lot for mood |
| 6 | A quad model | Lowest priority — in FPV you never see your own airframe |

Skip anything sold as a complete "city" until the rest is settled. They are enormous downloads
and you do not need one to find out whether the game is fun.

## Wiring it up

### Targets

Every `ADroneTarget` has a **Visuals** section:

| Property | Purpose |
|---|---|
| `BodyMesh` | Real mesh. Setting it hides the placeholder entirely |
| `DestroyedMesh` | Optional wreck, swapped in on death instead of the target vanishing |
| `MeshOffset` / `MeshRotation` / `MeshScale` | Fit the mesh to the target's footprint |
| `BodyMaterial` | Optional override applied to every material slot |

**`BodySize` still drives gameplay** even when a mesh is assigned — blast falloff, HUD marker
sizing and proximity all read it. That separation is deliberate: swapping art must not silently
change balance. Set `BodySize` to roughly the mesh's real footprint and they stay in agreement.

### Explosions

`AExplosionEffect` has an **Assets** section:

| Property | Purpose |
|---|---|
| `ExplosionFX` | Niagara system. Setting it suppresses the debug-sphere fallback automatically |
| `bScaleFXToRadius` | Scales the system to blast radius, assuming it is authored at 1 m |
| `ExplosionSound` | Played at the blast location |
| `bAlwaysUseLightFlash` | Keep the code light flash even alongside Niagara — usually worth it |

The light flash and radial impulse are real and worth keeping regardless; only the expanding
sphere is a placeholder.

### Making it apply everywhere

Setting these per-actor gets tedious. The straightforward route is a Blueprint subclass per
target kind — `BP_Vehicle`, `BP_Substation`, and so on — with the mesh and size configured once,
then place those instead of the C++ classes. `fpv.SpawnTestTargets` still spawns the raw C++
versions, so update it or stop using it once real prefabs exist.

## The environment is the bigger job

Targets are the easy half. What actually sells a wartime FPV scene is the *place* — a road, a
treeline, buildings with something behind them, somewhere to fly between rather than over. That
is level design rather than asset shopping, and it is where the time goes.

A useful smallest version: one road, a handful of buildings along it, a treeline, and a
pipeline running across. Enough to fly through rather than above, which is what makes FPV feel
like FPV.
