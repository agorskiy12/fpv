# Teams: how to build it

Teams look like a small feature and are not. Almost nothing in the game currently asks *whose*
anything is, so allegiance has to be threaded through damage, scoring, the HUD, objectives and
every future AI decision. Getting the shape right first is worth more than getting it in fast.

## 1. Where allegiance lives

A single enum, on one shared interface, implemented by everything that can belong to a side.

```
EFaction : Russia | NATO | Neutral
```

**Neutral is not padding — it is the value that makes the rest work.** Right now every
`ADroneTarget` is by definition a thing you are supposed to destroy. In battle mode most of the
world is scenery: the runway, buildings, parked equipment. Those need to be destructible without
being worth points or counting toward anything. Without a neutral value, every prop you add
becomes an objective by accident.

Implement it as a small interface rather than a property on `ADroneTarget`, because the **player
pawn also needs a faction** and it is not a target. Two classes, one question, one interface.

Worth also implementing UE's `IGenericTeamAgentInterface` on top of it. It costs a single
function, and it is what UE's AI perception uses to decide friend from foe — which step 4 of the
battle plan will want. Adopting it now avoids retrofitting perception later.

## 2. One rule, in one place

Every consumer asks the same question, so it should exist exactly once:

```
EAttitude GetAttitude(Source, Target) -> Friendly | Hostile | Neutral
```

Anything scattering `if (FactionA != FactionB)` around the codebase will drift, and the first
symptom will be something scoring when it should not. One function, everything calls it.

## 3. What has to consult it

This is the real extent of the work, and it is larger than it sounds:

| System | Change |
|---|---|
| `ApplyBlast` | Damage stays indiscriminate; **scoring** becomes faction-aware. A blast should not politely spare friendlies |
| Scoring | Enemy kills score, friendly kills penalise, neutral kills are free |
| Objectives | Only hostile units count toward the round |
| HUD markers | Coloured by attitude, so a target is identifiable before you commit |
| Strike report | Already lists kills; now flags friendly ones |
| Enemy operator | Needs to know which units are *its* enemies -- the same rule, run from the other side |
| Infantry | Same, later |

The HUD one matters more than it looks. If you cannot tell friend from enemy through a 120°
lens at speed, friendly fire stops being a mistake and becomes a coin toss.

## 4. The player's faction

Lives on `APlayerState`, chosen before the match. That is where UE expects it and it costs
nothing now, while being the only sane place once there is a menu or a second player.

The pawn reads it from there rather than owning it, so respawning cannot lose it.

## 5. Decisions needed before building

These change the work materially, so they are yours to make first:

**Can the player's drone be destroyed by an enemy strike?**
Currently nothing can hurt it. If enemy operators are to be a real threat, the pawn needs to take
blast damage — which means it stops being purely a delivery system and starts being something
that can be lost. I would say yes, and that it is most of what makes the operator duel work.

**Is friendly fire penalised, or impossible?**
Recommendation: possible, and penalised. Blast physics that check allegiance feel arbitrary, and
the risk of hitting your own line as it advances is exactly the tension that makes supporting
infantry interesting rather than automatic.

**Do neutral casualties cost anything?**
Simplest is no — neutral means scenery. But if civilian structures ever score negatively, that
decision is much cheaper to make now than to retrofit.

## 6. Order of work

1. Faction enum, interface, and the single attitude function
2. `ADroneTarget` and `AFPVDronePawn` implement it; everything defaults to Neutral so nothing
   changes behaviour on day one
3. Scoring and objectives consult attitude
4. HUD markers colour by attitude
5. Player faction onto `APlayerState`, defaulted until there is a menu
6. Existing test spawns assigned factions, so there is something to fight

Steps 1 and 2 are deliberately inert — everything neutral, nothing behaves differently. That
makes them safe to land and verify before any rule changes on top.

## 7. What this unlocks

Once attitude exists, several things stop being special cases:

- Enemy operators pick targets by asking the same question the player answers by eye
- Infantry find enemies with the same call
- The strike report can separate hostile kills from friendly ones for free
- Round scoring is two counters over one rule rather than bespoke logic

That is the argument for doing it properly rather than bolting a boolean onto the target class:
everything after this depends on it, and it is the cheapest it will ever be to get right.
