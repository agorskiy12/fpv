# FPV Operator — game design

Consolidates the design as it stands. Supersedes the battle-mode and teams notes, which remain
for the reasoning behind individual decisions.

## The game in one paragraph

You are an FPV drone operator in a ten-minute battle between two sides. Your own infantry are
fighting a ground war you cannot win for them, and somewhere across the map an enemy operator is
doing exactly what you are — hunting your side, and hunting you. You have no target HUD. You
identify what you are looking at by flying close enough to see it, which is also close enough to
be shot down. Your main objective is to find and kill the enemy operator, which requires
triangulating his position from the strength of his control signal. Highest score at ten minutes
wins.

## The core loop

```
launch → transit → listen → observe → identify → judge → commit or abort
                      ↑                                        │
                      └────────────  evade  ←──────────────────┘
```

The middle of that — observe, identify, judge — is what makes this different from a target
sandbox. It exists because hitting the wrong thing costs you, and because the game refuses to
tell you what anything is.

## Design principles

**Eyes for identification, ears for awareness.**
Vision does exactly one job: working out *what* you are looking at. Everything spatial arrives
through audio — radio chatter for friendly positions, a signal tone for the enemy operator, rotor
noise for the drone about to kill you. This is why removing the HUD works: the player is not
information-starved, they are getting it through a channel that does not compete with the thing
their eyes are busy doing. It also happens to be how a real operator works.

**Legible, not easy.**
Every hostile behaviour should be readable and therefore beatable — arcs rather than jinks, a
steady orbit rather than evasive manoeuvring, an audible drone rather than a silent one.
Difficulty comes from execution and judgement, never from being unable to perceive what is
happening.

**The player decides the battle.**
The ground war is attritional and does not resolve itself. Drone strikes are what break it. This
is both how FPV drones actually function and the only way one operator matters inside a
two-army fight.

**Cheap where it is not looked at.**
Infantry are seen at fifty metres and a hundred kilometres an hour. They must *read* correctly —
muzzle flashes, movement, casualties — not behave intelligently. The saved effort belongs in the
operator duel, which is experienced up close.

## Settled decisions

| Question | Decision |
|---|---|
| Sides | Russia and NATO, mechanically identical, differing only in appearance |
| Match length | 10 minutes |
| Win condition | Highest score |
| Player drone destructible | Yes — by enemy strikes and by infantry small arms |
| Friendly fire | Possible, and penalised |
| Civilian casualties | Penalised, for both sides symmetrically |
| Civilians | Present, and they move |
| Target HUD | None |
| Identification | By uniform markings only |
| Primary objective | Find and kill the enemy FPV operator |
| Operator kill value | Very high |
| Locating the operator | Control-signal strength, plus backtracking his drone |
| Friendly positions | Radio chatter from your own side |

## Systems

### Teams

One enum — `Russia | NATO | Neutral` — behind a shared interface, since the player pawn needs a
faction and is not a target. A single `GetAttitude(Source, Target)` function everything consults;
scattered faction comparisons drift and the first symptom is something scoring when it should
not.

**Neutral is load-bearing.** Today every target is by definition something to destroy. In battle
mode most of the world is scenery, and without a neutral value every prop added to the level
silently becomes an objective.

### Identification

The player sees no markers, labels or health bars. Sides are told apart by uniform markings
alone.

**Markings must be sized for the distance they are read at.** A stripe on a sleeve is a couple of
pixels through a 120° lens at 30 metres — not hard to see, but not physically resolvable.
Armbands, helmet covers, full-sleeve colour blocks, painted vehicle markings and position flags
all work; a thin stripe does not.

Two identification problems of very different difficulty:

- **Civilian vs military** — easy, uniform or no uniform, readable at range
- **Russia vs NATO** — hard, two uniformed groups separated by a colour detail

Penalties should reflect that. Hitting a civilian is carelessness; hitting the wrong army is
often bad luck. Weight the avoidable mistake more heavily.

**The central tension:** identification requires descending, and descending is how infantry kill
you.

### What the HUD still shows

"No HUD" means no information about the *world*. Instruments stay — a minimal OSD of the kind
real FPV goggles show: altitude, battery or timer, drones remaining, warhead state. A pilot blind
to their own aircraft is not authentic, only under-informed.

### Signal sensing

The player can sense enemy FPV control signals, and their strength varies with proximity.

**Strength only, never bearing.** A directional readout turns the hunt into following a compass
needle. Strength alone forces a search pattern — fly a leg, note the change, turn, fly another —
which is inference rather than instruction, is how real direction-finding works, and keeps the
player airborne and exposed while doing it.

**Presented as a tone, not a bar.** Rising pitch as it warms. Keeps the eyes free for the job
they already have.

**Detection is mutual.** He senses your approach as you sense his position, so closing in becomes
a race between your triangulation and his decision to move. That is what makes it a duel rather
than a search.

### Radio chatter

Friendly infantry talk about what they are doing. This replaces friendly markers, and is better
than markers would be: it is imprecise, so you still have to look; it is predictive, so you learn
where they are *going*, which is what covering from the air actually means.

**This constrains level design.** Chatter is only useful if it can refer to places — "the
hangars", "north end of the runway", "the fuel depot". The map must have named, distinct,
learnable landmarks before chatter carries any information.

### The operator hunt

The enemy operator hides — in buildings, on roofs, inside parked vehicles. Two ways to find him:

1. **Signal strength**, flown as a search pattern
2. **Backtracking his drone.** Every attack tells you the bearing it came from, which makes
   surviving an attack informative rather than merely frightening

He **relocates** after launching or after a near miss. Without that, the first time he is found
the match is decided and the remaining minutes are dead. Killing him scores heavily and buys your
side a window of free operation, rather than ending the round.

### Threats to the player

- **The enemy operator's drone.** Must be *audible before it is lethal*. An AI can intercept
  perfectly; without warning, death reads as random rather than as a mistake. Treat the audio cue
  as part of the feature, not polish.
- **Infantry small arms.** Makes low flying expensive, which is what gives the identification
  descent its weight.

### Audio priority

Four things compete for the audio channel: radio chatter, signal tone, incoming drone, own
motors. Three of those are warnings, and overlapping warnings are silence.

Incoming drone ducks everything else — it is the only one that is about to kill you.

## What carries over from the current build

| Existing | Status |
|---|---|
| Acro flight model | Unchanged |
| RC transmitter stack | Unchanged |
| Warhead, blast falloff, chain reactions | Unchanged |
| Strike camera and after-action report | Unchanged; reports become battle events |
| Explosions, debris, ragdolls | Unchanged |
| Target classes | Become units, plus faction |
| Soldier patrols | Become infantry, plus combat behaviour |
| Enemy UAVs | Become the operator's attack drones |
| Mission game mode | Becomes round management |

The flying — the hard part, and the finished part — is untouched by all of this.

## Build order

Ordered so something is playable at every stage, rather than building foundations for weeks.

**Phase 0 — Teams.** Faction enum, interface, attitude function. Everything defaults to Neutral
so nothing changes behaviour on landing. Safe to verify before any rules sit on top.

**Phase 1 — The duel.** One hidden enemy operator, signal sensing, his drone hunting you, yours
hunting him. Round timer and score. No infantry, no civilians, no chatter.

This is the vertical slice, and it is deliberately first. It tests the most novel mechanic in the
design and needs none of the AI-army work. If the hunt is fun with an empty map, everything after
is decoration on something that already works — and if it is not fun, that is worth discovering
before building two armies.

**Phase 2 — Identification.** Uniform markings, OSD-only HUD, something to misidentify. Infantry
arrive here as targets, before they arrive as combatants.

**Phase 3 — The battlefield.** Impressionistic infantry combat, radio chatter, civilians,
friendly-fire and collateral penalties.

**Phase 4 — The match.** Side selection, two bases, a purpose-built map with named landmarks.

## Open questions

- Exact marking design, and the range at which each must remain readable
- Score values, and whether the total may go negative
- Whether enemy radio can be intercepted — a strong later addition, not needed early
- How many enemy operators, and whether more than one can be active
- Whether the operator's hiding places are authored or selected at runtime

## Risks

**Scope.** This is a substantially larger game than the current build. Infantry combat and
operator AI are each bigger than anything built so far, and ten minutes of battle implies pacing
and content work well beyond one corridor of scenery.

**Identification frustration.** If markings are not readable at realistic engagement distances,
the penalties become arbitrary and the whole identification pillar collapses into annoyance.
This is the single most likely thing to go wrong, and it is testable early with static targets
before any of it is load-bearing.

**Audio overload.** Four channels of information in one sense. Needs mixing discipline from the
start rather than as a fix later.
