# ECOWAR

A hard fork of GFD's Battlegrounds client, carrying forward mainline REDGARDEN's newer features.

## What this is

Founder, real-time (2026-08-20): "build ECOWAR" → "separate lobby" → "separate source" → "HARD
FORK" → "in order to maintain easy hackability" → "we want to hard fork the GFD version interface
wise and we want the features of the new mainline REDGARDEN like improved ai and items and wasd
movement etc."

Concretely: a genuinely separate, independently-forked codebase (not just a separate mode/
matchmaker/client living inside one shared REDGARDEN repo — an earlier, since-superseded scoping
pass, see "History" below) — forking `GoblinFoxDragon/apps2/battlegrounds_gui`'s interface, but
pulling in mainline REDGARDEN's newer improvements (bot AI, item catalog, WASD movement) on top
of it. The rationale is explicit: keep both REDGARDEN and this fork simple and hackable, rather
than one shared codebase accumulating both games' complexity.

## Status: repo scaffolding only, no code yet

This is a fresh, empty repo as of 2026-08-20 — `CLAUDE.md` and this `README.md` are the first two
files. Nothing has been forked, copied, or written here yet. That's real, unstarted follow-up
work, not attempted in this pass.

## Origin — ECOWAR's earlier scoping, for context

Before the hard-fork direction, ECOWAR was scoped as a new REDGARDEN game mode (`REDGARDEN/
NORTHSTAR.md` §29, added 2026-08-13) reviving REDGARDEN's original deck-building/card-RTS vertical
slice (`packages/simulation/local_game.c`) blended with arena-side MOBA elements, with its own
separate matchmaking, lobby, and client — "separate everything," but still inside the REDGARDEN
repo. That scoping pass flagged four real open architecture questions (full hero kits vs. a
simpler unit tier, client unification timing, deck-building integration depth, matchmaking-port
precedent) that remain genuinely open here too; nothing about the hard-fork decision resolves them
on its own. See `REDGARDEN/NORTHSTAR.md` §29 for that scoping's full writeup.

## Real, unresolved design questions (not decided by this doc)

- What exactly gets forked from `battlegrounds_gui` vs. rebuilt: the whole `src/main.c` client, or
  a subset?
- How does "pull in mainline REDGARDEN's newer features" actually work mechanically — a one-time
  copy of the relevant REDGARDEN commits at fork time, or an ongoing sync/cherry-pick relationship
  with REDGARDEN going forward? (The `battlegrounds_gui` fork's own precedent — a one-time fork
  plus manual re-porting of individual fixes like the Jungle Camps milestones — is the closest
  existing model, but not confirmed as the intended one here.)
- Separate lobby/matchmaking: a new port following the existing `--matchmaker-port` /
  `--lobby-size` flag pattern (`REDGARDEN/apps/matchmaker`'s own dual-role precedent), or a
  fully independent binary?
- Separate source: does this repo vendor its own copy of `packages/simulation`/`packages/common`,
  or import/submodule them?

None of this is decided. Flagged honestly as open, not silently resolved by whoever writes the
first real commit here.

## Related

- `GoblinFoxDragon/apps2/battlegrounds_gui` — the fork source for this repo's interface.
- `REDGARDEN` — source of the newer features (AI, items, WASD movement) to pull in, and of
  `NORTHSTAR.md` §29's original ECOWAR mode scoping.
- `EMILY/BACKLOG.md` SECTION 188 (S188-04) — this session's real-time scoping thread in full.
