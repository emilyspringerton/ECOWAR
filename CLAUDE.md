# ECOWAR

## What this is

A hard fork of REDGARDEN (full git history preserved, forked at REDGARDEN commit `1515caf`),
diverging forward from here as its own project — same real precedent `shankpit-460` established
for `SHANKPIT`. See `README.md` for the full founder-quote provenance, including the real,
honest pivot from ECOWAR's original 2026-08-20 scoping (a `GoblinFoxDragon/apps2/battlegrounds_gui`
interface fork) to this session's redirected "hard fork from redgarden repo wise."

**Status**: fork complete and verified (`bash scripts/build.sh` + `bash scripts/test_arena.sh`
both clean, same real result REDGARDEN itself gets). Gameplay is currently byte-identical to
REDGARDEN's `apps/arena`/`apps/arena_server` — real ECOWAR-specific mechanics (cards, RTS
systems, separate deployment) are the next real work, not yet built. Read `README.md`'s "Direction
for what comes next" before assuming any of that is done.

**Deliberately, per the founder's own "more specific mechanic direction to follow for now build
what is obvious"**: don't guess at card mechanics, RTS systems, or deep gameplay divergence from
REDGARDEN until real direction arrives. Deployment separation (matchmaker/bot-pool/artifacts),
PARENA-mod-first architecture, and real card *content* (the 16-card catalog) are the "obvious"
parts safe to build now.

## Stack

Inherited from REDGARDEN, unchanged for now: C99, SDL2/OpenGL (client), server-authoritative
UDP (`packages/simulation/arena_game.c`), PARENA mods for real gameplay mutations (`stdlib/
redgarden/*.prn` — will get their own `stdlib/ecowar/*.prn` namespace as ECOWAR-specific mods
land), Bazel (`bazel build //...`) + a plain Makefile-equivalent (`scripts/build.sh`) for fast
local iteration, same as REDGARDEN.

## Related Repos

- `REDGARDEN` — the fork source (full history preserved in this repo's own git log).
- `GoblinFoxDragon` — `apps2/battlegrounds_gui` was the original (superseded) interface fork
  source; still the real live GFD Battlegrounds product, unrelated to this repo going forward.
- `TYLER` — `multiverse_heroes.md` is the hero-bible source for this repo's own card content.
- `PARENA` — the language this framework should deeply embed, mod-API-first (founder: "the
  framework should deeply embed parena... mod api first parena mod dev... do the whole game in
  pure parena as much as you can" — see README.md for what VS0's real current limits mean for
  that ask in practice).
- `EMILY` — RSI loop / backlog coordination for cross-repo work (`BACKLOG.md` SECTION 188 for
  the original scoping, SECTION 202 for this fork).

## Founder Real-Time Direction

Whenever the founder gives real-time direction — a new ask, a correction, a "can we also..." —
route it through `emily observe -s info "Founder real-time: <summary>"` first, even if it isn't
this repo's usual domain, then sprint-plan it into `EMILY/BACKLOG.md` (`emily backlog curate`,
scoped into a real SECTION/sub-item, not just a one-line log), and only then implement. See
`EMILY/docs/THE_EMILY_WAY.md` Principle 18 ("Pave the Cow Paths").

## Apple Filing Protocol

After any meaningful change, file an Apple:
```bash
emily apples post -t completion -repo ECOWAR "<title>" "<body with commit hash>"
```
Then mark the item done in `EMILY/BACKLOG.md` and commit.

## CHANGELOG Protocol

After any meaningful change, update CHANGELOG.md:
```bash
emily changelog add ECOWAR "<what changed>"
# or manually: append a dated bullet under ## YYYY-MM-DD in ECOWAR/CHANGELOG.md
```

## Frame-Break Reframing

Founder-sourced prompting technique (REDGARDEN/NORTHSTAR.md §28, full origin in
REDGARDEN/docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md §5): given a request, name the underlying
structural/systemic pattern it's one instance of — one level of abstraction up — as an added
lens during planning/triage/judgment calls. Use it to spot the general case behind a specific
ask. It augments judgment, it does not replace doing the work: direct, concrete execution of
the literal task asked for still happens every time.

## Commit Protocol (standing instruction)

Always commit and push completed work immediately — don't wait to be asked. This is the default for every repo in this monorepo.

Every commit — human-written or produced by automated code paths (git-commit helpers in emily-agent, emily.cli, IDUNA handlers, etc.) — must carry the active `emily session` fingerprint as a `session: <tag>` trailer (blank line, then the trailer). This was silently missing from several independently-implemented automated commit helpers across the monorepo until an audit on 2026-08-10 (founder, real-time: "where in the fuck is my llm session id anywhere"). If you add a new automated git-commit code path anywhere, wire in the session tag the same way — don't assume an existing helper already does it.
