# Making the level feel real

## What is actually there right now

- An 8 km untextured grey slab
- A runway deck floating on it
- Cubes standing in for structures, a gas line and a substation
- Vans, soldiers, UAVs and a helicopter, all correctly placed and moving
- Default template lighting, and the atmosphere controller built but not placed
- **The whole scene is spawned at runtime by `fpv.SpawnTestTargets`** — nothing is authored

The moving parts are in good shape. The environment is not, and it is now the thing holding the
game back.

## Why this environment is the hard case

FPV footage feels fast and real for one reason: **things pass close to the lens.** The camera is
wide — 120 degrees here — so anything within a few metres sweeps across the frame violently,
and that parallax is the entire sensation of speed. It is not the number on the HUD.

An open airfield is the worst possible environment for that. At 130 km/h over flat empty ground
with a distant horizon, nothing moves relative to the camera and the drone feels like it is
hovering. The level is currently fighting the genre.

Two consequences worth internalising:

- **Detail spent far away is wasted.** A beautiful hangar 300 m off contributes almost nothing.
  The same asset 8 m from the flight path transforms the shot.
- **The ground is most of the frame** when flying low, which is where this game is played. It is
  the single largest surface and currently the least finished thing in the level.

## Priority order

Sorted by impact per hour, not by how interesting each is.

### 1. A ground material

The 8 km grey slab is the biggest visual problem in the game. Megascans is free for Unreal —
asphalt, cracked concrete, sand, gravel. One material assignment changes every frame.

Worth blending two or three across the surface so it is not uniform. Uniformity is what reads as
fake, more than low resolution does.

### 2. Place the atmosphere controller

Already built, costs nothing. Drop an `AtmosphereController` into the level and set `Mood`.
Overcast plus fog hides the flat horizon, gives distance a gradient, and makes the whole scene
sit together. This is the cheapest realism in the entire list.

### 3. Undulate the ground

Replace the flat slab with a Landscape carrying gentle height variation — a metre or two is
plenty. A razor-straight horizon reads as a plane; a slightly broken one reads as terrain. It
also gives the drone something to follow at low level, which is most of what makes low flying
feel low.

### 4. Things to fly between

The gameplay-critical one. Along and beside the runway:

- Hangars, or any large boxes with gaps between them
- Shipping containers, stacked and scattered
- A fence line, and poles with wires between them
- Fuel tanks, a control tower, antenna masts

Wires and poles matter disproportionately. They are nearly free to render, and threading between
two poles at speed is the single most FPV thing a player can do.

Place these **along the flight paths**, not artfully arranged at a distance. The test is: can you
fly a line where something passes within five metres of the lens every couple of seconds?

### 5. Clutter at ground level

Barrels, crates, pallets, tyre stacks, rubble, road markings, puddles. Small, cheap, and they
give the eye something to measure speed against near the ground.

### 6. Sky

An HDRI or a decent cloud setup. Cheap, and it sets the mood of everything below it.

## The structural change

`fpv.SpawnTestTargets` was scaffolding for a project with no content. It has outlived that.

**Author the level in the editor instead.** Place the runway, structures and patrol routes by
hand, save the map, and keep the spawner only as a quick way to repopulate a test scene.

That matters for more than tidiness:

- Lighting can be built rather than fully dynamic
- Placement becomes an art decision made while looking at it, instead of a coordinate guessed in
  code
- The scene persists between runs and can be iterated on rather than regenerated

Everything already supports it: every target class is placeable, and the game mode finds targets
in the level as well as ones spawned at runtime.

## The smallest version worth building

Rather than dressing the whole airfield, build one good corridor:

- The runway, with a **line of hangars down one side**, gaps between them
- A **fence and pole line** along the other side
- A **fuel depot** at one end — tanks, pipework, the gas line
- A **road** crossing the approach, with the vans on it
- Ground material and fog

That is perhaps an evening of work and it gives a complete flyable route: dive from altitude,
thread the hangars, break over the fence, hit the depot. Once that corridor feels right, the same
recipe extends outward.

## Who does what

**You**, because it needs eyes on a viewport: choosing assets, placing them, judging scale,
deciding what looks right.

**Me**: anything measurable or systematic — placement tools, procedural scatter, spawning props
along a spline, wiring destruction into new target types, tuning behaviour against numbers.

The honest split is that I have been placing scenery by computing coordinates and reading logs,
which is why the runway ended up in the sea twice. Level design is the part of this project best
done by the person who can see it.
