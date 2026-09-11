# ECOWAR — Living Map: Hex Grid, Towns, Factions (NORTHSTAR)

Registered as `ECOWAR-LIVINGMAP-NORTH`. Scoping doc for BACKLOG.md SECTION 377.

## Founder real-time direction (verbatim shape, not paraphrased away)

"add towns - start with the entire map is divided into cells (hex grid) - so we can start to get
the living map stuff more formalized" → four town types (Frontier Village, Walled Hamlet, Jungle
Enclave, Blighted Settlement) → three warring factions (Dominion/RTS-classic, Symbiosis/roguelike,
Corruption/high-APM), each with an end-tech capstone → a doctrine-based tech tree ("pick 2 max per
match") → three visual factions (Imperatives, Verdant Pact, Ascended) mapped onto the same three
gameplay factions → "i think these are like 3 warring factions ... 3 bots are playing our version
of starcraft ... trying to gain territory" → "the ECOWAR game is all about full spectrum warfare
... playing all sides ... im not sure what the win condition is to be honest with you" → "start
with the frontier village" → "PARENA MODS FIRST — should plug into all of these entrypoints...
everything that happens in the game needs to announce events and then mods can subscribe."

This doc names the real, honest scope of what that turns into, and what's actually buildable
today given ECOWAR's current engine and VS0's (PARENA's current compiler) real limits — same
discipline DEADWEIGHT's/MIXFORGE's own NORTHSTAR docs already apply to their own big asks.

## Real, checked-first finding: this is a new layer, not an extension of the arena map

`packages/simulation/arena_game.c`'s existing map is `ARENA_NODE_COUNT` (9) fixed capture nodes on
a continuous float x/z plane (`ARENA_HALF_EXTENT`), built for 1v1/team MOBA matches — see
`docs/NORTHSTAR_MAP_EDITOR.md`'s own "map is hardcoded C constants" finding, still true. "The
entire map is divided into cells" describes a different, coarser structure than that: a strategic
layer sitting over the whole map, sized for towns/territory/factions, not hero-vs-hero combat
nodes. Building it as a **new, standalone package** (`packages/livingmap/`) rather than bolting
hex cells onto `ArenaNode` keeps the existing, live, tested MOBA sim untouched while this new
layer is still being found — the same "additive, doesn't replace" precedent NORTHSTAR §13 already
set for `apps/arena` next to `local_game.c`. Wiring the two together (does a captured hex cell
spawn an arena node? does a town's militia become an arena-side unit?) is a real, later, explicitly
open integration question, not assumed here.

## Phase 1 (this pass): hex grid + Frontier Village — real, built, tested

- `packages/livingmap/hex_grid.h/.c` — axial-coordinate (flat-top) hex grid, real math (cube-coord
  distance/rounding, redblobgames' standard formulas — not reinvented), a fixed hex-radius-12 map
  (`HEX_MAP_CELL_COUNT` = 469 cells) with world-space ↔ hex conversion so a hex size can be tuned
  against `ARENA_HALF_EXTENT` later without changing the grid math itself. Each `HexCell` carries
  `faction_owner` (0 = neutral, 1..3 = Dominion/Symbiosis/Corruption) and `town_id` (-1 = none) —
  the two fields the rest of this doc's systems actually need today; `corruption` is reserved
  (see "Corruption, honestly" below) but not yet driven by any real system.
- `packages/livingmap/living_map_events.h/.c` — the real event-announcement layer (see "Mod event
  model, honestly" below): an always-on, append-only ring buffer (same shape as
  `redgarden/combat_log_mod.prn`'s own `ArenaCombatLogEntry`) that every lifecycle transition
  writes into, readable by any future mod or tool.
- `packages/livingmap/town.h/.c` — `Town` struct + registry. `TownType` names all 4 real types
  from the founder's list, but **only `TOWN_TYPE_FRONTIER_VILLAGE` has real behavior** — the other
  3 are declared enum values with a doc comment pointing back here, not stub structs pretending to
  be implemented. Per-town: population (peasants), militia, a spawn timer, and conversion
  progress.
- `PARENA/stdlib/ecowar/frontier_village_mod.prn` — the real PARENA decision logic for Frontier
  Village's own named behavior ("Spawns peasants → militia. Avoids conflict. Converts easily."):
  spawn cadence that accelerates with population, a deterministic peasant→militia promotion rule,
  and a real, low conversion-resistance formula. I32-only the whole way (population/militia/ms are
  all plain ints), so this is `card_effect_mod.prn`'s "real decision logic, not just a trigger"
  tier, not the older trigger-only shape — same real "push logic into PARENA wherever VS0 already
  allows it" instruction the founder gave for ECOWAR generally.
- Tests: `tests/test_hex_grid.c`, `tests/test_town_frontier_village.c`, both headless (no
  SDL/GL), run via `scripts/test_livingmap.sh` and wired into CI as their own step, same pattern
  `scripts/test_arena.sh` already established for the arena sim.

**Not done, honestly**: no wiring into `apps/arena`/`apps/arena_server`'s live match loop, no
rendering, no AI/bot behavior deciding where to found a town, no save/load. This phase proves the
data structures and the mod ABI end-to-end with real tests, matching the exact "callable and
tested end-to-end today, real UI/live wiring is separate, later work" bar `card_effect_mod.prn`
itself shipped at.

## Mod event model, honestly

The founder's ask — "everything that happens in the game needs to announce events and then mods
can subscribe to those events and register functions to be called by the engine" — describes a
genuine **dynamic, multi-subscriber observer pattern**. Checked directly against VS0's own real,
current emitter (`src/emit.c`, `PARENA/NORTHSTAR.md`'s own Definition of Done): VS0 has no
function pointers, no closures, and no `Vec`-of-function values — every one of the 9 real mods in
this monorepo today (bloodflower, tree passive, build template, item curriculum, Duck's smoke
bomb, Abraham's fireball, ECOWAR's own card effect, combat log, bacon-puck speed) works by the
engine calling ONE specific, compiled-in PARENA function **by its literal C name** at ONE
hand-written call site. That's real "the mod is the trigger, host does the work" wiring, not
runtime registration, and it's a hard compiler ceiling today, not a design choice this doc is
making.

What's real and buildable now, and what this phase actually ships:

1. **Every lifecycle transition announces itself for real** — `living_map_events.c`'s ring buffer
   is written on every town-founded/tick/spawn/convert-attempt/converted event, unconditionally,
   whether or not any mod cares. Any future mod, tool, or the eventual map editor can read the log
   and see exactly what happened, in order — this is the real, working half of "announce events."
2. **A mod still "runs when the thing happens"** exactly like every other mod in this repo:
   compiled in, called by name, at the one real call site (`town.c`'s `town_tick`/
   `town_attempt_convert` call straight into `on_frontier_village_spawn_interval_ms` etc.) — real,
   live, not a stub.
3. **True dynamic subscription (multiple independent mods registering for the same event without
   the host being edited) is not real yet** — it needs VS0 function-pointer/closure support that
   doesn't exist. Named here as a real, tracked follow-up (add to `PARENA/NORTHSTAR.md`'s own
   backlog of emitter gaps once VS0 grows past today's scalar-only ABI), not silently assumed away
   or faked with something that looks dynamic but isn't.

## The 4 town types (only Frontier Village built this phase)

| Type | Founder's own description | Status |
|---|---|---|
| Frontier Village | Spawns peasants → militia. Avoids conflict. Converts easily. | **Built** (Phase 1) |
| Walled Hamlet | Defensive bias. Shoots hostile creeps. Slow to flip. | Named only — needs a real ranged-attack/aggro model this phase doesn't build |
| Jungle Enclave | Symbiotic with creeps. Spawns hunters. Expands naturally. | Named only — needs a real relationship to `arena_game.c`'s own creep system (a cross-package integration question, not scoped here) |
| Blighted Settlement | Corrupted over time. Spawns cultists. Unstable, explosive outcome. | Named only — blocked on "Corruption, honestly" below |

## The 3 factions and the rock-paper-scissors question

Founder: "not sure what corrupted means but it is a core part of the game of life part of the
living map i think — maybe units have different behavior if they become corrupted and the
corruption can spread either from the environment or between npcs and then there has to be some
balancing force? maybe we can set up some kind of rock paper scissor system with the 3 factions."

Read as real, open design work, not a gap to guess through:

- **Dominion (RTS-classic)** — faster production, stronger structures, hard borders. End tech:
  Citadel Node (locks a cell permanently — i.e. immune to conversion/corruption both).
- **Symbiosis (roguelike/Diablo)** — cells heal, villages auto-align, creeps become allies. End
  tech: Living Bastion (a base that *moves* — the one faction whose territory isn't static).
- **Corruption (high-risk/high-APM)** — viral spread, hijacks pillagers, chain reactions. End
  tech: Cataclysm Beacon (rewrites local rules).

A real, coherent rock-paper-scissors reading of the three end-techs, offered as a starting
hypothesis, not a decision: **Dominion's hard-locked Citadel cells resist Corruption's spread but
can't heal or reposition once placed → Symbiosis's healing/auto-align out-sustains a slow Dominion
siege but has no hard defense against a fast Corruption chain-reaction hijack → Corruption spreads
fastest through Symbiosis's own densely-interconnected, auto-aligned territory but burns out
against Dominion's locked, non-adjacent cells it can't get a foothold in.** This is this doc's own
proposed *shape* for the balancing force the founder asked about — a real, testable hypothesis for
whoever scopes Phase 2 (Corruption/Blighted Settlement) to confirm, adjust, or replace once actual
faction AI exists to test it against, not something to hard-code as game rules yet.

**"Corruption" mechanically, staged as three honest options for that same later pass:**
1. A per-hex-cell `corruption` value (already reserved in `HexCell` above) that rises near an
   existing corrupted cell/town and falls near Dominion/Symbiosis presence — pure environmental
   spread.
2. A per-unit/per-town flag that can flip based on the cell's corruption crossing a threshold —
   pure agent-level contagion.
3. Both at once (cell corruption drives the threshold that flips agents, flipped agents raise
   their own cell's corruption) — the most "living map" reading of the founder's own phrase, and
   this doc's own lean, but explicitly not decided or built here.

## Tech tree — doctrines, "pick 2 max per match"

Three vertical, non-branching doctrine paths (Imperatives/cubes-and-slabs, Verdant
Pact/rounded-organic, Ascended/tall-spires) mapped 1:1 onto the three gameplay factions above.
"Pick 2 max" is a real, load-bearing match-setup rule (not just flavor) — this doc names it and
defers the actual tech-node list to whoever scopes it once Dominion/Symbiosis have enough real
behavior for a doctrine choice to mean anything gameplay-wise. No code this phase.

## Win condition — genuinely undecided, named not guessed

The founder said so directly: "im not sure what the win condition is to be honest with you." Not
resolved here. Territory-percentage control, faction-elimination, and a Citadel/Bastion/Beacon
capstone race are the three obvious candidates given the end-techs above, offered as a starting
list for whoever picks this up next, not a decision made in this pass.

## Phased plan

1. **Hex grid + Frontier Village** (this pass) — real, tested, standalone.
2. Walled Hamlet — needs a real aggro/ranged-attack model against hostile creeps; first town type
   that has to reach into something creep-shaped.
3. Corruption mechanics (pick one of the 3 staged options above) + Blighted Settlement, once a
   real decision is made on how corruption actually works.
4. Jungle Enclave — real integration question against `arena_game.c`'s own creep system or a new
   living-map-local creep concept; not yet scoped which.
5. The 3 factions as real AI agents contesting hex cells ("3 bots playing our version of
   starcraft") — the rock-paper-scissors hypothesis above gets its first real test here.
6. Tech tree doctrines, "pick 2 max," end-tech capstones (Citadel Node / Living Bastion /
   Cataclysm Beacon).
7. Win condition, decided from real play data once 5-6 exist, not guessed now.
8. Visual factions (Imperatives/Verdant Pact/Ascended) — art/asset direction, deferred until the
   gameplay factions above have real, distinguishable behavior worth skinning.

## Related

- `docs/NORTHSTAR_MAP_EDITOR.md` — the map-tooling epic this sits next to; Phase 3 of that doc
  ("a real, separate, loadable map data format") and this doc's own hex grid are two different,
  currently-unconnected map concepts — worth reconciling once both are further along, not now.
- `docs/ARENA_API.md` — the real, existing PARENA mod ABI this doc's "Mod event model, honestly"
  section is grounded in.
- `PARENA/NORTHSTAR.md` — VS0's own real Definition of Done; the "no function pointers/closures"
  limit this doc's event-model section depends on.
