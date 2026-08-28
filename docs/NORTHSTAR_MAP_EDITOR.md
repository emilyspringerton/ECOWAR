# ECOWAR Map Editor + Arena API — NORTHSTAR

Founder, real-time, in one long burst (2026-08-27): "ok all in on the ecowar map editor lets add
ecowar features fof the cards and all that and the map editor at the same time" -> "parena mod
powered development" -> "bundle parena editor into the map editor" -> "same thing bundle the
entire parena and ecowar source in with a file tree to edit the files" -> "and a way to make new
[mods] if you want to make new parena mods there should be a way to do that but that should come
a little later" -> "for now we can build the ecowar cli on top of parena to do any cli things we
need" -> "built into the map thingy" -> "just the cli will work no other bash stuff to start...
until we have an environment" -> "i mean it can find bash i guess like it does with gitbash...
not an early requirement" -> "[arena apis first we wil build ecowar ontop of themn we start from
a bare engine and build on top of it with mod surface" -> "it would be good to make it make sense
order it where you think" (explicit delegation of phasing).

This is a genuinely large, multi-session epic spanning `ECOWAR` and `PARENA`. This doc exists to
give it one real, honest scoping pass — what's real today, what's actually missing, and a
phased order — rather than guessing at implementation mid-stream. Real work starts from Phase 1
below; this doc itself is the real, deliverable first step (`EMILY/CLAUDE.md`'s own golden-doc
protocol).

## What this is

Two real, related deliverables:

1. **A bare, mod-surface-first "Arena API"** — ECOWAR's own engine exposes a real, documented set
   of PARENA mod hooks (gameplay mutation points), and ECOWAR's own card/RTS features are built
   ON TOP of that surface as real PARENA mods, not baked into the C engine directly. Matches
   `ECOWAR/CLAUDE.md`'s own already-stated direction: "the framework should deeply embed
   parena... mod api first."
2. **An ECOWAR Map Editor tool** — a real, standalone binary that bundles the already-real PARENA
   editor (this session's own work: buffer/render/widgets/Spotlight/terminal-toggle/dual file-
   tree/Compile+Save) as its foundation, adds a real map-editing surface for ECOWAR's own arena
   layouts, and bundles an ECOWAR CLI (built on PARENA, not bash) for the build/deploy/matchmaker
   tasks currently spread across `scripts/*.sh`/`*.bat`.

## Real, current state (as of this doc)

- **PARENA editor**: real, shipped, CI-green. Has a real dual-sidebar pattern already (a LEFT
  file-tree for "whatever directory you're browsing" and a RIGHT file-tree for "the editor's own
  source," both independently navigable, both spawning new windows via `spawn_new_instance` on
  file click) plus a Compile button (rebuilds via `make editor-demo`, relaunches the fresh binary)
  and a Save button (top-left, hover-reveal, shares `do_save`'s real prnfmt-on-save path with the
  F2 keybind). This is the real, direct foundation Deliverable 2 bundles — not a rewrite.
- **ECOWAR's own "map"**: NOT a real, separate, loadable data format today — terrain features and
  spawn-point coordinates are hardcoded C constants directly in
  `packages/simulation/arena_game.c` (confirmed by grep: `arena_fountain_position`, hardcoded
  `x=+-6` spawn coordinates, etc., inherited byte-identical from REDGARDEN). A real map editor
  needs a real, separate map format to edit BEFORE any editing UI makes sense — this is the real,
  honest first technical blocker for Deliverable 2's own map-editing surface specifically (as
  opposed to the "bundle the PARENA editor" part, which needs no new data format at all).
  Real, immediate value: even before a map-EDITING surface exists, the bundled tool can be **the
  PARENA editor + dual source tree + CLI, pointed at ECOWAR's own repo** — genuinely useful on
  day one, with the map-canvas layered in once Phase 3a below lands.
- **Mod surface**: real PARENA mods already exist and load (`stdlib/redgarden/combat_log_mod.prn`
  is a real, current example) — but there is no single, documented "Arena API" naming what hooks
  exist, what a new mod can register for, or what's ECOWAR-specific vs. inherited from REDGARDEN.
  `ECOWAR/CLAUDE.md`'s own "Stack" section already flags this gap: "`stdlib/redgarden/*.prn` —
  will get their own `stdlib/ecowar/*.prn` namespace as ECOWAR-specific mods land" — not done yet.
- **Card system**: real card *data* exists (the 16-card catalog, per `ECOWAR/README.md`'s own
  "first mod to do real PARENA decision logic" note) but the earlier live playtest this session
  found **zero real callers** — a player has no way to actually trigger a card in a live match.
  Real, confirmed-live gap logged earlier this session (BACKLOG.md), not yet closed.
- **ECOWAR CLI**: does not exist. Today's tooling is `scripts/build.sh`, `scripts/test_arena.sh`,
  `PLAY.bat`/`START_ARENA_POOL.sh` — real, working, but hand-rolled bash/batch, not PARENA.
- **shell/spawn (git-bash auto-detect)**: real, shipped this session (`PARENA/stdlib/shell.prn`),
  already proven in the PARENA editor's own terminal-toggle feature. Directly reusable for the
  ECOWAR CLI's own "find a real shell to run build steps in" need — founder's own "it can find
  bash i guess like it does with gitbash" already names this as fine to reuse, not something to
  avoid.
- **Arena API / mod surface (Phase 1, done — see `docs/ARENA_API.md`)**: `PARENA/stdlib/ecowar/`
  already exists and already has a real mod in it (`card_effect_mod.prn`) — the "where does
  `stdlib/ecowar/*.prn` live" open question this doc originally posed is answered by real,
  existing precedent, not a new decision. `ARENA_API.md` documents the real ABI (trigger-only vs.
  real-decision-logic mods, the generate-commit-wire build process, the full current mod
  inventory) and confirms the earlier-flagged card gap precisely: `ecowar_resolve_card_effect`
  is defined and correctly calls the real PARENA mod, but has zero real callers anywhere in the
  codebase — Phase 2's own first concrete task.

## Real, honest phased order (founder delegated this: "order it where you think")

Numbered for reference, not a rigid gate — later phases can start once their own real
prerequisite lands, not necessarily only after every earlier phase is 100% "done."

1. **Arena API v0** — survey and DOCUMENT the real, current mod-hook surface ECOWAR's engine
   already exposes (via the real, existing `stdlib/redgarden/*.prn` mods), give it the promised
   `stdlib/ecowar/*.prn` namespace, and name any real, missing hooks a card/RTS mod would need
   that don't exist yet. Foundational — Phase 2 depends on this being real and named, not
   reverse-engineered per-mod.
2. **ECOWAR card/RTS mods** — the "cards and all that" ask. Real, existing card data already has
   a real gap (no in-match trigger path) — closing that is real, obvious next content work once
   Phase 1 names the hook it needs.
3. **Map data format** — extract the currently-hardcoded terrain/spawn-point layout out of
   `arena_game.c` into a real, separate, loadable/saveable format (PARENA struct + real
   read/write, or JSON via the already-real `stdlib/json.prn` — a real, small, separate design
   decision, not attempted here). The real, honest prerequisite for Phase 4's own editing surface
   to mean anything.
4. **Map Editor tool** — bundle the real, already-shipped PARENA editor (buffer/render/dual-tree/
   Compile/Save) as the base binary, generalize the existing right-tree's "editor's own source"
   default to also browse ECOWAR's own repo (both trees real and independently useful even before
   any map-specific UI exists), then layer in a real map-editing canvas reading/writing Phase 3's
   format.
5. **ECOWAR CLI on PARENA** — replaces `scripts/*.sh`/`*.bat` with a real PARENA-implemented CLI,
   bundled INTO the Map Editor tool (not a separate binary), reusing `shell/spawn`'s real git-bash
   auto-detect for whatever it still needs a real shell for. Real, deliberately narrow v0 per the
   founder's own "just the cli will work no other bash stuff to start... until we have an
   environment" — full bash-script parity is NOT the v0 bar.
6. **PARENA mod-scaffolding workflow** (deferred, founder's own "a little later") — a real
   "create a new PARENA mod" flow from inside the tool. Not scoped further here; real, separate
   follow-up once the rest of this doc's own real, more concrete phases land.

## Real open questions, not resolved here

- **Where does `stdlib/ecowar/*.prn` actually live?** `PARENA/stdlib/` (matching how
  `stdlib/redgarden/*.prn` already lives in PARENA today, not in REDGARDEN's own repo) or a new
  `ECOWAR/stdlib/`? Real, deliberately unresolved — Phase 1 should settle this by following
  whatever precedent `stdlib/redgarden/*.prn`'s own real, current location already sets, not
  guessed at here.
- **Map format specifics** (binary vs. JSON, what a "prop"/"spawn point"/"terrain feature" struct
  actually needs) — real, separate design work for Phase 3, not scoped here.
- **Does the Map Editor tool become its own binary/repo, or a build target inside ECOWAR that
  links the PARENA editor's own generated C?** Real, separate build-system question for Phase 4.

## Related

- `PARENA/docs/NORTHSTAR_LINNEN.md` — the editor's own native UI widget framework this tool's map
  canvas will likely need to grow past Toggle-only widgets to build.
- `ECOWAR/NORTHSTAR.md` — the large, REDGARDEN-inherited design doc; general ECOWAR-vs-REDGARDEN
  gameplay design lives there, this doc is deliberately narrow to the map-editor/mod-surface/CLI
  epic specifically, not a replacement for it.
- `ECOWAR/README.md` — "Direction for what comes next," the card-system's own real, current scope
  note this doc's "Card system" state paragraph above draws from.
