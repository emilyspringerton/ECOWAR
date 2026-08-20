# ECOWAR

## What this is

A hard fork of `GoblinFoxDragon/apps2/battlegrounds_gui`'s interface, carrying forward mainline
REDGARDEN's newer features (improved bot AI, item catalog, WASD movement) — see `README.md` for
the full founder-quote provenance, ECOWAR's earlier (superseded) in-REDGARDEN mode scoping, and
the real, unresolved design questions about the fork itself.

**Status: scaffolding only.** `CLAUDE.md` (this file) and `README.md` are the first two files in
the repo. No code has been forked or written here yet — that's real, unstarted follow-up work.
Read `README.md` before assuming anything about scope; don't start forking `battlegrounds_gui`
code here without first resolving the open design questions it lists (what forks vs. rebuilds,
one-time copy vs. ongoing sync with REDGARDEN, matchmaking-port model, source vendoring).

## Stack

Not yet decided at the tooling level, but the fork source is known: C (`battlegrounds_gui`'s
`src/main.c` + `packages/simulation`/`packages/common`, SDL2/OpenGL, same server-authoritative
UDP model as REDGARDEN/SHANKPIT). Whether this repo vendors its own copy of the shared packages
or imports them some other way is one of `README.md`'s open questions.

## Related Repos

- `GoblinFoxDragon` — `apps2/battlegrounds_gui` is the fork source for this repo's interface.
- `REDGARDEN` — source of the newer features (AI, items, WASD movement) to pull in.
- `EMILY` — RSI loop / backlog coordination for cross-repo work (`BACKLOG.md` SECTION 188).

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
