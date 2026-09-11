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

**Founder real-time, continued (Phase 2, arriving mid-build):** "continue the living map including
the spawned creeps make sure they have classic RTS interactions like agro chase and leash etc keep
building out the factions" → "cards should be able to tie into stuff - like there should be a card
that gives a chance to increase the number of militia emitted from frontier village by +1 so when
you play it twice villages start putting up three militia out at a time" → a card-UI redesign ask
(a Hearthstone/Clash-Royale-style draggable panel opened on **G**, hero/G-info at the top, drag
onto the battlefield to cast a spell or onto the hex grid to spawn/grow an entity) → the win
condition, resolved: "the wincon for ECOWAR - cap all of the control points - thats the base game
mode ... redgarden you have to keep up pace or come back quick at the end which may include a 5
cap - in ECOWAR the goal is to get the board into such a state that the volatility happens at the
points you need it to happen when you need it to happen - and the cards will help - like a card
could literally be capture a node - you drag it on pay the resource and it flips the base - so
ECOWAR its going to be hard to cap all of the nodes at once and thats the point it should be hard."

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

## Phase 2 (this pass): creeps, Walled Hamlet, militia-boost cards, the win condition

- `packages/livingmap/creep.h/.c` — real classic-RTS creep aggro/chase/leash/reset, hex-grid
  granularity. Directly mirrors `packages/simulation/arena_game.h`'s own proven
  `ArenaCampMinion` shape rather than reinventing it: a real DETECT-vs-HIT distinction (aggro
  range 3 hexes, attack range 1), leash measured from the creep's own HOME (never its current
  position, so it can't silently grow as a creep is kited further and further away), and a reset
  to full HP only once the creep is genuinely back home — not the instant it merely gives up.
  Movement is real but discrete: one hex step per `LIVING_MAP_CREEP_MOVE_INTERVAL_MS`, greedily
  toward the target (or home) via a new `hex_step_toward` helper (`hex_grid.h`) — a real, honest,
  narrow limitation named there: no obstacle avoidance, since nothing on this map can block a hex
  yet. A creep only ever aggros a *different* `faction_owner`'s creep (0/neutral included, treated
  as an ordinary faction value like everywhere else in this package). Generic and standalone —
  it doesn't know which town type spawned it; nothing spawns a creep yet except this phase's own
  tests and Walled Hamlet's defense (below), which *shoots* creeps, not spawns them.
- `TOWN_TYPE_WALLED_HAMLET` real behavior (`town.c`), driven by a new
  `PARENA/stdlib/ecowar/walled_hamlet_mod.prn`: slower spawn cadence and a lower raise-threshold
  than Frontier Village (economy traded for defense), a much higher baseline convert resistance
  (60 vs. Frontier Village's 20 — "slow to flip," real and measured, see
  `tests/test_walled_hamlet.c`), and genuinely new "shoots hostile creeps" behavior —
  `town_tick_with_creeps` fires the town's own stationary, garrison-scaled ranged attack at the
  nearest hostile `LivingMapCreep` within a real, garrison-scaled range/damage, on a real
  cooldown, never chasing (the town doesn't move) and never hitting its own faction.
- **Cards tie into the Living Map — a real mechanic, a real architecture gap.** `Town.militia_bonus`
  (+ `town_apply_militia_boost`) is the exact mechanic described: playing the card once adds +1 to
  every subsequent raise at that town (base 1 → 2 militia/garrison per raise), twice stacks to +2
  (→ 3), capped at `TOWN_MILITIA_BONUS_CAP` (20 — a real, deliberate balancing bound against
  unbounded stacking, not a tuned final number). Fully real and tested
  (`tests/test_walled_hamlet.c`). **The real, honest gap**: `town_apply_militia_boost` has no
  caller from a live match yet, because no running match anywhere in this repo initializes a
  `HexGrid`/`TownRegistry` at all — Phase 1/2 are still headless (see "Not done, honestly" above).
  Wiring an actual `apps/arena` card cast to call it needs (a) a live Living Map instance attached
  to a real match, and (b) a real decision on how arena teams map to the 3 Living Map factions —
  both genuinely open, not guessed at here. The founder's own **"capture a node" card idea needs
  zero new Living Map mechanics to work once that bridge exists** — `town_attempt_convert`
  (Phase 1) already takes an arbitrary `attempt_strength`; a card that "pays a resource and flips
  the base" is just a call to it with a strength large enough to clear resistance and cross 100 in
  one shot. Worth building the arena↔living-map bridge around that existing function, not a new
  one.
- **Card UI redesign — real founder direction, captured, not built this pass.** A panel opened on
  **G** (hero/G-ability info at the top, matching the existing single-key HUD convention
  `docs/ARENA_API.md`'s own card-input path already established), then a Hearthstone/Clash
  Royale-style draggable card interface: drag onto the battlefield to cast a spell (the existing
  16-card system's own shape), or drag onto the hex grid to spawn/grow an entity — affordance name
  genuinely undecided per the founder's own "not sure what that affordance should be called."
  Real, honest reason this isn't built in this same pass: it's client-side SDL2/OpenGL rendering
  and input work in `apps/arena`'s existing (large, unread-in-this-pass) card/HUD code, with no
  display available in this sandbox to visually verify it — the same real constraint every other
  ECOWAR visual change in this repo already names. Real, concrete next step for whoever picks this
  up: read the existing card HUD tile code (`docs/ARENA_API.md`'s own card-input path) before
  designing the drag interaction, rather than building a parallel one.
- Tests: `tests/test_creep.c`, `tests/test_walled_hamlet.c` (also covers `militia_bonus` and the
  win condition below), both headless, wired into `scripts/test_livingmap.sh` and
  `tests/BUILD.bazel`.

**Win condition — resolved.** Founder: "the wincon for ECOWAR - cap all of the control points -
thats the base game mode." `town_registry_faction_has_full_control` (`town.h/.c`) is the real,
tested implementation: a faction wins by owning every currently-active `Town` at once (an empty
board, or neutral/faction 0, can never "win"). A deliberate, real contrast with REDGARDEN's own
pace/comeback-at-the-end dynamic (which can include a late 5-cap) — here the design intent is that
capping every node simultaneously is genuinely hard, on purpose, and cards (the capture-node idea
above) are the tool for engineering *when and where* a node flips rather than leaving it to
whoever happens to be standing there. Other ECOWAR game modes (e.g. "resource race") are named as
real, later, separate work, not designed here. "Control points" is read as **towns**, not every
one of the map's 469 hex cells including empty terrain — see `town_registry_faction_has_full_control`'s
own header comment for why, and for the real, deliberate exception this reading would need to be
revisited under.

## ML training throughput, and a real, open architecture fork

Founder real-time: "as soon as some of these pieces are in place we are going to need to do a lot
of ML unsupervised training to get the bot to a place where it knows how to play the game" —
ECOWAR's bot needs REDGARDEN's own existing heuristics+AI baseline (a decent default-difficulty
opponent) adapted to interact with friendly troops/creeps rather than teammates, but the Living
Map adds far more state than REDGARDEN's hero-vs-hero MOBA ever had, so training leans harder on
unsupervised crunching and needs real simulation throughput.

**Real, checked-first finding: REDGARDEN/ECOWAR already have a fast, in-process training harness**
for the hero-combat side — `apps/arena_training/src/headless.c`'s `sim_init`/`sim_step`/
`sim_get_state` API (consumed by `scripts/rl_env.py`) already ticks `arena_update`/
`arena_update_teams` directly via plain C function calls, with zero real-time throttling of its
own — a Python training loop can already call `sim_step` as fast as the CPU allows. Nothing new
was needed there. What the founder's own "faster ticks via a CLI flag" ask exposed as a real,
separate gap: `apps/arena_server` (the real, live, *networked* match server real players and the
matchmaker's own bot pool actually use) paces itself with a hardcoded `usleep(16000)` per tick —
fine for real players, a real bottleneck for generating training data through the actual
network/matchmaker/lobby path (distinct from `headless.c`'s in-process shortcut, which skips that
path entirely and so can't exercise it).

**Shipped this pass**: `apps/arena_server` gained `--tick-ms <N>` (how much game time each tick
simulates — "fewer frames" for the same match duration) and `--fast-forward` (skip the real-time
sleep once a match is actually LIVE, so ticks run back-to-back as fast as the CPU allows — "ticks
happen faster"). Both default to today's unchanged behavior (16ms, real-time-paced) — the live
`ecowar-matchmaker.service` deploy is unaffected unless one of these is explicitly passed. A real,
deliberate safety boundary: `--fast-forward` only ever skips the sleep once `match_phase ==
ARENA_PHASE_LIVE`, never during the WAITING-for-players phase — a real UDP handshake still needs
real wall-clock time regardless of how fast the eventual match will simulate, and skipping the
sleep there too would make the existing 60-second lobby-fill timeout fire almost instantly (found
and fixed before shipping, not by observation in production). Live-verified: started a real
`red_garden_arena_server --fast-forward --tick-ms 32` process, confirmed it printed its real
startup log and stayed alive past the WAITING phase's real timeout window rather than exiting
prematurely.

**Not built this pass**: an equivalent fast headless harness for the Living Map itself
(`packages/livingmap` has no AI/bot decision-making to train yet, so there's nothing to point a
training loop at — its `town_tick`/`creep_tick_all` functions are already plain C calls with zero
throttling of their own, same shape `headless.c` already proved out for the arena side, whenever
that's needed).

## The hero-as-NPC / card-battler experiment (S378) — green-lit, real, in progress

Founder: "we may need to make the hero an NPC actually and convert it fully into a card battler —
like choosing a hero puts that hero in your starting hand and shuffles the hero abilities cards
into your deck," followed by a real, explicit go-ahead once the idea was named: "lets experiment
with making the hero cards based and if it ends up being unfun we bring back classic RTS/MOBA hero
affordances in order to increase player agency." Read exactly as framed — a real, reversible
experiment with an explicit rollback trigger tied to fun/player-agency, not a one-way architecture
commit. Every piece below defaults OFF everywhere; nothing in any live client sets it yet, so this
is real, tested, additive architecture, not a live behavior change.

**Real, checked-first finding: the two hardest-sounding pieces already existed.** `arena_game.c`'s
own `arena_bot_tick_heuristic`/`bot_cast_kit_if_ready` (built for the practice-mode bot and RL
training opponent) are ALREADY hero-index-agnostic in their real math (relative dx/dz/dist/hp-diff
inputs, a per-hero-id cast switch) — only their own thin wrapper hardcoded "always hero 1 vs hero
0." So "hero becomes an NPC" needed no new AI, just `arena_npc_hero_tick(hero_index, foe_index,
dt_ms)` — a real, generalized merge of both functions, callable for ANY hero (including a human
player's own owner-0 hero in card-battler mode).

**Shipped this pass**:
- `ArenaHero.npc_controlled` (default 0) — when set, `arena_update`'s own per-tick loop drives
  that hero via `arena_npc_hero_tick` instead of expecting player input. `apps/arena_server`'s
  `PACKET_ARENA_MOVE`/`PACKET_ARENA_CAST` handlers both gate on it too, so a stray real command for
  an NPC-controlled hero is a real, explicit no-op rather than a race the AI happens to win.
- `packages/simulation/card_deck.h/.c` (new): real Deck/Hand mechanics, Clash-Royale-style
  continuous draw (matching ECOWAR's own real-time economy, and the founder's own
  "Hearthstone/Clash Royale-style" card-UI framing better than Hearthstone's turn-based full-hand
  draw would). One copy of every real card id (the 16 existing `ECOWAR_CARDS` plus
  `CARD_ID_HERO_Q`/`_W`/`_R` — "the hero abilities cards shuffled into your deck," literally), a
  4-card hand, playing a card empties its slot and starts a real 3-second redraw. A real,
  deliberate simplification named honestly: an endless, recycling shuffled sequence, not a
  draw-pile/discard-pile model or curated deck-building — real, separate, later work if the
  experiment sticks.
- `packages/simulation/card_battler.h/.c` (new): the per-owner registry connecting a Deck/Hand to
  real gameplay. `card_battler_play_slot` resolves a generic card id via the EXISTING
  `arena_ecowar_play_card` (unchanged), and a hero-ability card id via that hero's own EXISTING
  `arena_cast_q`/`arena_toggle_w`/`arena_cast_r` — every hero's real kit implementation reused
  as-is, zero new per-hero content authored.
- 24 new tests (`tests/test_card_battler.c`), including a real live round-trip proving a
  `CARD_ID_HERO_Q` play genuinely starts that hero's own real ability cooldown (not a stub), and
  that `arena_npc_hero_tick` is a real no-op for a dead hero. Full suite green (3145 assertions,
  `scripts/test_arena.sh`); `scripts/build.sh`/`scripts/build_arena.sh` both clean.

**Not built this pass, real and named**: no client UI wiring (playing a card is still only
reachable by a test calling `card_battler_play_slot` directly — the G-key panel from Phase 2's own
card-UI section is the real, separate next step here too), no `card_battler_init_hero`/
`card_battler_tick` call sites wired into any live match loop, no balance pass on hand size/redraw
timing/which cards even make sense drawn randomly (a "capture a node" card drawn with no hover
target, or a Doc Wheel heal card with no living ally, both already whiff safely today via their
own existing real no-op conventions — not a new gap this pass introduces). **The real rollback
path, kept cheap on purpose**: `npc_controlled` defaults to 0 and every existing direct-control
code path (WASD move, Q/W/R key cast) is completely untouched and still the only path exercised by
any real match today — "bring back classic RTS/MOBA hero affordances" requires deleting nothing,
just never flipping the flag.

## Cows (real, built)

Founder, terse: "add cows." No further detail given — interpreted in context (a real, well-known
RTS-map convention: harmless neutral critters that give a map real, visible "life" without being a
gameplay threat, same spirit `packages/livingmap`'s own name already commits to) as: passive,
wandering, neutral wildlife on the hex grid.

- `creep_spawn_cow` (`creep.h/.c`) spawns a real cow: low HP (`LIVING_MAP_COW_HP`), neutral faction
  (`LIVING_MAP_COW_FACTION_OWNER` = 0), and a new `LivingMapCreep.passive` flag.
- A passive creep's own `IDLE` tick wanders instead of aggro-scanning (`creep_tick_wander`) — a
  plain, deterministic step pattern (no PRNG needed for a purely cosmetic wander), bounded to
  `LIVING_MAP_COW_WANDER_RADIUS` hexes from home, same "measured from home" convention the real
  leash range already uses.
- A cow never fights back and never initiates aggro — but it isn't immune to being targeted: its
  faction_owner (0) is a real, ordinary value like any other, so a real faction creep's own
  existing aggro scan can still find and kill one. This falls out of the existing aggro rule for
  free, no special-casing needed — real emergent behavior (a hostile creep can incidentally farm a
  cow), not a bug.
- New `LIVING_MAP_EVENT_CREEP_WANDERED` event, fired only on a tick that actually moves the cow —
  keeping faith with "everything that happens needs to announce events" for this too, not quietly
  skipping it because it's minor.
- 21 new tests (`tests/test_cow.c`), including a determinism check (two cows given the identical
  tick sequence wander to the identical position) and a real kill-a-cow round trip. Full Living Map
  suite green (132 assertions).

Not decided or built: no death reward/loot, no visual/asset direction, no relationship yet to
Jungle Enclave's own still-unbuilt "spawns hunters" (a real, later, open question — are hunters and
cows the same creep-spawning surface, or separate concepts?).

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

## The 4 town types (2 of 4 built)

| Type | Founder's own description | Status |
|---|---|---|
| Frontier Village | Spawns peasants → militia. Avoids conflict. Converts easily. | **Built** (Phase 1) |
| Walled Hamlet | Defensive bias. Shoots hostile creeps. Slow to flip. | **Built** (Phase 2) — real creep-defense fire via `packages/livingmap/creep.h/.c` |
| Jungle Enclave | Symbiotic with creeps. Spawns hunters. Expands naturally. | Named only — needs a real relationship to the now-real `creep.h/.c` system above (spawning hunters as `LivingMapCreep`s is a plausible fit, not yet decided) |
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
whoever scopes Phase 3 (Corruption/Blighted Settlement) to confirm, adjust, or replace once actual
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

## Win condition — see "Phase 2" above, resolved

Was genuinely undecided as of Phase 1 ("im not sure what the win condition is to be honest with
you"); resolved in Phase 2 to Full Control (own every active town at once), real and tested via
`town_registry_faction_has_full_control` — see the Phase 2 section above for the full reasoning
and the real, deliberate contrast with REDGARDEN's own pace/comeback dynamic.

## Phased plan

1. **Hex grid + Frontier Village** (Phase 1) — real, tested, standalone.
2. **Walled Hamlet + creeps + militia-boost cards + win condition** (Phase 2, this pass) — real,
   tested; see the Phase 2 section above. Card-UI redesign and the arena↔living-map live-wiring
   bridge are real founder direction, captured but not built this pass.
3. Corruption mechanics (pick one of the 3 staged options below) + Blighted Settlement, once a
   real decision is made on how corruption actually works.
4. Jungle Enclave — spawning "hunters" as real `LivingMapCreep`s (Phase 2's new system) is a
   plausible fit, not yet decided as the final shape.
5. The 3 factions as real AI agents contesting hex cells ("3 bots playing our version of
   starcraft") — the rock-paper-scissors hypothesis below gets its first real test here.
6. Tech tree doctrines, "pick 2 max," end-tech capstones (Citadel Node / Living Bastion /
   Cataclysm Beacon).
7. The arena↔living-map live-wiring bridge: a real running match that actually initializes a
   `HexGrid`/`TownRegistry`, a real team-to-faction mapping, and the actual card-cast call sites
   (`town_apply_militia_boost`, `town_attempt_convert` for "capture a node") — the real
   prerequisite for every card-tie-in idea in Phase 2 to matter in a live game, not guessed at.
8. Card UI redesign (G-key panel, Hearthstone/Clash-Royale-style drag-to-cast/drag-to-hex-grid
   affordance) — real client rendering work, gated on reading the existing card HUD code first.
9. Visual factions (Imperatives/Verdant Pact/Ascended) — art/asset direction, deferred until the
   gameplay factions above have real, distinguishable behavior worth skinning.

## Related

- `docs/NORTHSTAR_MAP_EDITOR.md` — the map-tooling epic this sits next to; Phase 3 of that doc
  ("a real, separate, loadable map data format") and this doc's own hex grid are two different,
  currently-unconnected map concepts — worth reconciling once both are further along, not now.
- `docs/ARENA_API.md` — the real, existing PARENA mod ABI this doc's "Mod event model, honestly"
  section is grounded in.
- `PARENA/NORTHSTAR.md` — VS0's own real Definition of Done; the "no function pointers/closures"
  limit this doc's event-model section depends on.
