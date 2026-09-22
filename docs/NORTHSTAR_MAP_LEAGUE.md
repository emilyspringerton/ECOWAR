# ECOWAR Map-Building League — NORTHSTAR

*Written 2026-09-22. Founder real-time: "continue working on ecowar as map building game you
build a map your army fites their army in your map then in their map etc slow and fast leagues."*
Continuation of kanban card `T13806138` ("ECOWAR/NOCK level-building game: AI+human co-op/
competitive level authoring, teams fight in each other's levels, slow league (human) + fast
bot-only league") — queued, not scoped, until this pass.

## 1. Real, checked-first finding: "map building" is mostly already real, just not player-facing

Before designing anything new, checked what actually exists (not assumed):

- **ECOWAR already has real, seeded, procedural map generation** (S370-02, 2026-09-11, founder:
  "procedurally generate the map for each new game"). `arena_set_match_seed` (`packages/
  simulation/arena_game.c`) drives `arena_obstacles_reset_layout`'s own xorshift32 PRNG-based
  jungle/obstacle placement — a real, deterministic, from-scratch-buildable map, keyed by a
  single `uint32_t` seed. `MatchFoundMsg.seed` (`packages/common/protocol.h`) already carries
  this seed from matchmaker → server → every client, so both sides compute the identical layout
  independently — the wire plumbing for "everyone in this match sees the same map" is real and
  already shipped.
- **The one real, current gap**: the matchmaker (`apps/matchmaker/src/main.c`'s `try_match`)
  generates that seed itself, from `time()`, every match — a player has no way to supply their
  OWN seed today. `PACKET_FIND_MATCH` (the client's own queue request) carries no payload at all
  — checked directly, `QueuedClient` only stores a `sockaddr_in`, nothing else. "Build a map"
  today means nothing more than knowing the game generates one randomly; there's no real sense in
  which a specific map belongs to a specific player yet.
- **No per-player identity in the matchmaker at all.** Unlike DEADWEIGHT/BRAWLPIT (`IDUNA/
  internal/games.Registry`'s generic guest-account system), ECOWAR's matchmaker pairs bare UDP
  addresses — no player_id, no IDUNA JWT, no persistent identity concept anywhere in `apps/
  matchmaker`. A real, PERSISTENT "your own map, saved, yours across matches" needs this to exist
  first; a real, per-QUEUE-REQUEST "the seed I want to play on right now" does not.
- **Node/terrain LAYOUT (the 9 capture points) is still fully hardcoded**, unaffected by the
  seed — only the obstacle/jungle placement is procedural. A "map" in this doc's own scope means
  the seed-driven obstacle layout, not a fully player-authored tile-by-tile editor (that's real,
  separate, much bigger scope — `docs/NORTHSTAR_MAP_EDITOR.md`'s own Phase 3/4, still not
  started, still the right doc for that half if the founder wants it later).

## 2. What "you build a map, your army fights in your map, then in their map" means, concretely

Real, buildable in phases, grounded in what's actually there today:

- **Phase 1 — client-supplied match seed.** A queuing client sends its own preferred `uint32_t`
  seed in `PACKET_FIND_MATCH`'s own (new) payload; `0` means "no preference, matchmaker picks
  randomly" (today's exact behavior, unchanged default). This alone makes "build a map" real and
  testable: pick a seed, see the same procedural layout every time you queue with it — the actual
  mechanic a Minecraft-style seed IS a map, applied here.
- **Phase 2 — home-and-away 2-leg series.** When two players (not bots) queue against each other,
  play a real 2-match series: leg 1 on player A's own seed, leg 2 on player B's own seed, real
  aggregate scoring (e.g. total node-captures or wins across both legs) decides the series winner
  — the actual "your map, then their map" mechanic. Needs: the matchmaker to track a real 2-leg
  series state (not just spawn-and-forget), both clients to handle a real "leg 1 over, reconnect
  for leg 2" transition, and a real aggregate-scoring rule (not designed here — a real, separate,
  small design decision).
- **Phase 3 — slow vs. fast league.** "Slow league" = real human 1v1 queue (today's real
  `ecowar-matchmaker.service`, `:9779`, unchanged) running the real 2-leg series above at real
  human pace. "Fast league" = bot-only, continuous, using the real existing bot-pool
  (`ecowar-bot-pool.service`) — bots queue against each other continuously, running many 2-leg
  series back-to-back, fast-forwarded (no real-time human pacing constraint) — the same real
  "slow bot league vs. fast league" shape `SLOWBOT_LEAGUE/NORTHSTAR.md` already scoped for
  SHANKPIT, applied here instead. Needs a second matchmaker/bot-pool deployment pair, same
  "R&D vs. stable, two full independent deployments" precedent `REDGARDEN/CLAUDE.md` already
  established, not a mode flag on the existing one.
- **Phase 4 — persistent, IDUNA-backed "your map."** A real player identity (ECOWAR isn't in
  `games.Registry` yet — real, separate onboarding work, same shape DEADWEIGHT/BRAWLPIT already
  did) plus a small table (`ecowar_player_maps`: player_id, seed, created_at) so a seed is
  actually OWNED and reused across sessions, not just typed in fresh every queue. Real, valuable,
  genuinely deferred — Phases 1-3 above don't need it (a session-scoped "my seed for this queue"
  is real and useful on its own).

## 3. Status

| # | Phase | Status |
|---|---|---|
| 0 | This doc | DONE |
| 1 | Client-supplied match seed (`PACKET_FIND_MATCH` payload + matchmaker) | DONE -- `5ee8aed`, live-verified (matchmaker log confirms the exact requested seed, not a random one) |
| 2 | Home-and-away 2-leg series | NOT STARTED |
| 3 | Slow/fast league split (second matchmaker+bot-pool deployment) | NOT STARTED |
| 4 | Persistent IDUNA-backed player map ownership | NOT STARTED |

## Related

- `docs/NORTHSTAR_MAP_EDITOR.md` — the bigger, separate "real tile/prop map editor" epic; this
  doc's own "map" scope is the existing seed-driven procedural layout, not that editor's future
  hand-authored format. The two aren't in conflict — a future editor could plausibly let a player
  paint hand-placed overrides on top of a seed, but that's real, later, unscoped work.
- `SLOWBOT_LEAGUE/NORTHSTAR.md` — the real precedent this doc's own Phase 3 slow/fast split
  borrows its shape from (a SHANKPIT-side sibling scope, not literally shared code).
- `REDGARDEN/CLAUDE.md`'s "Deployments (R&D vs. stable)" section — the real, established
  "two full independent deployments, never sharing a process/port" precedent Phase 3 follows.
