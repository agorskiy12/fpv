# Building an explosion in Niagara

Everything here uses what already ships with the engine. Nothing to download.

## What Niagara is

Niagara is Unreal's particle and visual-effects system — the thing that makes fire, smoke,
sparks, dust, water and magic. It replaced the older Cascade system and is built into the
engine; the plugin is already enabled on this project.

The mental model is two nested pieces:

- **Emitter** — one behaviour. "40 orange puffs that fly outward and fade." An emitter owns its
  particles from spawn to death.
- **System** — a container holding several emitters that play together. An explosion is
  normally three or four emitters: fireball, smoke, embers, ground dust.

Each emitter is a stack of **modules** executed in order, grouped into stages:

| Stage | Runs | Typical modules |
|---|---|---|
| Emitter Update | Every frame, per emitter | Spawn Burst Instantaneous, Emitter State |
| Particle Spawn | Once, per new particle | Initialize Particle, Add Velocity, Shape Location |
| Particle Update | Every frame, per particle | Drag, Gravity Force, Scale Color, Scale Sprite Size |
| Render | Every frame | Sprite Renderer, Mesh Renderer |

That is the whole concept. The rest is which numbers to put where.

## Templates that ship with the engine

When you create a Niagara System you are offered emitter templates. The useful ones here:

| Template | Use it for |
|---|---|
| **OmnidirectionalBurst** | The fireball, and the smoke. Sprays particles outward from a point |
| **UpwardMeshBurst** | Embers and chunks — spawns meshes rather than sprites |
| **SimpleSpriteBurst** | A simpler flat burst; fine for dust |
| **HangingParticulates** | Slow drifting haze, good for lingering smoke |
| DirectionalBurst | Cone-shaped, for directional blasts |

There is no ready-made "Explosion" template. You build one by combining the above — which is
the normal way this is done, and takes about twenty minutes.

---

## Step 1 — Make the fire material

The default particle material is *translucent*, which reads as smoke. Fire needs **additive**
blending, where overlapping particles get brighter instead of muddier. This one change is the
difference between a grey blob and a ball of flame.

1. Content Browser, find `Engine/EngineMaterials/DefaultParticle`
   (enable **Show Engine Content** in the Content Browser settings cog if you cannot see it)
2. Right-click it → **Duplicate**, save into your project as `Content/FX/M_Fire`
3. Open it. In Details:
   - **Blend Mode** → `Additive`
   - **Shading Model** → `Unlit`
4. Save

## Step 2 — Create the system

1. Content Browser → right-click → **FX → Niagara System**
2. Choose **New system from selected emitter(s)**
3. Pick **OmnidirectionalBurst**, add it, finish
4. Save as `Content/FX/NS_Explosion`

## Step 3 — The fireball emitter

Open `NS_Explosion` and select the emitter.

**Emitter Update → Emitter State**
- `Loop Behavior` → **Once**. Otherwise it repeats forever, which is the single most common
  first mistake.

**Emitter Update → Spawn Burst Instantaneous**
- `Spawn Count` → **45**

**Particle Spawn → Initialize Particle**
- `Lifetime` → Random Range, **0.35 to 0.8**. Short. Real fireballs are brief; the smoke is
  what lingers.
- `Color` → click the swatch, and set values **above 1**: roughly `R 14, G 4, B 0.6`.
  Values over 1 are HDR and are what make it bloom and glow. This is the setting people miss.
- `Sprite Size` → Random Range, **60 to 220**

**Particle Spawn → Add Velocity**
- `Velocity` → Random Range, **250 to 900**, in all directions

**Particle Update → add module `Drag`**
- `Drag` → **3.0**. Hot gas decelerates hard; without drag it looks like a firework.

**Particle Update → add module `Acceleration Force`**
- `Acceleration` → `Z = 250`. Fire rises.

**Particle Update → Scale Color**
- Set the alpha curve to fall from 1 to 0 over the particle's life
- Set the colour curve to travel bright yellow → orange → dark red. That cooling is most of
  what makes it read as fire rather than as orange confetti.

**Particle Update → Scale Sprite Size**
- Curve rising quickly then flattening — fast expansion, then it holds and fades

**Render → Sprite Renderer**
- `Material` → your **M_Fire**

Save, and the preview should already look like an explosion.

## Step 4 — The smoke emitter

Smoke is what makes a strike feel like it happened.

1. In the system, click **+ Emitter** → add another **OmnidirectionalBurst**
2. Rename it `Smoke`

Changes from the fire settings:
- `Loop Behavior` → **Once**, `Spawn Count` → **25**
- `Lifetime` → **2.5 to 5.0**
- `Color` → dark grey, **below 1**: about `0.05, 0.05, 0.05`, alpha `0.6`. Smoke is not
  emissive — it blocks light.
- `Sprite Size` → **200 to 500**, growing over life
- `Velocity` → **80 to 300** — much slower than the fire
- `Acceleration Force` → `Z = 120`, so it rises and drifts
- `Render → Sprite Renderer → Material` → leave as the default **translucent** particle
  material. Do not use M_Fire here.

## Step 5 — Embers (optional, high value for effort)

1. **+ Emitter** → **UpwardMeshBurst**
2. `Spawn Count` **30**, `Lifetime` **1.0 to 2.5**
3. Add `Gravity Force` so they arc and fall
4. Small bright HDR orange colour, tiny mesh scale

Little points of light arcing away sell scale better than a bigger fireball does.

---

## Step 6 — Hooking it up

Save the finished system as:

```
Content/FX/NS_Explosion
```

The code looks for that exact path and uses it automatically if present. When it is found, the
placeholder debug sphere switches off on its own — `AExplosionEffect` already scales the system
to blast radius and keeps the light flash and physics impulse, which are real effects rather
than placeholders.

Nothing else needs wiring.

## If you would rather not author it

`Window → Fab` and search for Niagara explosion packs. The hook-up is identical: assign the
system, everything else already works. An afternoon of shopping beats an afternoon of authoring
if VFX is not the part you want to be doing.

## NiagaraFluids — the heavyweight option

The `NiagaraFluids` plugin ships with the engine and does genuine volumetric gas simulation:
real fire and smoke that curls, billows and collides with the world, rather than sprites
pretending to.

It looks extraordinary and costs accordingly — realistically one at a time, not a chain
reaction. Worth considering for the helicopter specifically, once the sprite version is working.

## Common first mistakes

- **Loop Behavior left on Infinite** — the explosion repeats forever
- **Colour left at or below 1** for fire — no bloom, and it looks like paper
- **Translucent material on fire** — reads as brown smoke instead of flame
- **Lifetime too long** — real fireballs are under a second; long ones look like a gas leak
- **No drag** — particles fly in straight lines and read as fireworks
