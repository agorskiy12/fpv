# Battle mode: two sides, ten minutes

## The short version

The concept is good and most of it is achievable on top of what exists. One element is the best
idea in the proposal and should be built early. One risk could sink the whole thing and needs
designing around before any code is written.

## The best part: enemy FPV operators

This is the standout, and it is worth building before almost anything else.

Right now **nothing in the game threatens the player.** You fly, you pick a target, you hit it.
Failure means a wasted drone, never danger. That is the flattest thing about the current build.

Enemy drones hunting you changes the flight model from a skill into a duel. Suddenly altitude is
exposure, speed is survival, and the acro handling that already exists has a defensive purpose.
It also costs comparatively little: an enemy UAV that seeks, closes and detonates is a modest
step from the arcing patrol already implemented.

If only one thing from this proposal gets built, make it this.

## The biggest risk: the player does not matter

Ten minutes, two AI armies, and one drone operator. The failure mode is obvious once stated:
**the bots fight the battle and the player watches.** Kill twenty soldiers out of two hundred and
the scoreline barely moves; the player becomes a spectator with a hobby.

This is a design problem, not a technical one, and it has to be answered before building.

The strongest answer is also the most realistic one:

> **The ground war is a stalemate. Drones break it.**

Make the infantry lines attritional and slow — dug in, trading fire, neither side advancing on
its own. Then drone strikes are the only thing that shifts anything. That mirrors how FPV drones
actually function in modern warfare, and it makes the player decisive without inflating their
kill count.

Concretely, several levers:

- **Only drones can reach certain targets.** Artillery, supply, command posts, dug-in positions.
  Infantry cannot touch them; the player is the only instrument that can.
- **Kills that unlock advances.** Destroy the position pinning your side and your infantry moves
  up. The player sees the front line move *because* of them.
- **Losses hurt.** A limited drone supply, so a wasted sortie costs the team something real.
- **The enemy operator is a genuine rival.** Their strikes shift the line back. It becomes an
  operator duel with two armies as the scoreboard.

Any one of these helps. The combination turns the player from participant into the deciding
factor.

## Second risk: scoring shape

"Kill as many as you can in ten minutes" is a fine skeleton but flat in practice — no arc, no
comeback, no tension curve. Every minute weighs the same as every other.

Worth considering instead:

- **Attrition with reinforcements.** Each side has a finite pool. Draining it to zero wins early;
  otherwise the higher score at ten minutes takes it.
- **Weighted targets.** A supply truck is worth ten riflemen. Now the player is making choices
  rather than hunting whatever is closest.
- **A contested middle.** Something both sides want, so the fight has a location rather than
  being spread evenly.

Attrition pools are the cheapest of these and add the most, because they give the final minutes
stakes.

## Third: keep soldier AI cheap

The instinct will be to build proper combat AI. Resist it.

The player is fifty metres up doing a hundred kilometres an hour. At that distance and speed,
**infantry only has to read correctly, not behave intelligently.** Muzzle flashes, movement
between cover, men going down, a line that advances or falls back. Nobody will ever evaluate
their tactics, because nobody is looking at them for more than two seconds at a time.

A convincing crowd is perhaps a fifth of the work of a competent shooter AI and is
indistinguishable from the air. Spend the difference on the drone duel, which is what the player
actually experiences up close.

## Factions

Keep the two sides **mechanically identical** and differentiated only by appearance — uniforms,
vehicles, markings. Asymmetric factions mean balancing two games instead of tuning one, and
that cost lands squarely on the least interesting part of the project.

Practically, it also doubles asset shopping: every vehicle, uniform and prop needs a counterpart.
Worth budgeting for.

## What carries over

Nearly all of it, which is the good news:

| Existing | Status in battle mode |
|---|---|
| Flight model | Unchanged |
| Transmitter stack | Unchanged |
| Warhead, blast, chain reactions | Unchanged |
| Strike camera and report | Unchanged, and better -- reports become battle events |
| Explosions, debris, ragdolls | Unchanged |
| Target classes | Become units, plus a team allegiance |
| Soldier patrols | Become infantry, plus combat behaviour |
| Enemy UAVs | Become enemy operators, plus seek-and-strike |
| Mission game mode | Becomes round management: timer, two scores, win condition |

The genuinely new work is teams, combat behaviour, the enemy operator, and the round structure.

## Suggested build order

Deliberately ordered so the game is playable and interesting as early as possible, rather than
building foundations for weeks.

1. **Teams.** A faction on every unit, friendly fire rules, team-coloured HUD markers. Touches
   everything, so it goes first.
2. **Enemy FPV operator.** The single biggest change to how the game feels. Threat, immediately.
3. **Round structure.** Ten-minute timer, two scores, attrition pools, end screen. Makes it a
   match rather than a sandbox.
4. **Infantry combat, impressionistic.** Two lines that advance, shoot, and take casualties.
5. **Side selection and a two-base map.** Last, because until the above works there is nothing to
   pick a side *for*.

Steps 1 to 3 alone give a playable, tense game: your drone against theirs over a static
battlefield. That is worth reaching before committing to step 4.

## Scope, honestly

This is a substantially larger game than the current one. The existing build is a sandbox with
targets; this is a match with two AI armies and an opposing player-equivalent.

The flying — which is the hard part, and the part already finished — carries over untouched.
But infantry combat and enemy operator AI are each larger than anything built so far, and
"ten minutes of battle" implies content and pacing work well beyond a single corridor of
scenery.

Worth pursuing. Worth doing in the order above so there is something playable at every stage
rather than a long stretch with nothing to fly.
