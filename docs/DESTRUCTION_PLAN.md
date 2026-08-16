# Fireballs and shattering targets

Plan for making a strike look like a strike. Verified against the UE 5.8 install on this
machine — `Fracture`, `GeometryCollectionPlugin`, `ChaosSolverPlugin`, `ChaosNiagara`,
`NiagaraFluids` and `UCameraShakeBase` are all present.

## What "a million pieces" actually means

Worth being blunt up front: you cannot simulate a million rigid bodies. A realistic budget is
**30–60 real physics chunks per explosion**, and even that only for one or two explosions at a
time.

The look everyone remembers comes from **layering three things that cost wildly different
amounts**:

| Layer | Count | Cost | What it does |
|---|---|---|---|
| Physics chunks | 30–60 | Expensive | Readable. Casts shadows, bounces, settles. This is what the eye tracks |
| Niagara debris particles | 1000s | Cheap | Sells the *volume*. Never inspected individually |
| Fireball, smoke, light | 1 system | Moderate | Sells the *energy* |

Almost all the perceived detail comes from the cheap layer. Chasing it with real physics is the
expensive way to get a worse result, and it is the mistake that makes explosions tank framerate.

So the plan is: get the cheap layers right first, then add real fracture only where it earns
its cost.

---

## Phase 1 — Impact feel

**No assets needed. Entirely code.**

The current explosion is a light flash and a radial impulse. Before adding a single particle,
the *physical* response to a detonation is missing, and it is most of what makes a blast feel
like one:

1. **Camera shake** — a `UCameraShakeBase` subclass with a wave oscillator, scaled by distance
   from the blast. The single highest-impact item in this entire document.
2. **Brief time dilation** — drop to ~0.35 for about 0.15 s on a kill, then ease back. Sells
   weight, and gives the eye time to register the hit before the kill cam cuts.
3. **Post-process punch** — a short exposure and bloom spike, so the flash blows out the frame
   rather than politely lighting it.
4. **Scorch decal** — a dark radial decal at ground zero. Cheap, and it makes the strike leave
   a mark that persists after the smoke clears.
5. **Directional blast slam** on the drone if it survives a nearby blast.

**Why first:** every one of these works on today's placeholder explosion and improves every
explosion added later. None can be invalidated by an art decision.

## Phase 2 — Debris burst

**No assets needed. Entirely code. Works on every target immediately.**

A `UDebrisBurstComponent` on `ADroneTarget` that, on death, spawns a burst of physics chunks:

- **Chunk meshes** — a configurable array, falling back to scaled cubes so it works before any
  art exists. Assign real rubble meshes later and nothing else changes.
- **Velocity** — radial from the blast, *plus the target's own velocity*. A helicopter blown up
  mid-orbit must throw its debris forward along its flight path. This one detail does more for
  believability than doubling the chunk count.
- **Random spin**, random scale within a range.
- **Global cap** — a hard ceiling of roughly 120 live chunks across the whole level, recycling
  oldest-first. Without a cap, one good chain reaction drops the framerate through the floor.
- **Lifetime** — settle, then sink and despawn after a few seconds.

Per-kind tuning: a gas line throws pipe sections, a substation throws twisted metal, a vehicle
throws panels and wheels.

**Why second:** this is the phase that actually delivers "blown into pieces" for *every* target
in the game, at a cost the frame budget can carry. Chaos fracture, by contrast, will only ever
be affordable on a handful of hero objects.

## Phase 3 — The fireball

**Assets required — this part is yours.**

The code side is already done: `AExplosionEffect::ExplosionFX` takes a Niagara system, scales it
to blast radius, and automatically suppresses the debug-sphere fallback the moment it is set.

Two routes:

**Fab pack (recommended).** Search Niagara explosion or VFX packs. An afternoon of shopping
beats a week of authoring, and quality is immediately obvious.

**NiagaraFluids.** The plugin ships with UE and does genuine volumetric fire and smoke
simulation. It looks extraordinary and costs accordingly — realistically one hero explosion at a
time, not a chain reaction. Worth considering for the helicopter specifically.

What a good explosion system needs, for evaluating packs:

- Fast fireball, 0.2–0.4 s, that expands and lifts
- A smoke column that **lingers for 10+ seconds** — persistence is what makes a strike feel
  consequential rather than instantaneous
- Sparks and embers with gravity
- A ground dust ring pushing outward
- Ideally a separate smaller variant for secondary blasts

I cannot author Niagara systems — they are binary editor assets. This phase is entirely acquisition.

## Phase 4 — Chaos fracture, hero targets only

**Optional.** The goal is targets that visibly come apart, and Phase 2 already delivers that.
This phase buys one specific thing: pieces that match the shape of the thing that broke, rather
than generic rubble. Worth it for a helicopter, hard to justify for a shed.

Skip it unless the generic debris looks obviously wrong once it is in.

**You fracture the meshes; I write the runtime code.**

This is real shattering: the actual mesh breaking along real fracture lines.

**Your side, per mesh, in the editor:**
1. Select the static mesh, switch to **Fracture Mode**
2. Uniform Voronoi fracture, 20–40 pieces
3. Optionally a second level of fracture on selected chunks, so large pieces can break again
4. Save the resulting **Geometry Collection** asset

**My side:**
- `bUseGeometryCollection` on `ADroneTarget`, swapping `UStaticMeshComponent` for
  `UGeometryCollectionComponent` when a collection is assigned
- Apply a radial field (`URadialFalloff` + `URadialVector`) at the blast point on death
- Anchor pieces so structures collapse rather than exploding uniformly outward
- Despawn after N seconds
- Requires adding `GeometryCollectionEngine`, `ChaosSolverEngine` and `FieldSystemEngine` to
  the build

**Restrict this to two or three hero targets** — the helicopter and maybe one large structure.
Geometry Collections are heavy enough that a chain reaction of them will stall the frame.
Everything else keeps the Phase 2 debris burst, which is visually close and an order of
magnitude cheaper.

## Phase 5 — Falling wrecks

**Code only.**

Right now a destroyed helicopter simply stops existing, which is by far the least satisfying
kill in the game.

Instead: on death, the airframe becomes a physics body carrying its last velocity, tumbles out
of the sky trailing smoke, and detonates again on ground contact. A ten-second fall you can
follow and fly alongside.

Same treatment for vehicles at lower stakes — roll, flip, come to rest.

**This is the single most memorable change available for the helicopter**, and it needs no art.

---

## Suggested order

1. **Phase 1** — impact feel. Small, no dependencies, improves everything downstream.
2. **Phase 2** — debris burst. Delivers the actual request across all targets.
3. **Phase 5** — falling wrecks. Cheap, and transforms the highest-value target.
4. **Phase 3** — fireball, once you have been through Fab.
5. **Phase 4** — Chaos fracture. Optional, last, and only if generic debris looks wrong.

Phases 1, 2 and 5 are all code and can be done back to back without you touching the editor.
Phase 3 is blocked on asset acquisition. Phase 4 needs per-mesh editor work from you.

## Performance guardrails

Worth fixing now rather than diagnosing later:

- Global live-chunk cap, recycled oldest-first
- Explosion effects capped per second; a chain reaction should not spawn eight full fireballs
- Chunks despawn on a timer, and never accumulate across a session
- Geometry Collections restricted to hero targets, and torn down aggressively
- Distance culling: a strike 300 m away does not need its debris simulated at all

## What I cannot do

Stated plainly so the split is clear:

- **Author Niagara systems** — binary editor assets
- **Fracture meshes into Geometry Collections** — Fracture Mode is a GUI tool
- **Evaluate how any of it looks** — I have no view of the viewport, so judging whether an
  explosion reads well is yours
