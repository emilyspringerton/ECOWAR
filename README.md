# ECOWAR

A hard fork of REDGARDEN, full history preserved, diverging forward from here as its own project
— same real precedent `shankpit-460` already established for `SHANKPIT`.

## What this is (current, 2026-08-27 — supersedes the 2026-08-20 scoping below)

Founder real-time, this session: "ok start working on ecowar in the redgarden repo" → "we need
separate bot pool 1v1 separate matchmaking separate client and server separate artifacts
everything we need 16 hallucinated cards from tyler hero bible with promptoverse art" → "more
specific mechanic direction to follow for now build what is obvious" → "the framework should
deeply embed parena and the ideas from the redgarden map editor" → "mod api first parena mod dev"
→ "do the whole game in pure parena as much as you can" → "actually lets do a hard fork from
redgarden repo wise" → "ECOWAR repo" → "either its there or use your token to create it" → "finish
what ur working on then iterate."

**A real, honest pivot from the 2026-08-20 scoping below, not silently overwritten**: the original
plan named `GoblinFoxDragon/apps2/battlegrounds_gui` as the interface fork source, with REDGARDEN's
newer features (bot AI, items, WASD movement) ported on top. This session's real-time direction
explicitly redirected the fork base to REDGARDEN itself instead — "hard fork from redgarden repo
wise" — which also happens to match `REDGARDEN/NORTHSTAR.md` §29's own separate, earlier
architecture recommendation ("a directly-piloted hero point at `packages/simulation/arena_game.c`/
`apps/arena` ... as ECOWAR's base simulation loop, extended with card-driven RTS systems grafted
on — not a fresh third `ServerState`"). The GFD-battlegrounds_gui plan is superseded, not forgotten
— see "Origin" below for the full original reasoning, still real history.

**Status**: hard-forked from REDGARDEN at commit `1515caf` (full REDGARDEN git history now part of
this repo — 446+ commits), build verified (`bash scripts/build.sh`, all binaries compile clean)
and tests verified (`bash scripts/test_arena.sh`, same real-world result REDGARDEN itself gets:
every check passes except the already-documented, sandbox-only `test_arena_replay` segfault).
Gameplay is currently byte-identical to REDGARDEN's `apps/arena`/`apps/arena_server` — the fork
gives ECOWAR its own repo, history, and deploy identity; real ECOWAR-specific mechanics (cards,
RTS systems) are genuinely not started yet, per the founder's own "more specific mechanic
direction to follow for now build what is obvious" sequencing.

### Direction for what comes next (real, not yet built)

- **Separate deployment**: ECOWAR's own matchmaker + bot-pool (1v1, own port range, own systemd
  units) — distinct from REDGARDEN's `:7778-7780`/`:8778` range so the two can run on the same box
  without collision.
- **PARENA-embedded, mod-API-first**: "the framework should deeply embed parena... mod api first
  parena mod dev... do the whole game in pure parena as much as you can." Every REDGARDEN mod to
  date (Bloodflower, Tree passive, Duck's Smoke Bomb, Abraham's Fireball, build templates, item
  curriculum) uses the same real, deliberate shape — "the mod is the trigger, host C does the real
  work" — because VS0 (PARENA's current compiler) has real, current gaps: no `F32` mod parameters,
  no `Vec`/array parameters, no closures (see `PARENA/STDLIB.md` and `ladybug`'s own README for the
  full list). ECOWAR should push past that trigger-only pattern wherever VS0 genuinely allows real
  logic in PARENA (I32-only arithmetic, `match`/`cond` decision logic — e.g. a card's own effect
  resolution) rather than defaulting to "just a trigger" out of habit, while staying honest about
  what VS0 still can't do yet (anything needing floats or arrays stays in C, flagged, not forced).
- **Map editor ideas**: "the ideas from the redgarden map editor" — no such editor exists yet
  anywhere in this monorepo (confirmed via a real search before writing this) — read as forward
  guidance for how ECOWAR's own future map/level tooling should be designed, not a reference to an
  existing tool to port.
- **16 cards from TYLER's hero bible + Prompt-o-verse art**: real card content, not yet built —
  `TYLER/multiverse_heroes.md` is the source roster, `emily promptoverse` is the real art pipeline
  already established for exactly this kind of asset (see BRAWLPIT's own fighter roster for the
  precedent: lore into TYLER's hero bible first, then real stats/mechanics).

## Origin — ECOWAR's earlier scoping (2026-08-20, superseded above, kept for real history)

Founder, real-time (2026-08-20): "build ECOWAR" → "separate lobby" → "separate source" → "HARD
FORK" → "in order to maintain easy hackability" → "we want to hard fork the GFD version interface
wise and we want the features of the new mainline REDGARDEN like improved ai and items and wasd
movement etc."

Concretely: a genuinely separate, independently-forked codebase (not just a separate mode/
matchmaker/client living inside one shared REDGARDEN repo — an earlier, since-superseded scoping
pass) — forking `GoblinFoxDragon/apps2/battlegrounds_gui`'s interface, but pulling in mainline
REDGARDEN's newer improvements (bot AI, item catalog, WASD movement) on top of it. The rationale
was explicit: keep both REDGARDEN and this fork simple and hackable, rather than one shared
codebase accumulating both games' complexity — a rationale that still holds even though the fork
base itself changed.

Before the hard-fork direction, ECOWAR was scoped as a new REDGARDEN game mode (`REDGARDEN/
NORTHSTAR.md` §29, added 2026-08-13) reviving REDGARDEN's original deck-building/card-RTS vertical
slice (`packages/simulation/local_game.c`) blended with arena-side MOBA elements. See
`REDGARDEN/NORTHSTAR.md` §29 (still present in this repo's own history/tree, forked in) for that
scoping's full writeup.

## Real, unresolved design questions (not decided by this doc)

- Full hero kits vs. a simpler card-summoned unit tier for the RTS-troop half of the original
  card-game design (§29's own open question 2, never resolved).
- The dragon/shared-structures objective (§29's own open question 3, never resolved).
- Exact card mechanics — "more specific mechanic direction to follow" per the founder's own
  words this session; the 16-card catalog below is content, not yet a resolved mechanics system.
- Sync model with REDGARDEN going forward: one-time fork plus manual re-porting of individual
  fixes (shankpit-460's own established precedent), or something else — not decided here.

None of this is decided. Flagged honestly as open, not silently resolved.

## Related

- `REDGARDEN` — the fork source (full history preserved in this repo).
- `GoblinFoxDragon/apps2/battlegrounds_gui` — the original (superseded) interface fork source.
- `TYLER/multiverse_heroes.md` — the hero-bible source for this repo's own card content.
- `PARENA` — the language this framework should deeply embed, mod-API-first.
- `EMILY/BACKLOG.md` SECTION 188 (S188-04, 2026-08-20 scoping) and SECTION 202 (2026-08-27,
  this fork) — the real-time scoping threads in full.
