# Changelog

## 2026-07-28 (continued)

- feat(arena): reduce team size to 7v7 (S170-178). Founder: "reduce it to 7 v 7." `ARENA_TEAM_SIZE`
  10 → 7 — every sim-side array/loop bound derives from `ARENA_MAX_HEROES`, so this is the whole
  gameplay change. Duplicated size constants that don't auto-derive updated to match:
  `ARENA_SNAPSHOT_MAX_HEROES` (protocol.h) 20 → 14, `launch_arena_pools.sh`'s
  `BOT_POOL_LOBBY_SIZE` 20 → 14, `run_bot_pool.sh`'s default bot count 19 → 13, both
  `ops/systemd/*.service` deploy sources' lobby-size/bot-count. The systemd files are deploy
  sources only — editing them doesn't touch the actually-running live pool, which needs a manual
  re-copy + restart on the host (deliberately not done here). Build clean, full suite green — all
  team-mode tests already reference `ARENA_TEAM_SIZE` symbolically.

- feat(arena): shop panel + character pane + scoreboard, Sprint 4 of NORTHSTAR §19 (S170-175).
  Shop structures rendered at each `arena_shop_position()` (team-relative colored trim). An
  always-visible character stat pane (local hero's HP/MP/AD/Armor/Flow/Flow-earned/XP/K-D). A
  shop panel (`B` to toggle) with instant one-click buy/sell and `1`-`9` quick-buy — no confirm
  step, per this repo's own "high-APM... instantly resolve, no menu-diving" cross-cutting
  constraint. A held-`TAB` scoreboard: per-hero and team-aggregate K/D/Flow/XP. `net_poll_snapshots`
  now copies the Sprint-3-synced economy fields into local hero state so the client has real data
  to show. Build clean, full suite green (client-only change). Visual verification hit a real but
  pre-existing Xvfb/software-GL coordinate quirk in this sandbox that also reproduces against
  already-shipped HUD code this change never touched (the 3D pass, including the new shop
  structures, rendered correctly in every screenshot) — flagged, not faked; real-desktop
  verification still open.

- feat(arena): shop wire protocol, Sprint 3 of NORTHSTAR §19 (S170-175). `PACKET_ARENA_SHOP_BUY`/
  `PACKET_ARENA_SHOP_SELL` + `ArenaShopBuyCmd`/`ArenaShopSellCmd`, dispatched in
  `server_handle_packet` the same shape as the existing `PACKET_ARENA_ATTACK` handler.
  `ArenaHeroSnapshot` gains `flow`/`flow_earned`/`xp`/`kills`/`deaths`/`equipped_item[]` so the
  client can see this state once the character pane (Sprint 4) reads it. Wire plumbing only —
  shop positions/proximity/buy-sell validation shipped in Sprint 2 with full test coverage. Not
  live-network-verified with a raw UDP client this round; flagged, not faked (see commit
  `c80cb93` for the full reasoning). Full suite still green.

- feat(arena): Flow/XP economy + FFXI/WoW item slots, Sprint 1+2 of NORTHSTAR §19 (S170-175).
  Per-hero `flow`/`flow_earned`/`xp`/`kills`/`deaths` fields, kept deliberately separate from
  `resources[team]`'s win-condition meter (the conflict §19.1 resolved). Flow/XP awarded on
  jungle creep, lane creep, and hero kills — melee/homing-shot only, matching
  `arena_zone_damage_creeps`'s existing "AoE kills grant nothing" precedent. 24-item catalog
  (12 specific from `docs/HEROES_VS0.md`, 2 weird, 10 generic FFXI names from
  `docs/FFXI_ITEM_PARITY_SEED.md`) across 11 FFXI+WoW-style equip slots
  (Weapon/Head/Body/Hands/Legs/Feet/Ring/Neck/Back/Waist/Trinket). Buying auto-equips (no bag),
  auto-sells whatever was already in that slot first; selling refunds 50%. All economy state
  and equipped items survive `arena_respawn_hero`'s reset, with item stat bonuses correctly
  reapplied afterward — found and fixed a related latent bug where `attack_target`/
  `last_attacked_by_owner` were left at 0 (not -1) post-respawn, wrongly meaning "hit by owner
  slot 0." 13 new tests. Sprint 3 (shop wire protocol) and Sprint 4 (client shop UI + character
  stat pane) are next.

- docs(arena): NORTHSTAR §19 — gold/XP economy + structures, resolving a real resources[]
  conflict (S170-174). Founder: "continue the backlog for redgarden." Picks up sprint plan
  items 4/5, designed together since structures' gold-bounty payoff needs gold to exist first.
  Found and resolved a real conflict: `docs/CONSUMABLES_AND_COOKING.md` (written before the
  resource-race win condition existed) assumed cooking spends from the same `resources[]` pool
  that's now the win-condition meter — spending team resources on personal items would slow your
  own team's progress toward winning. Resolved with two separate currencies: `resources[team]`
  stays win-condition-only, a new per-hero gold currency (fed by kills) handles personal power
  progression, matching how LoL/Dota split objectives from per-player gold. Grounds the spend
  target in `docs/HEROES_VS0.md`'s existing 12-item Starting Item Roster instead of designing
  new items from scratch, names the concrete mechanical work each item category needs, and
  scopes XP down to a flat power curve rather than a full leveling system. Structures follow the
  map's real single-lane geometry, closing the "push payoff" gap S170-139/Duck's W have both
  been blocked on. Spec only, same "no code yet" treatment as §15-§18.

- feat(arena): bot AI seeks out healing fountains when critically low on HP (S170-173).
  Founder: "add healing fountains to bot awairness brain and heuristics whatever makes sense
  bots seek out fountains when super low." New top-priority check in the bot decision loop,
  evaluated before node-capping or enemy engagement — a hero below 25% HP retreats to the
  nearest fountain and does nothing else that tick until topped back up. Fountain positions are
  static, mirrored by hand from `arena_fountain_position()`'s two fixed points, no wire sync
  needed. Live-verified via an isolated 20-bot match: 153 low-HP snapshots observed, 65 of them
  (42%) with the hero positioned near a fountain corner — real evidence of retreat behavior, not
  chance. No crashes, full suite green (bot-client-only).

- feat(arena): heroes and creeps now rotate to face their movement direction (S170-171).
  Founder: "heroes and creeps should rotate to show what direction they are facing currently
  they just float around there is no front of the model." This renderer never had a rotation
  matrix at all — added `mat4_rotate_y` to `packages/common/mat4.h`. Facing is derived purely
  from observed motion (position delta since last frame), needing no wire-protocol change and
  persisting the last known heading through a stop instead of snapping to a default. Heroes'
  existing per-hero_id silhouettes (Unicorn's horn, Duck's bill, etc.) already had a real
  "front," just frozen pointing at a fixed +Z — now the whole composite rotates as one rigid
  shape. Jungle and lane creeps were plain symmetric cubes with nothing to show a turn, so both
  got a small darker forward-facing nub added. Live-verified via Xvfb, full suite green
  (client-rendering-only, no sim/protocol touched).

- feat(arena): Tyler's W (Poof) teleports the whole clone army, not just his own body (S170-170).
  Continues the earlier sprint plan's own open item, flagged directly in `docs/HEROES_VS0.md`'s
  S170-141 scope note ("W still moves only Tyler's own body ... a real next step, not attempted
  this pass"). `tyler_cast_w` now teleports every active clone linked to Tyler to the exact same
  point he blinks to, each independently landing its own arrival-damage check against the same
  target — concentrating the whole clone army's damage onto one enemy, the real "full-team dive
  tool" identity the original design names. Removed `ARENA_TYLER_W_HIT_RADIUS`, now genuinely
  unused (the old distance check was always trivially true by construction). 1 new test, full
  suite green, live-verified via an isolated 20-bot match with no crashes.

- fix(arena): boids flocking made bots dance around node objectives instead of capping
  (S170-168). Founder, real-time, live: "there is a bug where the boyds stuff makes the team do
  a weird cluster dance around the objective ... not sitting right on it" -> "at least one of
  them should sit right on it and ignore the flock." Root cause: separation force is strongest
  exactly when allies are close together, unavoidably true the moment several bots converge on
  the same node — flocking never let anyone settle long enough to make real capture progress.
  Fixed with a stateless "anchor" rule: a bot ignores the flock and paths straight to the node's
  exact position whenever its own owner index mod the node count matches the target node's
  index; every other bot still flocks around it as a loose escort. Live-verified via an isolated
  20-bot match, no crashes.

- docs(arena): NORTHSTAR §18 — unsupervised learning for the bot AI, general + per-hero,
  cross-hero transfer (S170-167). Founder: "write the northstar for unsupervised learning - it
  will have to be both general and per hero - for example experience playing a hero will help
  inform decisions playing with and against it on another hero" -> "also look for archetype
  engine fwiw" -> "we are going to want to do long running per personality bot training but for
  now we need generalized ai for the different heroes." Checked for existing org tech first
  (found a real Archetype Engine, `EMILY/docs/ARCHETYPE_ENGINE_NORTHSTAR.md`, unrelated to hero
  kits but a real fit as a slower strategic tier). Proposes a two-tier architecture — Tier 1
  (fast, per-tick) is this repo's own already-committed §12 Phase E GPT-2 policy-network plan,
  Tier 2 (slow, occasional) is the Archetype Engine, the natural home for the deferred
  long-running per-personality training. Names the general layer as a genuinely unsupervised
  (next-token prediction, no labels) pretraining stage slotting in front of §12's own supervised
  fine-tune, and answers the cross-hero-transfer example concretely: shared weights plus explicit
  archetype/kit-shape tags on `arena_serialize_state`'s existing self/foe framing, so a pattern
  learned on one hero transfers to any other hero sharing the same tagged mechanic. Spec only,
  same "no code yet" treatment as §15-§17.

- feat(arena): click-to-attack system (NORTHSTAR §17) + Gary's homing auto-attack + draft
  randomization fix (S170-162/163/166). Founder: "gary auto attacks are projetiles that always
  hit (visually projectile) they can still miss or crit as normal but you cant juke them" ->
  "implement that with the click to auto attack northstar" -> "and the bots will need to be
  updated so they choose their auto attack targets etc in their brain" -> "up our visual
  affordances for auto attacks so its readable." Built §17.4's real target design (team mode
  only): new `PACKET_ARENA_ATTACK` wire command + `ArenaHero.attack_target` persistent lock,
  pure-pursuit chase toward an out-of-range target every tick (the literal "does it follow a
  fleeing target: yes, automatically, no re-click" answer), and a new homing `ArenaProjectile`
  variant (`homing_target`) for Gary's basic auto-attack — re-aims at its live target every tick,
  connects regardless of movement, not a skillshot. This engine has no miss/crit RNG at all
  (confirmed before building), so that part of the ask is a no-op against a mechanic that doesn't
  exist. Gary excluded from every flat-melee auto-attack path (heroes, jungle creeps, lane
  creeps) so his damage comes exclusively through the homing shot. Wire-synced per-hero so the
  lock is visible to every hero watching (pulsing amber outline on the current target's health
  bar); homing shots render through the existing ability-projectile pipeline with no client
  changes needed. `apps/arena_bot` now sends an attack command every decision tick, the actual
  mechanism that makes a bot-piloted Gary deal damage at all. Also fixed a real bug found in the
  same code path: "ensure auto draft is random i keep always drafting flutedebt first on a new
  client" — the human client's auto-draft offset was port-derived only, not actually random;
  now mixes in `rand()`. 17 new tests, full suite green, live-verified via an isolated 20-bot
  match with no crashes.

- feat(arena): jungle creeps use the "dynamic creep ecosystem" direction (NORTHSTAR §8) -- 
  graveyard spawn + march/fan-out + toned-down team strength (S170-161). Founder: "add jungle
  creeps use the redgarden dynamic creep ecosystem something simple to start," refined with:
  "have the team creeps spawn and fan out from owned nodes marching towards unowned nodes"
  (team-flavored creeps now continuously walk toward the nearest node their team doesn't own,
  recomputed live every tick, each owned node's creep independently fanning out toward its own
  target); "initially they spawn from the graveyards behind the nodes not the center" (spawn
  position is now the owning team's graveyard, not the node's own position); "tone down the
  strength of the team creeps just a bit they are so strong" (`ARENA_CREEP_TEAM_HP` 40->26,
  damage split into neutral/team constants so only team creeps got nerfed). Neutral/contested
  creeps completely unaffected — still stationary at their node, unchanged stats. 9 existing
  tests updated for the new positional assumptions, 6 new tests added. Full suite green,
  live-verified via an isolated 20-bot match.

- feat(arena): boids flocking (alignment/cohesion/separation) in the networked bot AI
  (S170-160). Founder: "add boyds to the ai brain[,] check GFD apps2 crystal for a reference if
  you need it" — GoblinFoxDragon/apps2/crystal/main.go's own working Reynolds boids
  implementation (`Boid` struct, `boidForces()`) used as the structural reference, ported to
  `apps/arena_bot`'s plain-float style and to hero positions. Every bot previously picked its
  own move target completely independently (chase nearest enemy or capture nearest un-owned
  node); new `flock_offset()` adds a small steering perturbation from nearby living teammates
  only — alignment (toward their average recent heading, inferred from this tick vs the
  previous snapshot since the wire format carries position only), cohesion (toward their average
  position), separation (push away from anyone actually crowding) — layered on top of, not
  replacing, the real objective-seeking target. Weights are separation-heavy so this reinforces
  rather than reintroduces the "bots bunch up" bug S170-90 already fixed separately. Live-
  verified via an isolated 20-bot match: no crashes, teammates visibly clustering as loose
  squads. Full suite green (bot-client-only, no sim/protocol changes).

- fix(arena): resource-race bar color was absolute, not viewer-relative (S170-159). Founder,
  real-time, live: "check the win cons i think it shows the wrong team winning" -> "i think the
  color of the bar ticking up may just be wrong." Verified the win-condition logic itself first,
  live: temporarily lowered `ARENA_RESOURCE_CAP` and added a debug print to `apps/arena_bot`
  logging each bot's own team/winner/resources/verdict, ran an isolated match to completion —
  every one of the 20 bots correctly identified win/loss matching its own team and the actual
  resource totals, confirming the simulation's winner logic has no bug (debug changes reverted
  after, zero diff left behind). The real bug was in the resource bar added by S170-153: team 0
  was hardcoded blue and team 1 hardcoded red regardless of which team the local viewer is
  actually on — the exact same absolute-vs-relative mistake S170-149 already found and fixed for
  node coloring. Fixed to color relative to the viewer's own team (mine always blue, opponent
  always red), matching the convention hero name labels and node coloring already use.

- docs(arena): NORTHSTAR §17 — League of Legends auto-attack movement parity spec (S170-158).
  Founder, real-time: a detailed request for exactly how LoL's click-based auto-attacking works
  with respect to movement — does the champion stop, does it chase a fleeing target, ranged vs
  melee differences — with LoL as the explicit gold standard. Documents the real windup/
  backswing/kiting state machine, the persistent attack-target chase lock (pure pursuit, no
  intercept prediction, no leash timeout), and the ranged-specific homing-not-skillshot
  projectile behavior, then grounds the gap analysis in REDGARDEN's actual current combat code
  (`resolve_combat`, `arena_hero_attack_creeps`): a fully passive, always-on proximity system
  today, with no attack command, no windup, no chase state, and no ranged basic attacks at all.
  Spec only, same "no code yet" treatment as §15/§16.

- feat(arena): map widened + corner graveyards; fix: sudden-death fallback closes a real
  zombie-match gap (S170-155/156/157). Founder, real-time: "the map should be a little bigger
  and the graveyards behind 2 of the corners not in the middle of the map." `ARENA_HALF_EXTENT`
  28->32; `arena_graveyard_position()` moved from dead-center-behind-spawn (x=+-9, z=0) to the
  two map corners the fountains don't already occupy, so respawning reads as coming back to a
  real corner base instead of a mid-line marker. Separately, founder flagged a suspicion ("i
  think there may be zombie games with infinite win cons") that turned out to be a real gap:
  removing the team-wipe win condition for S170-153's resource race also removed the only
  mechanism that guaranteed a live match eventually ends, and `apps/arena_server`'s LIVE-phase
  loop had no timeout of its own at all. Added a sudden-death fallback -- after
  `ARENA_MATCH_MAX_DURATION_MS` (12 real minutes) without either team reaching the resource
  cap, whoever's ahead on resources wins outright, tiebroken by nodes currently owned. 4 new
  tests. Live-verified via an isolated 20-bot match confirming the wider map bounds show up in
  real hero movement. Full suite green.

- feat(arena): permanent graveyards, Arathi-Basin resource-race win condition, and 30-second
  wave respawns (S170-153/154). Founder, real-time: "add graveyards behind the spawns that
  never despawn so there is always a place to respawn and add true arathi basin node control
  resource management as a win con instead of team wipe" and "respawns happen in 30 second
  waves." A team wipe no longer ends the match -- teams that own no node now respawn at a
  fixed, permanent graveyard behind their spawn instead of staying dead for the rest of the
  game. The match itself is now decided by `arena_tick_resources()`: each team's resource
  meter (capped at `ARENA_RESOURCE_CAP`) fills every `ARENA_RESOURCE_TICK_MS` based on how
  many of the 5 nodes it currently owns, first team to fill it wins. Respawns no longer count
  down per-hero from their own death -- every dead hero on both teams comes back together the
  instant the global `respawn_wave_timer_ms` wraps at `ARENA_RESPAWN_WAVE_MS` (30s), so dying
  right before a wave is nearly free and dying right after costs nearly the full 30s. Also:
  the networked bot AI gets a first-pass node-capping heuristic (walks to and holds the
  nearest un-owned node when no enemy is within real engagement range, since node control is
  now what actually wins), and the arena client HUD gets a resource-race tug-of-war bar
  (wire-synced via a new `resources[2]` field on `ArenaSnapshotMsg`). 4 invalidated
  team-wipe/per-hero-respawn tests rewritten, 4 new tests added (graveyard fallback, wave
  respawn syncing multiple deaths, resource accumulation scaling with nodes owned, resource-cap
  win condition). Full suite green.

- fix(arena): a jungle creep no longer attacks its own owning team, so capturing/holding your
  own node doesn't damage you (S170-152). Founder, real-time: "capturing node should not make
  the user take damage." Root cause: `arena_tick_creeps()` had no team check at all -- a
  team-flavored creep attacked ANY hero in its aggro radius, including its own owning team.
  Since `ARENA_NODE_CAPTURE_RADIUS` (5.0) comfortably overlaps `ARENA_CREEP_AGGRO_RADIUS`
  (4.0), any hero who stood still to channel-capture (or simply defend/hold) their own
  already-owned node got attacked by their own "home-turf resupply" creep -- thematically
  backwards, real home turf doesn't hurt you for standing on it. Fixed: a team-flavored creep
  now only ever targets the OPPOSING team, matching the counter-play framing its own kill-
  reward already carries ("farming an enemy's own jungle creep helps flip their node"). A
  NEUTRAL/contested creep is unchanged -- still attacks anyone regardless of team, the actual
  "fight through the prize" challenge that flavor is meant to be. 3 new headless tests (own
  team unharmed, opposing team still takes damage, neutral creep regression check). Full suite
  green (468 checks, up from 465).

- feat(arena): ability tiles moved bottom-center, new font glyphs, and a real H-key ability-
  description overlay (S170-151). Founder, real-time, three related HUD requests:
  - **"move the cast frames bottom center"**: the Q/W/E ability tiles (`apps/arena/src/main.c`)
    moved from their old top-left placement to bottom-center, the same anchor point real MOBAs
    (LoL, Dota) use for their own ability bars. The existing retime countdown (radial cooldown
    wipe + seconds-remaining text) and mana-blocked dark/"MP" state were already built
    (S170-127/137) -- this was a pure reposition, not new tile behavior, confirmed unchanged
    by reading the code before touching it.
  - **"ensure our font has all necessary glyphs"**: this client's hand-drawn line-font
    (`draw_char()`) was missing `%`, `?`, `;`, `/`, and `&` -- found ahead of the ability-
    description overlay below, since real ability text (percentages, semicolons in lists,
    question marks) would have silently fallen through to the generic missing-glyph box, the
    same class of gap this font's own comment already flagged once before for hero names.
  - **"H should show an overlay with character ability descriptions"**: a real toggleable
    panel (H key, works in any mode), showing the local player's current hero's Q/W/E names
    (already available via the existing `arena_ability_name()`) plus a new
    `arena_ability_description()` (`packages/simulation/arena_ai_bridge.c`/`.h`) -- a full
    26-hero x 3-slot table of short, plain-language mechanical blurbs (what the ability
    actually does in this arena, not the full `docs/HEROES_VS0.md` lore prose), same
    "short enough to read at a glance" bar the name tiles already set.
  Verified live: a real Xvfb screenshot confirms the repositioned tiles and the overlay panel
  both rendering correctly, including every new glyph (colon, comma, apostrophe, and the newly
  added set all visible and correct in real ability description text). Client-only /
  string-table changes; full headless suite unaffected (465 checks before the creep fix above).

- fix(arena): mana always trickles 1/sec even in combat, and a real latent bug fixed where
  mana regen had silently never worked in actual gameplay (S170-150). Founder, real-time:
  "have mana tic up slowly 1 per second always." New `ARENA_MP_REGEN_IN_COMBAT_PER_SEC` (1) --
  regen is no longer a hard on/off gate (S170-148's combat pause); it's now two rates, a slow
  trickle that runs even mid-fight and the faster out-of-combat rate once
  `combat_timer_ms` expires. **Real bug found while implementing this, not the literal ask**:
  the regen math was `h->mp += (int)(rate * dt_ms / 1000.0f)` computed fresh every call with
  no persistence across ticks -- at this codebase's own real production tick rate
  (`apps/arena_server` always calls `arena_update()`/`arena_update_teams()` with `dt_ms=16`),
  that's `(int)(6 * 16 / 1000.0) == (int)0.096 == 0`, EVERY single tick, for every rate this
  file has ever used. Mana regen had silently never actually worked in real gameplay -- only
  in tests, which happened to call with large `dt_ms=1000` "one full tick" steps that mask
  the truncation. Fixed with a persistent `mp_regen_accum` float on `ArenaHero`: fractional
  progress now carries over between ticks instead of being discarded each time. 3 new
  headless tests, including one that runs 63 real 16ms ticks specifically to catch this class
  of bug rather than a single large-dt_ms step. Full suite green (465 checks).

## 2026-07-28

- fix(arena): "wrong team wins" and "node flips wrong color" -- two real, high-impact
  team-mode bugs found from a live founder report (S170-149). Founder, real-time: "there are
  bugs with node ownership sometimes the wrong team comes out of a node sometimes the wrong
  team wins" -> "yea theres a bug where i cap a node but it flips wrong color and then my
  whole team comes out and then they kill the other team but it says i loose."
  - **Root cause #1 (the "i loose" bug)**: the "YOU WIN"/"YOU LOSE" HUD text
    (`apps/arena/src/main.c`) compared `arena_state.winner` (which encodes which TEAM won,
    1/2) against `my_owner + 1` -- `my_owner` is the raw client_id/hero SLOT INDEX (0..19 in
    a real 20-player match, only ever equal to team index by coincidence for owner 0, and
    only correct for owner 1 in the literal 1v1 case where owner IS team). Any real team-mode
    player past owner 1 -- the overwhelming majority of any real 10v10 match -- got a flipped
    result: shown "YOU LOSE" after their own team's real win, or "YOU WIN" after a real loss.
    Fixed: compare against `arena_state.heroes[my_owner].team + 1` instead.
  - **Root cause #2 (the "wrong color" bug)**: node coloring was hardcoded absolute
    (owner==1 always blue, owner==2 always red) while every HERO on the same map is colored
    RELATIVE to the local viewer (self/ally = blue-ish, enemy = red). For a team-0 viewer
    those two conventions happened to agree by coincidence; for a team-1 viewer, their OWN
    captured node rendered in the exact red already reserved for enemy heroes on their own
    screen -- a node they just captured looked identical to an enemy-held one. Fixed: nodes
    now color relative to the local viewer's own team, same "ally-blue / enemy-red" rule
    heroes already use, gold for neutral/contested unchanged.
  - Both are client-side display bugs, not sim-logic bugs -- the underlying win-condition and
    node-capture logic in `packages/simulation/arena_game.c` was already correct on audit (no
    changes there). **Verified live with the exact broken scenario**: a real 20-player match
    (19 bots + the actual SDL client under Xvfb) with the human client deliberately connected
    *last* so it claimed owner slot 19 (team 1, guaranteed NOT owner 0/1) -- match ended with
    `winner: 2` (team 1), screenshot confirms "YOU WIN" now displays correctly for this exact
    previously-broken slot. Client-only change; full headless suite unaffected (461 checks).

- feat(arena): mana visible on the HUD, combat-gated regen, fountains restore mana, and a
  real jungle-obstacles-disappearing bug fixed (S170-148). Founder, real-time, three requests
  plus a bug report in sequence:
  - **"mana as a resource should be visible to the player"**: a real persistent mana bar under
    the existing HP bar in the local player's own HUD corner (`apps/arena/src/main.c`) --
    before this, mana was only ever visible as occasional "MP" text replacing an ability
    tile's countdown when a cast was blocked, never as a standing resource meter. Uses
    `ARENA_MP_MAX` (not `h->max_mp`, which is deliberately not part of the wire snapshot --
    flat/roster-wide, the client already knows it) so it reads correctly in both local and
    net_mode.
  - **"it should slowly regenerate when not in combat"**: new `combat_timer_ms` on `ArenaHero`
    (`packages/simulation/arena_game.h`/`.c`), re-armed to `ARENA_COMBAT_TIMEOUT_MS` (4000ms,
    WoW-adjacent) by `apply_damage()` every time a hero takes damage from any source -- mana
    regen (`tick_hero_kit`) is now gated on this hitting 0, real WoW-style out-of-combat regen
    instead of the previous always-on flat tick. Honest simplification, flagged in the code:
    keyed off damage *taken*, not dealt -- threading an attacker-side signal through every
    damage call site in this file would be a much larger change for a rare edge case (pure
    safe-distance poking) real fights are overwhelmingly mutual, so this covers the vast
    majority of "am I actually fighting" correctly. The mana bar dims while in combat so the
    gate has a visible answer on the bar itself, not just implied by it holding still.
  - **"fountains should also restore mana"**: `arena_tick_fountains()` (S170-147) now restores
    `ARENA_FOUNTAIN_MANA_PER_SEC` (15, same rate as the heal) alongside HP, unconditionally --
    a fountain is a deliberate location-based resource, not gated by the new combat timer the
    way passive regen is.
  - **Real bug found and fixed, not requested but reported live**: "the first game i played i
    saw jungle rocks and trees but subsequent games were missing those." Root cause: the
    requeue-after-a-networked-match button (`apps/arena/src/main.c`) does a blanket
    `memset(&arena_state, 0, ...)` before reconnecting, which silently wiped the client's own
    `obstacles[]` -- obstacles are never wire-synced (client computes the same static layout
    independently, same precedent fountains use), so nothing ever repopulated it after that
    memset. `arena_obstacles_reset_layout()` made public (was `static`) so the requeue handler
    can call it directly; first match after program start was never affected (its own initial
    call happens before this bug's code path), every match reached via requeue was.
  - 6 new headless tests (fountain mana restore + cap, regen gated correctly both ways, damage
    re-arms the timer, timer counts down and pins at 0). Verified live: a real Xvfb screenshot
    confirms the mana bar rendering correctly under the HP bar. Full suite green (461 checks,
    up from 455).

## 2026-07-27 (continued)

- feat(arena): healing fountains at 2 opposite map corners (S170-147). Founder, real-time:
  "add healing fountains at 2 corners of the map across from each other." New
  `arena_fountain_position()` (shared source of truth for both the sim tick and the client
  renderer -- same "static, deterministic layout, no wire sync needed" precedent as jungle
  obstacles) places two fountains at diagonally-opposite corners `(-24,-24)`/`(24,24)`, clear
  of every jungle obstacle and within the hero movement clamp. `arena_tick_fountains()` heals
  any active, alive hero within `ARENA_FOUNTAIN_RADIUS` (3.0) for `ARENA_FOUNTAIN_HEAL_PER_SEC`
  (15) per second, fixed-interval tick same idiom as every other heal/DPS zone in this file,
  capped at max_hp. **Deliberately neutral, not team-exclusive** -- the founder's own wording
  ("2 corners... across from each other") described map geography, not "one per team's base"
  (which real MOBA fountains usually are); read as a genuinely contestable resource matching
  this map's existing neutral-structure pattern (nodes, jungle creeps), flagged as a real
  design choice in the code rather than silently assumed, easy to flip to team-exclusive later
  if that's what's actually wanted. Rendered client-side as a base+pillar silhouette in bright
  cyan (distinct from every other shape/color already on the map) -- reuses the heal-flash
  system from S170-143 automatically (fires on ANY HP increase, any source), so fountain
  healing already shows visual feedback with zero extra work. Wired into both `arena_update()`
  and `arena_update_teams()`. 5 new headless tests. Verified live with a real Xvfb screenshot
  of the local demo showing a fountain rendering correctly. Full suite green (455 checks, up
  from 450).

- feat(arena): jungle and lane creeps wire-synced to the network for the first time (S170-146).
  Continuing this session's own sprint plan ("wire-sync jungle creeps, lane creeps, and Tyler's
  clones... the single biggest 'looks unfinished in a live match' gap left by this session's own
  new work"). Before this, `ArenaSnapshotMsg` carried heroes/nodes/projectiles but neither creep
  pool -- a real networked match (the actual product, per NORTHSTAR §13) simply never showed
  either kind of creep at all, only the local 1v1 practice demo did. New `ArenaCreepSnapshot`
  (fixed 5-slot array, index-matched to nodes, mirroring `ArenaHeroSnapshot`'s always-populated
  convention) and `ArenaLaneCreepSnapshot` (sparse count+array, mirroring projectiles' own
  pack-only-active convention) in `packages/common/protocol.h`. `apps/arena_server`'s
  `server_broadcast()` populates both every tick; `apps/arena`'s `net_poll_snapshots()` consumes
  them into the same `arena_state.creeps[]`/`lane_creeps[]` this session's own S170-145 rendering
  code already reads generically -- no client rendering changes needed at all, that code was
  already mode-agnostic. New packet size: 1244 bytes (was 968), comfortably under both the
  client's 2048-byte recv buffer and typical UDP MTU. **Verified live, not just built clean**: a
  real `arena_server` + `arena_bot` + the actual SDL client (connected via `--connect`, running
  under Xvfb) played a full networked 1v1 match; a real Xvfb screenshot confirms a jungle creep
  (correctly gold/neutral-colored) rendering client-side over the live wire connection, not just
  in the local demo. Full headless suite unaffected (450 checks, protocol/broadcast/consume-only
  change, no sim logic touched).

- feat(arena): auto-attack hit flashes now fire on creeps too, and jungle creeps are
  rendered for the first time (S170-145). Founder, real-time: "when auto attacks hit a creep
  or a hero it should show visual indication of such." The hero-side hit flash already
  existed (S170-122, HP-delta detection); creeps had none at all. Added the same frame-to-
  frame HP-delta tracking for both jungle (`ArenaCreep`) and lane (`ArenaLaneCreep`) pools in
  `apps/arena/src/main.c`, reusing the existing `attack_flashes` visual (a hit is a hit).
  Along the way, found jungle creeps were never rendered client-side AT ALL (a real,
  previously-unfixed gap -- a hit-flash on an invisible creep would have been useless) --
  added real rendering for the first time: a flavor-colored box (gold/neutral, blue/red team,
  matching the node-ownership color convention exactly, not team-relative like heroes/lane
  creeps -- a jungle creep's color is about whose territory it's tied to). Verified with a
  real Xvfb screenshot of `red_garden_arena`'s local demo: a gold neutral jungle creep
  rendering correctly alongside the jungle-obstacle trees/rocks and a hero. Local-mode/1v1-
  demo only, same not-yet-networked scope jungle/lane creeps already carry. Full suite
  unaffected (450 checks, client-only change).

- feat(arena): AoE damage spells now hit creeps too, not just heroes (S170-144). Founder,
  real-time: "ensure aoe damage spells hit creeps." Before this, every zone/aura damage tick
  (Ghost's Recital, Pizza's always-on burn aura, Beleth's Detonation burst, Paimon's Two
  Hundred Legions, NOOR-1's Do Not Approach) only ever checked the single nearest-enemy-HERO
  parameter `tick_hero_kit` threads through -- an existing, already-flagged limitation (see
  Pizza's own aura comment) that also meant a zone dropped squarely on a jungle or lane creep
  did nothing to it. New shared `arena_zone_damage_creeps()`
  (`packages/simulation/arena_game.c`) applies flat damage to every living jungle creep AND
  lane creep within radius, called from all five damage-dealing zone/aura sites. Same
  team-exclusivity rules as melee (a team-flavored jungle creep or a lane creep is only a
  valid target for the OPPOSING team's zone; a neutral jungle creep is fair game for anyone).
  Zone kills grant no jungle-creep kill-credit reward (capture-bonus/heal) -- no single
  attributable hero slot in this simplified model, flagged not faked, same "not every damage
  source needs full reward wiring" precedent already accepted elsewhere in this file. 4 new
  headless tests, each deliberately positioned within zone radius but outside melee attack
  range to isolate the new zone-damage path from the existing, separate melee-vs-creep
  mechanics. Full suite green (450 checks, up from 446).
- **Live bot-mode verification, this session's whole batch (S170-139 through S170-144).**
  Founder: "verify it with bot mode." Ran a real `apps/arena_server --lobby-size 20` +
  20 `apps/arena_bot` match on freshly built binaries (fresh ports, isolated from the
  already-running persistent bot pool discovered earlier this session -- confirmed untouched
  before and after). Confirmed: all 20 bots connected and drafted distinct heroes cleanly
  (roster of 26 intact), a real 10v10 team split, and genuine sustained combat over 55+
  seconds with no crash -- 15 of 20 heroes actually died with real, varied HP values on the
  5 survivors (not a static/frozen snapshot). First attempt used `--lobby-size 6`, which
  surfaced a real (pre-existing, not caused by this session) operational gotcha worth noting:
  `arena_init_teams()` splits by `i < ARENA_TEAM_SIZE` (10), so any lobby smaller than 20 that
  isn't exactly 2 puts every player on team 0 with nobody on team 1 -- no combat is possible.
  Not a bug in code touched this session (confirmed unrelated to any of S170-139 through
  144), flagged here since it's a real trap for the next person who reaches for a "small
  team test" lobby size.

- feat(arena): WoW-style hover casting, starting with Doc Wheel's Q (S170-143). Founder,
  real-time: "add hover casting like in wow macros for healing start with doc wheel abilities
  that make sense for that ensuring we show cast animation on the target and the self so its
  legible to all heroes on the battlefield with visibility of that interaction." New
  `arena_hover_ally_or_nearest()` (`packages/simulation/arena_game.h`/`.c`): a drop-in
  fallback-chain replacement for `arena_nearest_ally()` -- prefers whoever the caster's
  `ArenaState.hover_target[owner]` names, if it's a valid living same-team hero other than the
  caster, else behaves identically to the old always-nearest-ally targeting. `ArenaCastCmd`
  (`packages/common/protocol.h`) gained a signed `hover_target` byte, set by
  `apps/arena_server`'s cast handler via the new generic `arena_set_hover_target()` (any slot
  could consult it; only Doc Wheel's Q does today) and by the local 1v1 demo's own direct
  keybind path for parity. Client-side: `apps/arena/src/main.c`'s existing S170-69 per-hero
  hover hit-test now publishes its result into a persistent `g_hover_target` each frame, read
  by the QWE keybind handler when a cast actually fires (~1 frame of latency, imperceptible).
  "Show cast animation on the target and the self": the caster's own flash already existed
  (`cast_flash_slot`, S170-124); added a new generic heal-flash (any HP increase, any source,
  reusing the exact same frame-to-frame-HP-delta idiom S170-122's attack-flash already
  established for damage) that fires at wherever the HP increase actually landed -- the real
  gap a mouseover heal exposes, since the target can be standing far from the caster. 6 new
  headless tests (fallback with nothing hovered, hover wins over nearer un-hovered ally, hover
  of an enemy/dead hero safely falls back, out-of-range owner is a no-op, full Doc Wheel Q
  integration). Full suite green (446 checks across 4 binaries, up from 439).

- feat(arena): lane creep waves (S170-139), Ghost's Q + Tyler's Q converted to real
  projectiles (S170-140), Tyler's puppet clones ("true Meepo parity," S170-141), per-hero
  cast-flash colors (S170-142), rooted name-label color, and a merge of four parallel
  worktree branches into one coherent mainline. Founder, real-time, several requests in
  sequence across a long session:
  - **Lane creep waves** ("add subsystems needed to make creeps a reality" -> clarified as
    classic MOBA lane-pushing waves, distinct from S170-51's jungle creeps): new
    `ArenaLaneCreep` pool, a per-team wave spawn timer (with a real short grace period before
    the first wave, matching real MOBA precedent), waypoint marching along the existing
    spawn-to-center-to-spawn axis, hero-vs-lane-creep and lane-creep-vs-lane-creep combat
    through the same generic combat primitives every other system already uses. Team mode
    only -- no real "push" objective exists in the 1v1 practice demo. 9 new headless tests.
  - **More projectile conversions** ("convert more spells to projectiles... ensure each
    spell is unique show different color cast circles... ensure spell projectiles are shown
    on all player clients"): Ghost's Q (Alien Frequency, already documented as a "skillshot"
    but never built as one) and Tyler's Q (Earthbind, "fires a net at a target area") both
    converted from instant-hit to real `ArenaProjectile` casts, carrying on-hit status
    effects (silence / root+burn) via new generic `on_hit_silence_ms`/`on_hit_root_ms`/
    `on_hit_burn_ms`/`on_hit_burn_dps` fields and `arena_spawn_projectile` now returning a
    pointer so callers can set them. Found and fixed a real tunneling bug in
    `arena_tick_projectiles` along the way: a position-only collision check let a fast shot
    skip clean past a target during a single large-`dt_ms` tick (exposed by
    `test_ghost_r_zone_damages_foe_over_time`'s own `arena_update(1000)` call) -- replaced
    with a proper swept segment-vs-point check. Cast-flash particles now colored per-hero
    (golden-angle HSV hue rotation, deterministic, no table to maintain as the roster grows)
    instead of just per-slot, so 26 heroes' worth of casts read as genuinely distinct spells
    -- already broadcast to and rendered by every connected client with zero additional wire
    work needed (confirmed by reading the existing pipeline, not assumed). 7 new headless
    tests (Ghost + Tyler Q projectile behavior).
  - **"when the hero is rooted change the color of their name label to green"**: small,
    isolated HUD tweak in `apps/arena/src/main.c`.
  - **"add tyler true meepo parity" -> "do that work"**: real AI-driven puppet clones, not
    faked. `ARENA_MAX_CLONE_SLOTS`, a small pool of hero slots appended after the real
    per-player range so a clone never competes with an actual connecting client for a slot.
    Clones mirror Tyler's own move-target every tick and fight through the exact same
    generalized `arena_nearest_enemy`/melee loop every hero already uses (widened to see the
    puppet range) -- no parallel combat system needed. Real shared-fate death for the first
    time: `apply_damage`'s death branch cascades the kill through every `clone_owner`-linked
    entry, the literal OG "one dies, all die" rule, no exceptions. Team mode only; clones are
    melee-only (no independent kit casts) and don't count toward team-alive/respawn checks.
    Full design/scope note (including what's still simplified) in `docs/HEROES_VS0.md`'s
    Tyler section. 7 new headless tests.
  - **Merge reconciliation**: this session's work landed in its own worktree branch in
    parallel with three sibling sessions' branches (S170-138 jungle obstacles/map expansion,
    the QWER-ready-indicator net_mode fix, and translucent-while-intangible rendering) that
    had all been sitting unmerged. Founder: "you did some work in branches that all needs to
    be folded into mainline i dont work in branches currently." All four merged into `main`
    directly (no PRs) in dependency order; the only real conflicts were this session's own
    map-expansion pass (`ARENA_HALF_EXTENT` 20->30, decorative non-colliding trees) against
    the sibling jungle-obstacles branch's more complete version (20->28, real collision) --
    resolved by dropping this session's redundant map/tree work entirely and reconciling
    lane creep waypoints against the (unchanged) +-8 team spawn line their branch left in
    place. Full headless suite (439 checks across all 4 test binaries) green after
    reconciliation; `scripts/test_10_bots.sh` (unrelated card-RTS path) unaffected.

- feat(arena): jungle obstacles -- rocks/trees carve the map into lanes (S170-138). Founder,
  real-time: "expand the map and add rocks and trees etc so we start to get a bit of a jungle vibe
  - just use boxes for now like in shankpit so we naturally start to create some lanes." Widened
  `ARENA_HALF_EXTENT` 20->28 and rescaled the 5-node layout (Stables/Farm/Lumber Mill/Gold Mine now
  at x=+-18, z=+-11) to give the jungle room without cramming it against the 1v1 mid lane. New
  `ArenaObstacle` type (`packages/simulation/arena_game.h`/`.c`): 22 static rock/tree boxes in two
  mirrored walls between each team's spawn column and that side's flank nodes, spanning roughly
  z=-5.5..5.5 -- wide enough that reaching a flank node means routing around the top or the bottom,
  the actual "lanes" asked for, plus a handful of decorative pieces scattered past the nodes for
  jungle-vibe dressing. Real collision, not just decoration: `resolve_hero_obstacle_collision`
  (simple circle-vs-circle push-out) is called from the same shared `update_hero_motion()` both
  `arena_update()` (1v1) and `arena_update_teams()` (team mode) already use, so both the local demo
  and `apps/arena_server`'s networked matches get identical, consistent terrain with no wire sync
  needed (the layout is static and deterministic, built the same way client- and server-side).
  Client-side (`apps/arena/src/main.c`) renders trees as a trunk+canopy box pair (same silhouette
  idiom as the `ARENA_HERO_TREE` hero model) and rocks as a single squat grey box. Obstacle
  placement deliberately never crosses the x=0 mid lane or the 1v1 local demo's own movement-test
  coordinates, so the full `test_arena.sh` suite (390 assertions) passes unchanged. Verified with a
  live Xvfb screenshot of `red_garden_arena` showing both jungle walls rendering correctly around
  the mid-lane fight.

- feat(arena): first real projectile skill-shot -- Gary's Q (S170-136). Founder, real-time: "we
  need to add spell animations and projectiles for some of the spells - some of the spells
  obviously should be projectile skill shots instead of instant cast - find one such spell - start
  with gary q" -> "it should be a projectile skill shot with animations and affordances that allow
  dodging as counterplay." New `ArenaProjectile` pool (`packages/simulation/arena_game.h`/`.c`,
  `arena_spawn_projectile`/`arena_tick_projectiles`) -- straight-line, no homing: velocity is fixed
  at cast time toward the foe's position then, so a foe that moves off the line before the shot
  arrives genuinely dodges it. Wired into both `arena_update()` and `arena_update_teams()`, same
  convention as `arena_tick_creeps`. Gary's Q ("The Property") rewritten to spawn a projectile
  instead of instant-hitting; cooldown still spent on cast regardless of outcome, matching every
  other ability. New `ArenaProjectileSnapshot` in `packages/common/protocol.h`, broadcast by
  `apps/arena_server`, rendered client-side in `apps/arena` as a small bright cube (color-coded
  self/ally/enemy same as heroes, so an incoming enemy shot reads as an immediate visual threat --
  the actual dodge affordance this was built for). 5 new tests (cast spawns a projectile with no
  instant damage, out-of-range cast whiffs with no projectile and no cooldown spent, a stationary
  target is hit after real travel time, a target that steps off the line takes no damage, an unhit
  shot despawns cleanly past its max range). Verified: `scripts/build.sh`, `scripts/build_arena.sh`,
  `scripts/test_arena.sh`, `scripts/test_10_bots.sh` all pass; local mingw cross-compile (all 4
  source files) links clean. **Not yet deployed to the live services** -- the founder's own match
  was in progress when this was built; redeploying now would kill it. Deploy after their match ends.
- fix(arena): Q/W/E ability tiles now reflect real readiness in networked play (S170-137).
  Founder, real-time: "QWER animation frames need to indicate visually if an ability is ready to
  cast or not." Root cause: the ability-tile HUD (S170-127) already dims/wipes/counts down
  correctly, but `ArenaSnapshotMsg` (`packages/common/protocol.h`) never carried cooldown or mana
  state — in net_mode the client never runs `arena_update()` locally (the server owns the sim),
  so `q/w/r_cooldown_ms` and `mp` for the local player's own hero sat zeroed forever and every
  ability rendered permanently "ready" regardless of actual server-side state, in the one mode
  (real online play) where the tiles' answer matters most. Added the four missing fields to
  `ArenaHeroSnapshot`, populated them in `arena_server`'s `server_broadcast()`, and consumed them
  in `net_poll_snapshots()`. Also closed a second, independent readiness gap from the mana layer
  (S170-132): an ability can be off cooldown and still unaffordable, which previously still read
  as fully "ready." `draw_ability_tile()` now takes a `mana_blocked` flag (checked against this
  slot's flat `ARENA_MP_COST_Q/W/R`) and dims the tile the same as a real cooldown, but shows "MP"
  instead of a countdown number since there's no fixed timer to animate. Verified: `build.sh` and
  `build_arena.sh` clean, full headless suite (`test_arena.sh`) all-pass, and a live
  server+2-bot match over the actual network path completed end to end with the new, larger
  snapshot struct (580 bytes/packet, well under both the 2048-byte recv buffer and typical UDP
  MTU).
- feat(arena): heroes render translucent (35% alpha) for the duration of the shared
  `intangible_ms` untargetable status (Ghost's Not a Ghost, Frog's R vanish, Bacon Puck's Q),
  on top of the existing INTANGIBLE text tag above the health bar. Blends only the affected
  hero's boxes (GL_BLEND on, depth writes off) for that draw, same convention already used for
  the ring/flash effects; every other hero stays fully opaque with normal depth writes.

## 2026-07-25 (33)

- feat(arena): MnM, the Shapeshifting Crab, 26th hero — Tank (S170-134). Founder, real-time: "add
  MnM a shapeshifting rapping crab tank from detroit to the lore docs first" → "have tyler and
  mid-piano cowrite it." Lore landed first (TYLER `multiverse_heroes.md` #114, framed as
  literally co-written by Tyler and Mid-Piano); this is the follow-on kit pass. Passive flat
  armor (Cain's/Gunnr's/Beleth's shape), Q a melee root+poke (Paimon's Q shape), W a free toggle
  bonus armor stacking on top of the passive (Loki's/Ada's shape, but additive rather than
  replacing), R the literal mechanical translation of the lore's own line — Mid-Piano's framing
  that the shapeshifting is just what happens to a body that's absorbed hits meant for somebody
  else, built as self-root + a guaranteed-survival window (`survive_floor_ms`, same real damage
  floor as Pizza's R) combining two existing generic fields the same way Tree's Grand Secret
  does. `ARENA_HERO_COUNT` 25 → 26. 6 new headless tests, including one that actually fires a
  real lethal Duck Q at a 1-HP MnM under R and confirms it survives at 1 HP — not just that the
  flag gets set. Verified: full suite (379 checks), VS0/VS1 stable, live — restarted the three
  systemd units, confirmed a real 20-player match drafted MnM (hero_id 25) among 20 distinct
  picks with zero duplicates.

## 2026-07-25 (32)

- feat(arena): status-effect text label above the health bar (S170-133). Founder, real-time:
  "text label above health bar above hero shows status effects like stun silence root slow etc."
  New `hero_status_label()` composes a short space-separated tag string (SILENCED, ROOTED,
  INTANGIBLE, BURNING, UNKILLABLE) from whichever generic status-effect fields — already shared
  across every hero's kit — are currently active, drawn above the existing name label, only when
  there's something to show (no empty-line clutter on the common case). "Stun" and "slow" aren't
  modeled as their own generic fields in the sim yet (only silence/root/intangible/burn/
  survive-floor exist today) — this surfaces what the sim actually tracks rather than inventing
  new effect types as a side effect of a HUD task; a real stun/slow mechanic would be separate
  kit work. Purely client-side (`apps/arena/src/main.c` only), no protocol/server changes.
  Verified: clean build, full headless suite (366 checks) unaffected.

## 2026-07-25 (31)

- docs(arena): Weatherman + Donkey spec, NORTHSTAR §16 (S170-93) — spec only, no code.
  Scoped via AskUserQuestion to spec-first: Donkey is documented Indirect-Control (never
  owner-piloted, auto-triggers on HP threshold and an escape condition) and blocked on a
  non-piloted-unit system that doesn't exist in `arena_game.c` yet — every hero implemented so
  far is owner-piloted. §16.1 specs what that companion-slot system would actually need
  (folded/unfolded state derived from the owner, not input; a per-tick trigger check shaped like
  `arena_tick_respawns`; collision rules reusing `hero_is_hittable`; a second-model-per-owner
  render path that doesn't exist today). §16.2 is Weatherman's full kit, written from scratch
  (TYLER `multiverse_heroes.md` #45, zero prior kit writeup existed) — passive flavor-only
  ledger, Q a displacement-only wind knockback (the roster's first push instead of pull/damage),
  R a fixed-zone AoE ultimate. §16.3 is the specific interaction the founder asked for: W reads
  ally-vs-enemy on a target currently airborne via Donkey's Paper Glide — grounds an enemy
  mid-escape, extends an ally's flight instead — same "same ability, opposite effect depending on
  team" precedent Ghost's Recital already set for this roster. Nothing built; the companion-slot
  system is the real prerequisite before either hero can wire in for real.

## 2026-07-25 (30)

- feat(arena): mana resource layer, roster-wide (S170-132). Founder, real-time: "add mp so
  toggling stuff has a cost spells cant be spammed unless its a zero mana spell or ability." A
  second resource layered on top of every existing cooldown, not a replacement for them — a
  Q/W/R can be fully off cooldown and still blocked for lack of mana. `mp`/`max_mp` added to
  `ArenaHero` (100 max, regenerates ~6/sec, full at spawn and on respawn). Flat per-slot costs
  (Q 20, W 20, R 45) applied uniformly across all 25 heroes by hooking the codebase's own
  existing `cast_cooldown()` choke point — every Q/W/R cooldown-assignment site in the file
  already routed through it, so a scripted pass inserted `h->mp -= COST` at all 63 call sites
  (25 Q + 13 W-decree + 25 R) without touching per-hero cast logic. Free-toggle W's (Gunnr, Flute
  Debt, He Xiangu, Loki, Gary, Bacon+Puck, Abraham, Ada, Unicorn — 9 total) previously had zero
  cost at all; per the founder's own framing ("toggling stuff has a cost"), activating one now
  costs the flat W rate — deactivating stays free, since canceling isn't casting a new spell. A
  cast blocked by insufficient mana behaves exactly like a whiff: no cooldown starts, matching
  the codebase's own established "whiffed casts don't consume the cooldown" precedent extended
  to the new resource. No ability is flagged zero-mana yet, but the cost lives behind named
  per-slot constants rather than inlined values, so the founder's "unless it's a zero mana spell"
  exception is a one-line change away whenever a specific ability needs it, not a redesign. 12
  new headless tests (starts-full, regen, landed-cast deduction, insufficient-mana block behaves
  like a whiff, toggle-activate-costs/deactivate-is-free, toggle-blocked-when-insufficient).
  Verified: full suite (366 checks), VS0/VS1 stable, live — restarted the three systemd units,
  confirmed two clean real-match connects with no instability (a burst of `SIGKILL`s in the
  service journal around this same window traced back to my own rapid manual `systemctl restart`
  calls during testing, not the new code — confirmed by 70+ seconds of clean, event-free
  operation once the manual restarts stopped).

## 2026-07-25 (29)

- feat(arena): Beleth, the Detonation, 25th hero — Burst/Control (S170-93). Fourth and final hero
  shipped from the batched "next wave" backlog entry. Real TYLER canon (`multiverse_heroes.md`
  #14) — 2.22 Hz, the emotional-detonation frequency behind every love triangle in the show's
  history. Passive flat armor (Cain's/Gunnr's shape), Q a ranged bolt + burn (Pizza's Q shape), W
  an instant silence-only decree on the nearest enemy (Paimon's W shape with the damage stripped
  out — pure escalation-denial). R is the roster's first genuinely delayed-payoff ultimate: marks
  the target's position at cast time and starts a silent fuse via `r_active_ms`; the instant it
  hits zero, whoever's still in the zone takes one large one-time burst — not a continuously-
  ticking zone like every other zone hero on this roster. `ARENA_HERO_COUNT` 24 → 25. 6 new
  headless tests. Two real test bugs found and fixed before landing: the fuse-detonation test
  originally ran in the 1v1 local-demo path, whose `arena_update` runs an autonomous chase-bot
  by default that closed distance to melee range well within the 1.8s fuse window, contaminating
  the burst-damage assertion with an extra melee trade — moved to team mode, which has no such
  chase AI. Second: even after that fix the damage still came up 4 short, because
  `arena_init_teams()` leaves every hero at its own `ARENA_HERO_UNICORN` placeholder id until a
  real draft pick overrides it, and Unicorn carries a flat +4 armor passive that was silently
  eating part of the burst — fixed by giving the test target an armor-less hero_id explicitly.
  Verified: full suite (352 checks), VS0/VS1 stable, live — restarted the three systemd units on
  the freshly built binaries, forced a real 20-bot match, confirmed 20 distinct hero picks with
  zero duplicates at the new 25-hero roster size (Beleth wasn't in this particular match's 20 of
  25, which is expected and not a bug — the port-derived draft-offset scheme this session already
  proved for hero counts exceeding lobby size continues to hold).

## 2026-07-25 (28)

- fix(arena): unique skinmodels for all 24 heroes (S170-131). Founder, real-time: "ensure all
  characters have unique skinmodels." Audit of every `draw_hero_model()` case (all 24 present,
  none missing) found two real near-duplicate pairs sharing an identical base-body box plus
  visually-confusable accents: Gary and Abraham both used the same 0.8×1.3×0.8 body with a flat
  slab accent in the same chest position (clipboard vs. grimoire, indistinguishable as low-poly
  boxes); Cain and Tyler both used the same 0.75×1.3×0.75 body, with Tyler deliberately bare (per
  his own lore, "unremarkable plain humanoid") and Cain's mark accent only a 0.14-unit cube on the
  shoulder — easily lost against Tyler's identical bare silhouette at gameplay camera distance.
  Fixed: Gary's accent replaced with a long rifle/scope bar held out to the side (fits his
  marksman kit — "no dash, no gap-closer... watches from where he's standing" — better than a
  chest slab anyway); Abraham gained a second small floating orb accent above his grimoire
  (arcane-caster read, no longer just a flat book matching Gary's old shape); Cain's mark moved
  to the forehead and enlarged (0.14 → 0.22, more Genesis-accurate than a shoulder detail, and
  now reads clearly against Tyler's bare body). Verified: clean build (`scripts/build.sh`,
  `scripts/build_arena.sh`), full headless suite (337 checks) unaffected — purely a visual/
  client-side change, no sim logic touched.

## 2026-07-25 (27)

- feat(arena): charming squish (squash-and-stretch) animations for movement, hits, and spell
  casts (S170-128). Founder, real-time: "add charming squish animations" → "for movement also
  spell casts." `draw_hero_box`/`draw_hero_model` now take a `squish` factor that scales the
  hero's stacked-box silhouette non-uniformly — Y compresses, X/Z inversely expand (clamped to a
  0.4f floor so it never inverts), keeping the model visually grounded like a classic animation
  bounce rather than a uniform shrink. Three triggers reuse existing per-frame detection instead
  of adding new state machines: taking damage (already detected via the S170-122 HP-delta hook),
  casting a spell (already detected via S170-124's `cast_flash_slot`), and starting to move (new
  — `h->moving` transitioning false→true, same transition-detection idiom as the HP-delta check
  in the same loop). `compute_squish()` is a decaying-cosine bounce curve over a 260ms window.
  Real bug found before it ever shipped: `squish_age_ms[]` zero-initializes with static storage,
  and 0.0f is `compute_squish`'s "just triggered" value, not its neutral one — every hero would
  have appeared squashed for a frame at launch with no trigger fired. Fixed with an explicit
  init loop pushing every slot past the animation window before the render loop starts. Purely
  client-side (`apps/arena/src/main.c` only) — no protocol/server changes. Verified: clean build
  (`scripts/build.sh`, `scripts/build_arena.sh`), full headless suite (337 checks) and VS0/VS1
  10-bot stability both pass unaffected — this feature has no server-testable component, same
  "verified via clean build + full suite" pattern as S170-69/S170-92/S170-127.

## 2026-07-25 (26)

- feat(arena): He Xiangu, 24th hero — Support/Sustain (S170-93). Third hero shipped from the
  batched "next wave" backlog entry. One of the traditional Eight Immortals — subsists on
  mother-of-pearl and moonlight, self-denial never once framed as sacrifice. Passive small HP
  regen (Dagda's shape), Q a ranged bolt that heals her for a fraction of its own damage (Bacon+
  Puck's heal-pct mechanic, repeatable on Q instead of a one-off R burst — the first real
  sustain-through-combat on the roster), W a free-toggle second regen layer (Flute Debt's shape),
  R a heal-only zone with no enemy damage (the mirror of Vassago's silence-only R — pure support
  vs. pure control). `ARENA_HERO_COUNT` 23 → 24. 5 new headless tests. Verified: full suite
  (337 checks), VS0/VS1 stable, live — confirmed a real match drafted 20 distinct heroes with
  zero duplicates (He Xiangu included), streamed real snapshots with no crash.

## 2026-07-25 (25)

- feat(arena): Vassago, 23rd hero — Support/Diviner (S170-93). Second hero shipped from the
  batched "next wave" backlog entry. Vassago is real TYLER canon, not just a lore entry — the
  Eastwind Owls' whole working frequency (11.11 Hz) is his, named directly in `TYLER/CLAUDE.md`'s
  own Goetia frequency table. Passive small HP regen (Dagda's Undry shape), Q a ranged
  damage+silence bolt (Ghost's Q shape), W grants the nearest ally `next_cast_refund` (Frog's
  Borrowed Time mechanic — the first hero on this roster to make that ability its own primary W,
  not incidental), R a fixed zone that's silence-only with **no damage component at all** — the
  first purely-control ultimate on the roster, every prior zone (Ghost/Flamel/Morrigan/Paimon/
  NOOR-1) deals damage. `ARENA_HERO_COUNT` 22 → 23.

  Found and fixed a real design issue while writing the R-zone test: the first-draft silence
  duration (900ms) was shorter than the zone's own 1000ms re-application tick, which would have
  left real gaps where a continuously-standing enemy isn't actually silenced between ticks —
  caught by comparing against Flamel's own proven `ARENA_FLAMEL_R_ROOT_MS` (1200ms, deliberately
  longer than its 1000ms tick), same margin now applied here.

  6 new headless tests. Verified: full suite (324 checks), VS0/VS1 stable, live — rebuilt +
  restarted all three systemd units, confirmed a real match drafted 20 distinct heroes with zero
  duplicates (Vassago, Cain, and Gunnr all included — 23 heroes now means exactly 3 are excluded
  per match), streamed real snapshots with no crash.

## 2026-07-25 (24)

- feat(arena): Gunnr, 22nd hero — Duelist (S170-93). First hero shipped from the batched
  "next wave" backlog entry (Xiangu, Gunnr, Drowned Prince, Vassago, Beleth, Weatherman+Donkey
  interaction) — the rest queued for follow-up passes. Gunnr already has a real entry
  (`multiverse_heroes.md` #30): a shieldmaiden with no magic of her own who correctly disagreed
  with one of Odin's own ravens, then was quietly right about three more things since, none of it
  acknowledged. Passive flat armor (same shape as Cain's/Unicorn's), Q a plain melee-range strike
  (no status effect, "a correction, not a flourish"), W a free toggle self-regen (same shape as
  Flute Debt's Recouping Interest), R an execute-scaled burst (same shape as Morrigan's/Cain's Q).
  `ARENA_HERO_COUNT` 21 → 22. Draft-modulo needed no manual update this time — the port-derived
  shared-offset fix from S170-105 already scales with `ARENA_HERO_COUNT` automatically. 6 new
  headless tests. Verified: full suite (311 checks), VS0/VS1 stable, live — rebuilt + restarted
  all three systemd units, confirmed a real match drafted 20 distinct heroes with zero duplicates
  (Gunnr and Cain both included, since 22 heroes now means exactly 2 are excluded per match, not
  1), streamed real snapshots with no crash.

## 2026-07-25 (23)

- feat(arena): Cain, 21st hero — Duelist (S170-105). Founder, real-time: "add Adelle" → "to the
  guide in tyler first" → "and then to the game" → "then the boys do a podcast with her." "Adelle"
  had zero anchor anywhere in the TYLER corpus (every other hero this session maps to a real
  mythological/historical figure or an existing lore file) — asked which identity anchor to use;
  founder's answer: "replace adelle with Cain." Cain already has a real entry (#80) — Genesis's own
  account: killed his brother Abel, cursed to wander as a fugitive, marked by the same authority
  that cursed him specifically so no one could kill him in turn ("a punishment that is also,
  unmistakably, a mercy"), then founded the first city anyway. No new lore needed. Passive flat
  armor (same shape as Unicorn's own, "the one permanent thing about a man cast out to wander"), Q
  an execute-scaled bolt ("The First Murder," same shape as Morrigan's Q), W a dash directly *away*
  from the nearest enemy + self-cleanse ("Cursed to Wander," the mirror of Courier's Q), R a
  survive-floor panic button ("The Mark," same shape as Pizza's/Loki's R — the curse-and-mercy
  duality made literal). Distinct silhouette: weathered wanderer body + a small marked accent.
  `ARENA_HERO_COUNT` 20 → 21.

  **Real, live-found structural bug, fixed in the same pass:** with 21 heroes now exceeding
  `ARENA_MAX_HEROES` (20) for the first time, the existing `owner % hero_count` auto-draft scheme
  broke — a full 20-player lobby only ever has owner slots 0..19, so that mapping could *never*
  produce hero_id 20 no matter how many matches ran; not a rare miss, a permanent, deterministic
  exclusion, confirmed live (Cain never appeared across a real match with the old scheme). First
  fix attempt (a per-bot random offset) was itself wrong in a different way: every bot in the same
  match rolling its own independent random value meant two different owners could land on the same
  hero_id by coincidence, a duplicate-pick risk that didn't exist before. Corrected to a
  *shared* offset derived from the match's own connected port (every client in a match already
  knows the same port) — deterministic per match, varies match to match, zero coordination needed,
  zero duplicate risk. Verified live end to end: a real match post-fix drafted all 20 distinct
  heroes with zero duplicates, and Cain (`hero_id=20`) was actually picked. Incidental fix along
  the way: `apps/arena` never called `srand()` anywhere, so its own ticket-nonce randomness
  (`mint_ticket_fallback`) was silently identical every launch — now seeded for real.

  5 new headless tests. Verified: full suite (299 checks), VS0/VS1 stable, live — rebuilt +
  restarted all three systemd units, real match traffic confirmed the draft fix and Cain's
  reachability directly against the live matchmaker log, not just headless tests.

## 2026-07-25 (22)

- feat(arena): NOOR-1, 20th hero — Scout (S170-104). Founder, real-time: "add NOOR-1 as a
  snowman." NOOR-1 ("Four Days Behind", `multiverse_heroes.md` #3, Jiangshi Syndicate MUNDANE)
  already has a real lore entry — an operative sent in clean, being read by her own subject
  before she's filed a word on him. "As a snowman" read as an in-game FORM directive, the same
  convention the original roster already uses (a Duck, a Unicorn, a Pizza, a Tree), not a lore
  change. Full kit, fully wired end to end this pass (not left partial like Paimon originally
  was): passive periodic-silence aura (same idiom as Pizza's/Paimon's, themed as reading the
  enemy's next move before they commit to it), Q a ranged damage+root bolt, W a self-cast
  intangibility on its own cooldown (same mechanic as Ghost's Not a Ghost, themed as "sent in
  clean" — going quiet and unreadable herself), R a fixed cold zone dealing periodic damage with
  no ally-heal side ("do not approach" is one-sided). Distinct 3D silhouette: three stacked boxes
  of decreasing size, the literal snowman form. `ARENA_HERO_COUNT` 19 → 20; pick-validation bound,
  draft-modulo (both bot and human clients), name/ability-name tables all updated in the same
  pass — every gap Paimon's initial landing left behind (S170-55) closed here from the start. 5
  new headless tests. Verified: full suite (289 checks), VS0/VS1 stable, live — rebuilt +
  restarted all three systemd units, confirmed NOOR-1 (`hero_id=19`) actually gets drafted in a
  real 20/20 match and the match runs stably with real snapshots streaming, no crash.

## 2026-07-25 (21)

- feat(arena): Overwatch-style recast-time tiles for Q/W/E (S170-127). Founder, real-time: "add
  the ability frame cooldown timer tiles from shankpit og engine as recast time affordances" ->
  "make it like overwatch recast frames for q w e." Replaced the plain three-line text HUD
  ("Q: NAME [CD]") with real square ability tiles. Visual language ported from SHANKPIT's own
  `apps/lobby/src/main.c` `draw_ability_one_tile()` (bordered square, background/border color
  swap on cooldown, big centered countdown number, keybind label below) plus a real radial
  cooldown wipe on top -- SHANKPIT's tile was for one hero's one fixed-length ability; REDGARDEN
  has 19 heroes across 3 slots with cooldowns ranging roughly 2s-26s+, where "how much is left"
  matters more than a flat color tint alone shows. No per-hero max-cooldown table exists
  client-side to compute that fraction against, so it's tracked locally instead:
  `draw_ability_tile()` remembers the highest `cooldown_ms` seen since it last hit 0 (arms the
  instant a cast starts counting down from its real peak) and wipes that fraction away as a dark
  wedge sweeping clockwise from 12 o'clock -- self-correcting per-slot, no new wire data needed.
  W's tile lights bright toggle-green while `w_active`, matching the existing "W is ON" HUD
  convention it replaces. Ability-name caption kept (S170-96/S170-112's "show which real ability
  that is" work), drawn small below each tile -- known limitation, not hidden: several hero names
  are long enough to visually overflow the caption's tight column width at this tile size, a
  cosmetic issue only, worth a follow-up pass if it reads as messy in practice. Client-only
  change, no protocol/server changes. Verified: clean build, full headless suite (277 checks),
  VS0/VS1 stable. No headless test possible for the actual rendered tile layout.

## 2026-07-25 (20)

- feat(arena): small musical sound effects for gameplay legibility (S170-92). Founder, real-time:
  "add little musical sound effects to redgarden to add legibility via midi." Real scope decision
  made, not guessed: raw SDL2 core audio (`SDL_OpenAudioDevice`/`SDL_QueueAudio`), no SDL2_mixer.
  The backlog item's own open questions (new mixer dependency? second DLL to bundle in `PLAY.bat`'s
  zip alongside SDL2.dll?) both dissolve if nothing new gets linked at all -- SDL2 core already has
  an audio subsystem, already ships in every existing build. "Via midi" read as "short, distinct
  musical notes per event," not literal `.mid` playback -- a procedurally-synthesized sine tone per
  cue is the honest match at this scope ("little," per the founder's own word). Two cues: a short
  low thud (220Hz) on any hit landing, and an ascending A4/C#5/E5 triad per ability slot (Q/W/R) on
  cast -- mirrors the spell-flash color tiers (S170-124) in sound, so which slot just fired reads
  even without looking at the cast location. Gated to a ~15-unit hearing radius around the local
  player's own hero -- unfiltered, a real 20-hero match's several-casts-per-second would be noise,
  not legibility. Graceful degradation: no audio device available (this box is headless; a real
  player's box might also lack sound hardware) means every `play_tone()` call is a silent no-op,
  never a crash. Client-only change (`apps/arena`) -- no protocol/server/bot changes, no live
  systemd restart needed. Verified: clean build (both native and, per the existing CI workflow's
  own mingw step, expected to cross-compile cleanly since only core SDL2 functions are used --
  not locally re-verified, mingw isn't installed on this box), full headless suite (277 checks),
  VS0/VS1 stable. No headless test possible for actual audio playback, same limitation as the
  earlier hover-cursor/flash-animation work.

## 2026-07-25 (19)

- feat(ops): auto-deploy the live arena binaries on green CI (S170-100). Founder, real-time:
  "ensure the server version always stays current with the currently latest passing build." New
  `scripts/auto_deploy.sh`, polled by `redgarden-auto-deploy.timer` (every 10 min, 6 GitHub API
  calls/hour, well under the unauthenticated rate limit) + `.service`. Real design, not a quick
  bolt-on, given tonight's own S170-84 CI-hang incident:
  - Operates on a **separate checkout** (`~/redgarden-deploy`), never the interactive dev
    directory this script lives in -- an automated `git checkout <sha>` against the same
    directory active development happens in would risk clobbering uncommitted work or racing a
    build.
  - Only ever considers GitHub Actions runs with `status=completed` AND `conclusion=success` --
    guards against exactly the CI-hang failure mode that can sit "in_progress" indefinitely.
  - **Re-verifies locally before touching anything live** -- rebuilds and reruns
    `test_arena.sh`/`test_10_bots.sh` against the fresh checkout rather than trusting CI's word
    alone; aborts and leaves the old binaries in place on any local failure.
  - Publishes via copy-then-rename, not a direct overwrite -- found live, first real run: a
    direct `cp` onto `red_garden_arena_bot` hit `ETXTBSY` because the 19-bot pool has that binary
    mapped the whole time it runs. `rename()` atomically repoints the path to a new inode;
    already-running processes keep the old one mapped until they're actually restarted.
  Verified live, real end-to-end run (not simulated): bootstrapped the deploy checkout, found and
  deployed the latest green SHA, restarted all three systemd units, confirmed a real 20/20 match
  still forms afterward. Second run correctly no-ops ("already deployed... up to date"). Timer
  installed and enabled for real.

## 2026-07-25 (18)

- docs(northstar): spec camera lock/unlock + fog of war (S170-125), no code yet. Founder,
  real-time: "specdd unlockable and lockable camera and fog of war." Asked and confirmed scope
  before writing: spec only (same treatment as §14's draft-ban thread), and if/when fog of war
  gets built, client-side visual only for a first pass, not real server-side vision culling. New
  `NORTHSTAR.md` §15: camera lock proposal (hard-center on the local hero, `C` to toggle, open
  question on whether zoom stays free while rotation locks), fog-of-war proposal (radius-based
  hard cutoff around the local hero, allies always visible, honestly scoped as "the stock client
  chooses not to render this" rather than real anti-cheat), and named open questions (team vision
  sharing, node-ownership vision bonus, jungle-creep visibility) for whoever picks this up next.

## 2026-07-25 (17)

- feat(arena): particle effects for spells (S170-124). Founder, real-time: "redgarden add
  particle effects to spells." Distinct from S170-122's auto-attack flash, which fires on any HP
  decrease -- a signal several kits' spells don't produce at all (Frog's Q rewinds position/HP
  with no damage; Unicorn's W is a pure toggle). Real wire-protocol addition instead of another
  client-side guess: `ArenaHeroSnapshot.cast_flash_slot` (0/1/2/3 = none/Q/W/R), set the instant a
  cast clears its gate in `arena_cast_q`/`arena_toggle_w`/`arena_cast_r` regardless of whether it
  goes on to hit anything (a real cast animation fires on cast, not just on a landed hit) -- W
  needed care since only some heroes have an internal cooldown gate (instant-cast heroes like
  Ghost/Tyler/Paimon) while others are pure toggles (Unicorn) with no cooldown at all; gated on
  `w_cooldown_ms <= 0`, which is always true for toggle heroes and only true for cooldown heroes
  when actually available. Server clears its own copy right after each broadcast, one-tick
  lifetime, same idiom as `damaged_this_tick`. Client renders Q/W/R as visually distinct tiers
  (small cyan, bigger violet, biggest gold) via the existing ring-mesh machinery; local 1v1 demo
  mode (no server broadcast to hook) drains the same field directly off `arena_state` each frame
  instead. 5 new headless tests (cast_flash_slot set correctly on Q/W-toggle/R, not set when
  blocked by cooldown on Q or a cooldown-gated W). Verified: full suite (277 checks), VS0/VS1
  stable. Wire-format change (ArenaHeroSnapshot grew by one byte) -- rebuilt and restarted all
  three live systemd units together, confirmed a real 20/20 match forms, drafts, and streams real
  snapshots with no crash.

## 2026-07-25 (16)

- feat(arena): enhanced cursor hover state, enemy vs. ally (S170-69 revisited). Founder,
  real-time: "do the enhanced cursor hover state work" — promotes this from the northstar/design
  note it was logged at to real implementation. Purely client-side: `arena_state.heroes[i].team`
  is already populated in every render mode, no wire changes needed. Hovering near a hero's
  floating health bar now draws a bracket outline (same relationship color as the bar fill —
  self/ally/enemy) plus a tooltip with relationship, hero name, and real HP numbers near the
  cursor. Found in the same pass the health bars already run (world_to_screen per hero), no extra
  per-frame cost. Client-only rendering change — no headless test possible (same limitation as the
  earlier attack-flash/melee-animation work), verified via clean build only.

## 2026-07-25 (15)

- docs(readme): real "How to Play" section for the arena MOBA (S170-97). Founder, real-time: "put
  all the hero doc comments that say how to play them (what the kit keybinds do) move that up to
  the top of the readme." The keybind contract was scattered as code comments in
  `apps/arena/src/main.c` and left implicit across every per-hero entry in `docs/HEROES_VS0.md` —
  no single place explained "Q/W/E are your three ability slots, click to move, right-drag/wheel
  for camera." New section right under the title: a real synthesis (checked directly against the
  actual SDL event handling, not guessed), not comments moved verbatim.

## 2026-07-25 (14)

- feat(arena): finish wiring Paimon into the live roster (S170-55). The hero (enum entry, kit
  dispatch, docs writeup) was added in an earlier, uncommitted pass but was never actually
  reachable: no `arena_hero_name()`/`arena_ability_name()` entries (rendered as "unknown"),
  `apps/arena_server`'s pick-validation bound stopped at `ARENA_HERO_TYLER`, the draft-modulo in
  `apps/arena_bot`/`apps/arena` (both `% 18`) meant Paimon could never be drafted at all, and
  `tick_hero_kit`/`bot_cast_kit_if_ready` had no Paimon case (the latter a real compiler warning:
  "enumeration value 'ARENA_HERO_PAIMON' not handled in switch"). Fixed all five gaps: name +
  ability-name table entries, pick-validation bound raised to `ARENA_HERO_PAIMON`, draft-modulo
  bumped to `% 19` in both bot and human clients, passive aura-silence + R-zone damage/heal tick
  added to `tick_hero_kit` (new `ARENA_PAIMON_PASSIVE_INTERVAL_MS` constant), bot-cast heuristic
  added. Also gave Paimon a distinct 3D silhouette (robed body + raised scepter accent) instead of
  falling through to the generic default cube. 5 new headless tests (Q root+damage in/out of
  range, W damage+silence, passive periodic silence, R zone damage-to-enemy/heal-to-ally).
  Verified: full suite passes (272 checks), live: rebuilt + restarted all three systemd units,
  confirmed Paimon (`hero_id=18`) actually gets drafted in a real 20/20 match and the match runs
  stably with real snapshots streaming, no crash.

## 2026-07-25 (13)

- fix(matchmaker): close the phantom-requeue race that was silently capping almost every 10v10
  lobby at 19/20 (S170-85, S170-86). Founder, real-time: "tried the loki build matchmaking still
  launches the client but still no players no enemies nothing" → "q w e r t dont seem to work."
  Root-caused, not guessed: `apps/arena_bot`'s `wait_for_match()` resends `PACKET_FIND_MATCH` if
  it hasn't seen a reply in ~5s (a prior, partial mitigation from S170-99-era work, narrowed from
  1s but never closed). If that resend is still in flight the instant the matchmaker actually
  matches and dequeues the client, the late retry arrives with no way to tell it apart from a
  fresh request — `enqueue()` re-added an address that was already off connecting to its real
  match, permanently costing some *future* lobby exactly one slot (that address's owner isn't
  listening for a second `PACKET_MATCH_FOUND`). With 19 bots continuously cycling through matches,
  this stopped being a rare edge case and became the reason almost every lobby landed at
  `phase=0, 19/20 connected` before the server's 60s no-progress timeout tore it down — which
  also fully explains S170-86: a match that never leaves `ARENA_PHASE_WAITING` never reaches
  `ARENA_PHASE_LIVE`, so casts are correctly rejected the whole time, not broken. Fix: matchmaker
  now remembers every address for a 10s cooldown after it's actually been matched (comfortably
  longer than `connect_to_server`'s own ~5s max retry window) and ignores, rather than re-queues,
  any `FIND_MATCH` from it during that window. Verified live: before the fix, every real attempt
  against the 19-bot pool + 1 extra bot capped at 19/20 and timed out; after rebuilding and
  restarting all three systemd units, the very first attempt reached a genuine 20/20 lobby, all
  20 heroes picked, `match live`, and real snapshot events streaming to the match log. Headless
  suite unaffected (`test_arena.sh` all pass, `test_10_bots.sh` VS0/VS1 stability pass).

## 2026-07-25 (12)

- feat(arena): hero respawn, gated on node control (S170-121). Founder, real-time: "redgarden
  controlling a node enables its spawn for your team." Before this there was no hero respawn
  system at all -- `arena_update_teams` only ever checked team-wipe (0 alive) for the win
  condition, so death was permanent for the rest of the match. Added `respawn_ms_remaining`
  (mirrors `ArenaCreep`'s existing respawn idiom): armed to `ARENA_HERO_RESPAWN_MS` (8s) on death
  in `apply_damage`, ticked down each frame in the new `arena_tick_respawns`, but the actual
  respawn is withheld until the hero's team owns at least one `ArenaNode` -- territory is a real
  gate, not just a speed bonus, matching the founder's framing literally. Respawns at the owned
  node closest to that team's spawn line, full HP, hero identity preserved. Win condition updated
  to match: a team-wipe no longer instantly ends the match if that team still holds a node (they
  can fight back in); only ends once they're wiped AND own nothing to respawn onto. 4 new headless
  tests covering: stays dead with no node, respawns once a node is owned, match doesn't end
  prematurely on a wipe with a held node, match does end once truly locked out.
- feat(arena): basic auto-attack animations (S170-122). Founder, real-time: "add basic animations
  for auto attacks." Neither the wire snapshot (`ArenaHeroSnapshot`, deliberately minimal) nor a
  uniform-across-render-modes signal exists for "an attack just landed" -- used frame-to-frame HP
  decrease instead (available in every render path: local demo, net_mode, replay), spawning a
  quick orange-white flash at the hit hero's position. Reuses the exact same ring-mesh/shader
  machinery the existing move-click placement ring already uses, just a different color/scale
  curve so the two don't read as the same effect.
- Verified: `build.sh`, `build_arena.sh`, `test_arena.sh` (259/259 pass), `test_10_bots.sh`
  (VS0/VS1 stability pass). Live: rebuilt and restarted all three systemd units (wire protocol
  unchanged, but sim behavior changed so old running binaries needed to cycle). Confirmed via a
  temporary 20th bot that the live pool still forms real matches on the new binary. Noted but not
  chased down (pre-existing, already tracked as S170-115): the persistent bot pool intermittently
  gets stuck at 19/20 connected and times out -- matches the already-diagnosed abandoned-queue-slot
  behavior from force-quit client reconnects, not something introduced by this change.

## 2026-07-25 (11)

- fix(arena): requeue looked exactly like a crash (S170-115, real bug found by reading the
  matchmaker log). `net_find_and_connect()` blocks the whole event loop for up to 60s with no
  frame rendered in between -- the window shows whatever was on screen before the click and
  never updates for the entire wait, indistinguishable from a hang. Confirmed live: 13+ distinct
  source ports from the founder's own IP within a few minutes, consistent with force-quitting an
  apparently-frozen window and relaunching, over and over, each relaunch abandoning the previous
  queue attempt mid-match (which is also why those matches kept stalling at high-but-not-full
  connect counts). New `draw_queuing_screen()` renders and presents one real "QUEUING FOR MATCH /
  PLEASE WAIT" frame immediately before the blocking call starts, wired into the OK-button
  requeue handler. Doesn't make the wait non-blocking (bigger rearchitecture, not this pass) but
  makes the wait visibly a wait instead of a crash. Verified: `build_arena.sh`, `test_arena.sh`,
  and a local mingw cross-compile, all clean.

## 2026-07-25 (10)

- fix(ci): Windows cross-compile broken by S170-96's hero-name labels -- `arena_ai_bridge.c` (home
  of `arena_hero_name()`) was never added to the mingw link command when the HUD started calling
  it, so CI has been red on every commit since (`e53ee5f` confirmed failed via the Actions API).
  No valid Windows build existed for the founder to download. Fixed in the CI workflow and
  verified with a local mingw cross-compile using the same toolchain/flags, 0 errors.
- feat(arena): 18th hero, TYLER -- `docs/HEROES_VS0.md` already specced this as "an exact copy of
  Meepo's classic kit" (real OG clone-death rule) well before any code existed for it (S170-111).
  True multi-clone spawning isn't buildable on this engine without touching the draft/pick/
  connection model every other hero depends on (`ArenaHero` slots are one-per-client) -- honestly
  simplified and documented as such: Q "Earthbind" roots + a DoT (folds in Geostrike's poison,
  no generic per-melee-attack passive hook exists to hang it off separately), W "Poof" is a real
  instant blink-strike, R "Divided We Stand" keeps the actual point of the OG rule (real risk/
  reward) as a self-buff that hits hard and leaves Tyler's own armor negative for the window
  after, rather than literal shared-fate clones. `ARENA_HERO_COUNT` 17→18.
- feat(arena): real ability names on the HUD (S170-96 follow-up). Founder, live: "show ability
  names on screen." The Q/W/E cooldown strip only ever showed generic "Q READY"/"W ON" -- new
  `arena_ability_name(hero_id, slot)` (`packages/simulation/arena_ai_bridge.c`) returns each
  hero's real ability name from `docs/HEROES_VS0.md` (e.g. "EARTHBIND", "THE SACRED MAGIC"),
  stacked vertically on the HUD now since real names run much longer than "Q READY" ever did.
  Verified: `build.sh`, `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh`, and a local mingw
  cross-compile (including `arena_ai_bridge.c`), all clean.

## 2026-07-25 (9)

- feat(arena): hero name labels above the floating health bars (S170-96). Founder: "add hero name
  labels above health bars." With 17+ heroes in the roster, a colored bar alone doesn't say who's
  who at a glance. One more `draw_string()` call per alive hero in the existing per-hero HUD loop
  (S170-89), using `arena_hero_name()` (`packages/simulation/arena_ai_bridge.c` -- the same token
  vocabulary the Game AI bridge already uses, e.g. "morrigan") for the text, reusing whatever GL
  color was already set for that hero's bar (team-colored labels, no extra color call needed).
  `arena_ai_bridge.c` wasn't previously linked into the arena client at all -- added it to
  `scripts/build_arena.sh`. Verified: `build.sh`, `build_arena.sh`, `test_arena.sh` all clean.
  Client-only change (no wire-protocol/gameplay-logic touched), so no live systemd restart needed.
  This box has no display, so verified by code review + clean compile only, same standing
  limitation as every other windowed-client-only change this session -- not run interactively.

## 2026-07-25 (8)

- fix(arena): bots bunching up on top of each other in a live match (S170-90). Founder, real-time:
  "all of the bots just bunch up on eachother." Root cause: `apps/arena_bot`'s move-target logic
  sent the nearest enemy's *exact* (x,z) as the move target -- whenever several bots on one team
  shared the same nearest enemy (common once a team clusters up mid-fight), they'd all converge on
  the literal same point and stack. Fixed by spreading each bot to its own approach angle around
  the target, derived from its stable owner index (`my_owner % 8`, no coordination needed between
  bots) at a radius just outside `ARENA_ATTACK_RANGE` -- a real surround formation instead of a
  single pile. Verified: `build.sh`, `test_arena.sh`, `test_10_bots.sh` all clean; restarted the
  three live systemd units on the new build, then ran a real temporary 20/20 match (added one
  extra bot to the persistent 19-bot pool's open human slot, removed it after) and confirmed real
  position data in the match log -- heroes on the same team ended up at genuinely distinct
  coordinates around a fight, not stacked identically the way the bug produced.

## 2026-07-25 (7)

- fix(arena): the two capture nodes render compressed onto one point in net_mode (S170-87). Real
  protocol gap, exactly as diagnosed but not yet fixed: `ArenaSnapshotMsg` never included node data
  at all -- only `heroes[]`, `winner`, `phase`, `picked[]`. In net_mode the client never calls
  `arena_init()`/`arena_init_teams()` locally (the server owns that), so `arena_state.nodes[0]`/
  `nodes[1]` stayed at the global static default -- both zeroed to `(0,0)` -- making both nodes
  render on top of each other at the world origin. Added `ArenaNodeSnapshot` (x/z/owner/
  capturing_team/capture_progress_ms) + `ArenaNodeSnapshot nodes[ARENA_SNAPSHOT_NODE_COUNT]` to
  `ArenaSnapshotMsg`, populated server-side in `server_broadcast()`, consumed client-side in
  `net_poll_snapshots()`. Also colored the node cubes by owner (blue/red/gold matching the hero
  team-color convention) now that ownership actually reaches the client -- the territory redesign's
  whole point, who controls the ground right now, was invisible before this. Verified: `build.sh`,
  `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh` all clean; restarted all three live systemd
  units (`redgarden-matchmaker-bots`, `redgarden-matchmaker-players`, `redgarden-bot-pool`) on the
  new build since this is a wire-format change -- confirmed the pool re-fills cleanly to 19/20 with
  no size-mismatch/crash on the new binary.

## 2026-07-25 (6)

- fix(arena): missing font glyphs. Founder: "we are missing a lot of font glyphs in redgarden."
  `draw_char()`'s hand-drawn vector font only ever covered digits + `W,I,N,L,O,S,E,U,Y,H,P` +
  space -- everything else (15 missing uppercase letters, all of lowercase, punctuation) fell
  through to a generic missing-glyph placeholder box. Tonight's own hero-name expansion (Gary,
  Bacon+Puck, Abraham, Ada, Flute Debt) made this much more visible, since most of those names use
  letters the font never had. Added the remaining 15 letters (A,B,C,D,F,G,J,K,M,Q,R,T,V,X,Z),
  lowercase-folds-to-uppercase (one glyph set, not two), and common punctuation (`- + ' " . , : ! ( )`)
  in the same simple `GL_LINES` stroke style as the existing letters. Verified: `build_arena.sh`,
  `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-25 (5)

- feat(arena): 16th/17th heroes, Abraham the Mage and Ada Lovelace (S170-103). Founder: "add
  abraham the mage" → "add ada lovelace mech pilot." Ada already had full lore
  (`multiverse_heroes.md` #112); Abraham needed new lore, written this pass (#113, The Unbound
  Historicals — Abraham of Worms, disputed author of the real grimoire behind Crowley's actual
  Abramelin Operation). `ARENA_HERO_COUNT` 15→17. Abraham: caster, Q a ranged bolt boosted by a
  toggle (W boosts Q's damage rather than range/duration, a new toggle shape), R a full
  self-cleanse + heal. Ada: tank/controller, Q roots at range, W is a toggled armor bonus
  (`arena_hero_armor()`), R real damage + a follow-up root. Wired into every real call site.
  Verified: `build.sh`, `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh`, and a local mingw
  cross-compile, all clean.

## 2026-07-25 (4)

- docs: FFXI Rise of the Zilart-era item parity seed list (S170-102). Founder direction, real-time
  sequence: "add parity with all ffxi items at rise of the ziliart launch" → "northstar" →
  "redgarden into a doc like the hero metaverse guide in tyler" → "for true ip." Real,
  representative FFXI item names by category (currency, crafting materials, weapons by
  weapon-skill class, armor by equip slot, real Zilart-mission key items, notable RotZ-era
  end-game weapons) at `docs/FFXI_ITEM_PARITY_SEED.md`, same doc-first convention as
  `TYLER/multiverse_heroes.md`. Explicitly not for direct shipping — seed/training data for
  `gpt2-alpine-c`'s fine-tune pipeline to generate this game's own original item names from.
  Registered in `EMILY/context/golden-docs-index.md`.

## 2026-07-25 (3)

- fix(arena): real root cause of "everything breaks after 1 game" (S170-99). Founder, live:
  "still after 1 game in redgarden everything breaks." Confirmed via the matchmaker log: a
  genuinely full 20/20 lobby (including the founder's own external IP) entered draft and then --
  "No lobby progress in 60s (phase=1, 20/20 connected) -- shutting down." Reproduced a bot-only
  20/20 lobby live to rule out a server-side draft bug: it completed cleanly every time, meaning
  the human's own pick specifically was the one never landing. Root cause: `net_send_pick()` (and
  `apps/arena_bot`'s own `send_pick()`) was a single fire-and-forget UDP send with **no retry** --
  unlike `net_connect()`/`net_find_and_connect()`, which both already retry on a timer. Rock-solid
  over localhost loopback (bots, which is all this path was ever tested against all session), but
  a real external connection can drop that one unacknowledged packet, and `net_picked` latching to
  1 on *send* rather than *confirmation* meant it would never be resent -- the client believed it
  had drafted while the server waited forever for a pick that was never coming. Fixed in both the
  human client and `arena_bot`: resend the pick every ~1s while still stuck in draft, harmless if
  the original arrived (the server just re-records the same hero_id). Verified: `build.sh`,
  `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-25 (2)

- feat(arena): 15th hero, Bacon+Puck merged (S170-94). Founder: "add bacon and puck as the same
  hero." Same merge pattern as Flamel/Druid earlier in the roster -- Bacon (`multiverse_heroes.md`
  #5, "custodian of the one location nobody's allowed to know yet," seed phrase "ask again later")
  and Puck (#67, an unresolved duality between two versions of himself nobody can confirm is real).
  `ARENA_HERO_COUNT` 14→15. Q "Ask Again Later" (self `intangible_ms`, the shared can't-be-hit
  status, S170-32), W a free toggle that extends Q's own intangibility duration rather than
  granting a stat, R "The Trick Was Always the Same" (real damage + a self-heal off a fraction of
  it, always commits). Wired into every real call site. Verified: `build.sh`, `build_arena.sh`,
  `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-25 (1)

- feat(arena): 13th/14th heroes, Gary and Flute Debt (S170-91). Founder: "add GARY to redgarden"
  → "music" → "add flute debt" (read in context, not a separate audio request). Both already had
  full lore entries, no new writing needed: Gary, Bifrost Security (Off-Duty)
  (`multiverse_heroes.md` #35) and Han Xiangzi's Flute-Debt (#42). `ARENA_HERO_COUNT` 12→14.
  Gary: a stationary marksman with no dash/teleport at all -- Q is a range-gated precision shot
  (no movement), W is a free toggle that extends Q's own range rather than granting a stat, R is
  a fixed-duration root ("slow down, this isn't a track meet"). Flute Debt: a real debt/payoff
  mechanic -- Q applies the shared `burning_ms`/`burn_dps` DoT (Pizza's fields, S170-46) as "the
  wrong note," W is a free-toggle self-heal, R always lands but deals real bonus damage only if
  the Q debt is still active on the target ("eventually collects"), base damage otherwise. Wired
  into every real call site: all three cast-dispatch switches, `tick_hero_kit`'s regen tick, bot
  AI heuristics, both auto-draft pools (`% 12` → `% 14`), the server's pick-validation bound,
  `arena_hero_name()`, `docs/HEROES_VS0.md`. Verified: `build.sh`, `build_arena.sh`,
  `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-24 (34)

- fix(arena): "ENEMY" HUD readout was a hardcoded 1v1 assumption, broken in team mode (S170-86
  investigation, real bug found). `heroes[1 - my_owner]` only ever made sense for exactly 2
  heroes -- in a 20-hero team match it either mislabels a teammate as ENEMY (`heroes[1]` is
  always team 0, same team as `heroes[0]`, whenever `my_owner==0`) or reads a negative
  out-of-bounds index for any `my_owner > 1`. Replaced with `arena_nearest_enemy(my_owner)` in
  net_mode, the same team-aware lookup the server already uses -- local 1v1 mode keeps the
  original behavior unchanged.
- feat(arena): per-hero floating health bars (S170-89). New `world_to_screen()` projects a world
  point through the same view-projection matrix the 3D pass draws with, into the 2D HUD's pixel
  space. Every alive hero now gets a small billboarded bar above them, colored by relationship
  (cyan = you, blue = teammate, red = enemy) -- not just the two fixed YOU/nearest-enemy bars,
  which never showed anything for the other 18 heroes in a real team match. Verified:
  build_arena.sh, test_arena.sh, and a local mingw cross-compile, all clean.

## 2026-07-24 (33)

- fix(ci): hard timeout ceilings after a real hung build. Founder: "we have a hung build for the
  rebrand in ci." Confirmed via the GitHub Actions API: commit `62ca556`'s run sat "in_progress"
  on the mingw-w64/SDL2 install step for 18+ minutes with byte-identical YAML to four immediately
  preceding runs that all passed in seconds -- a transient runner/mirror stall, not a code bug, but
  nothing in the workflow would have ever timed it out on its own (job default is 6 hours; no gh
  CLI/token available in this environment to cancel it remotely). Added `timeout-minutes: 30` at
  the job level, `timeout-minutes: 10` on the specific mingw/SDL2 step, `DEBIAN_FRONTEND=noninteractive`
  on that step's apt-get (defends against an interactive alternatives prompt as one plausible
  cause), and a real `wget --timeout=30` (previously only `--retry-connrefused`, which doesn't
  catch a connection that succeeds and then stalls).

## 2026-07-24 (32)

- feat(arena): 12th hero, Loki (S170-79). Founder: "add LOKI to KNIGHTS_OF_THE_VOID hero
  multiverse then into the game one shot as a kit." New lore entry first
  (`TYLER/multiverse_heroes.md` #37, "Loki, Who Isn't Here" — see that repo's own commit), then a
  real `ARENA_HERO_LOKI` kit here, one pass, no stub: Q "Interference, Not a Signal" (instant
  positional swap with the nearest enemy, no travel time, small hit on arrival, no range gate),
  W "Bound Where the Myth Says" (free toggle, flat armor bonus while active, same convention as
  Unicorn's regen toggle), R "Held For As Long As The Myth Demands" (self-cast survive-floor
  window, the same `survive_floor_ms` mechanic Pizza/Dagda's ultimates already use). Wired into
  every real call site: `arena_hero_armor()`, the Q/W/R dispatch switches, the bot AI heuristic,
  the human/bot auto-draft pools (`% 11` → `% 12`), the server's draft-pick validation bound
  (`ARENA_HERO_COURIER` → `ARENA_HERO_LOKI`), `arena_hero_name()`, `docs/HEROES_VS0.md`.
  `ARENA_HERO_COUNT` 11 → 12. Verified: `scripts/build.sh`, `scripts/build_arena.sh`,
  `scripts/test_arena.sh`, `scripts/test_10_bots.sh` all clean, plus a full local mingw
  cross-compile with the updated roster, 0 warnings.

## 2026-07-24 (31)

- feat(arena): toggleable APM overlay, F11 (S170-71). Founder: "add toggalable apm overlay f11"
  → "adding apm near term if its cheap." Ring buffer of action timestamps (clicks + Q/W/E casts)
  in `apps/arena/src/main.c`, `apm_compute()` counts entries within a trailing 60s window so the
  on-screen number is a real rate rather than a since-launch average. Off by default, F11 toggles
  it in any mode (local, net, or observing). Ties into `REDGARDEN/CLAUDE.md`'s standing
  high-APM-affordance UI constraint. Verified: `scripts/build_arena.sh` + `scripts/test_arena.sh`
  clean, local mingw cross-compile clean.

## 2026-07-24 (30)

- fix(ops): redgarden-bot-pool.service never set REDGARDEN_TICKET_SECRET (S170-72) -- the real
  root cause of "no entities visible," not a rendering or death bug. Live investigation of the
  founder's "i cant see myself or team or enemies" / "maybe the game isnt actually working right
  theres no entities" turned up ~55 accumulated zombie arena_server processes, all stuck at
  "0/20 connected" until their 60s no-progress timeout. The matchmaker log showed lobbies filling
  and spawning a real dedicated server every time, but no client ever completed PACKET_CONNECT to
  it. Root cause: only the two matchmaker systemd units had REDGARDEN_TICKET_SECRET set --
  redgarden-bot-pool.service (the unit that actually runs the 19 apps/arena_bot processes) never
  did, since it was written. Bots could queue fine (no ticket needed for that) but silently failed
  to mint a valid connect ticket for the actual game-server handshake, so matches formed and then
  sat empty forever. Fixed by adding Environment=REDGARDEN_TICKET_SECRET=... to the bot-pool unit.
  Verified live: killed the zombie servers, restarted all three services, watched real
  CLIENT N CONNECTED lines climb to 18-20/20 across several matches -- the system is now capped
  only by needing a 20th (human) player, not by a broken connect path.

## 2026-07-24 (29)

- fix(arena): the actual instant "YOU WIN" bug (S170-66) -- three more `#ifndef _WIN32` guards
  reintroduced by the newer 10v10 networked-PvP code, all in `apps/arena/src/main.c`: the
  `net_poll_snapshots()` call site, the click-to-move `net_send_move()` call site, and the Q/W/E
  `net_send_cast()` call site. On Windows (the founder's actual platform) all three silently
  compiled out -- no error -- so the client fell through to the local single-player practice
  simulation instead of ever applying real server snapshots, resolving near-instantly and
  producing a "YOU WIN" completely disconnected from the real networked match. `grep -n "#ifndef
  _WIN32"` now returns zero hits in this file; every remaining guard is a correctly-scoped
  `#ifdef _WIN32` around an actual platform difference. Second real blocker found + fixed in the
  same pass: `redgarden-bot-pool.service` (S170-65) launched exactly 20 bots into a
  `--lobby-size 20` matchmaker, permanently filling the lobby with bots alone -- dropped to 19 so
  a human always has an open slot. Also added, absorbing part of S170-68's scope per the
  founder's own real-time narrowing ("terminal launching the client is fine for now" / "auto
  draft is fine for now"): `net_send_pick()` + auto-draft (sends a `PACKET_ARENA_PICK` the moment
  `net_phase` reports `ARENA_PHASE_DRAFT`, console-logged) so a match never hangs waiting on a
  pick that never comes; and a click-to-continue "OK" requeue button on the win/lose screen in
  net_mode, reusing the same `net_find_and_connect`/`net_connect` path used at startup. Verified:
  `scripts/build_arena.sh` clean, `scripts/test_arena.sh` all green, and a full local mingw
  cross-compile (same toolchain/flags as CI) produced a clean 0-warning `RedGarden.exe`.

## 2026-07-24 (28)

- ops: real systemd units for the arena matchmaker pools + persistent bot pool (S170-65). Founder,
  after the S170-63 fix: "fix is not pushed" -- correctly pointing at the actual gap, since the
  code fix genuinely was pushed and merged, but the live matchmaker processes had *never* run
  under systemd at all, ever, on this box -- only ever manually nohup'd, which is exactly why
  S170-63's outage happened in the first place (died silently, stayed dead until someone
  noticed). New `ops/systemd/redgarden-matchmaker-bots.service`,
  `redgarden-matchmaker-players.service`, `redgarden-bot-pool.service` (+ new
  `scripts/run_bot_pool.sh`, a foreground wrapper so systemd can actually supervise the bot set),
  matching the existing `fatbaby-newssite.service`/`gfd-mud.service` pattern. Deployed and
  verified live: killed the manual processes, started the units, confirmed `Restart=on-failure`
  actually works (one stray leftover `arena_server` was squatting on the matchmaker's own port;
  once killed, systemd auto-relaunched the matchmaker within its restart window with no manual
  intervention).

## 2026-07-24 (27)

- fix(ops+ci): matchmakers had died (bots orphaned, queue packets going nowhere) and `PLAY.bat`
  never set `REDGARDEN_TICKET_SECRET`, so even after restarting the matchmakers the client failed
  silently at the ticket-mint step -- no human login flow exists yet, so `--queue` falls back to
  self-minting via that env var, which has to be set client-side too (S170-63, found live while
  a founder was actually trying to connect). Restarted both matchmaker pools with the shared test
  secret the live bot pool already uses. Fixed `PLAY.bat` to set the same secret before launching
  and added `pause` so a failure is actually readable instead of the window closing before the
  error prints. Also: briefly misdiagnosed this as an IDUNA-vs-server ticket-signing-secret
  mismatch (real, but irrelevant to the self-mint path actually in use here) and accidentally
  spawned one broken test bot mid-investigation that spammed the pool with failed
  connect/requeue cycles -- corrected, orphans cleaned up.

## 2026-07-24 (26)

- fix(ci): `PLAY.bat`'s hardcoded `127.0.0.1` was wrong for the actual distributed client
  (S170-59). Found live: a founder actually downloaded and ran the CI-built Windows client, and
  it hung "queuing for a match" at `127.0.0.1:7778` -- loopback, which only makes sense if the
  matchmaker is running on that same Windows machine. Fixed `PLAY.bat` to point at this box's
  real address (`198.58.107.85`) and print what it's connecting to before launching, instead of
  a silent `start` that gave no feedback about which server it was even trying to reach.

## 2026-07-24 (25)

- CI green end to end (S170-54 closed): confirmed via the GitHub Actions API (no `gh` CLI on this
  box, public API works without a token for a public repo) that commit `276614c`'s run passed
  every step -- headless tests, the bot-pool soak test, Linux server-side build, Linux arena
  client build, the mingw-w64 install, the Windows cross-compile, artifact bundling, and upload.
  `red-garden-build` now contains a real `RedGarden_Client_*.zip` (Windows .exe + SDL2.dll +
  PLAY.bat) and `RedGarden_Server_*.zip` (Linux server-side binaries), matching what a founder
  actually asked for: "the github artifact for REDGARDEN is unsuitable... no executable... SDL
  dll not bundled... check shankpit for the protopattern."

## 2026-07-24 (24)

- fix(arena): the actual root cause of the Windows build failure, found by locally reproducing
  the cross-compile (downloaded `gcc-mingw-w64-x86-64-win32` + deps via `apt-get download`,
  extracted with `dpkg-deb -x`, no sudo/root needed) instead of guessing blind against CI (S170-54
  cont'd). The whole networking section of `apps/arena/src/main.c` (ticket minting, WOTAN
  registration, `net_connect`, `net_find_and_connect`, snapshot polling — ~300 lines) was still
  wrapped in one big `#ifndef _WIN32`, so none of it was ever compiled on Windows at all despite
  the earlier per-call portability fixes — `main()`'s calls to these functions produced "implicit
  declaration" + linker "undefined reference" errors. Removed that outer guard now that the
  platform differences inside are each handled individually (winsock includes, ioctlsocket/fcntl,
  closesocket/close, mkdir, and one more found this pass: `getpid()` is POSIX-only, added a
  `GetCurrentProcessId()` branch). Also silenced two real `sendto()` type-mismatch warnings
  (Winsock wants `const char *`, POSIX accepts anything pointer-shaped). **Verified: a real
  `RedGarden.exe` (PE32+, Windows) now builds clean locally with zero errors and zero warnings**,
  Linux side (`build_arena.sh`, full test suite) still green. Same fix pushed for CI to confirm
  independently.

## 2026-07-24 (23)

- fix(arena): real Windows portability for `apps/arena/src/main.c`'s networking, found by
  actually watching the S170-54 CI run fail rather than trusting the workflow blind. Root cause:
  the file's `#ifndef _WIN32` guard around POSIX socket headers had no matching `#ifdef _WIN32`
  branch including `winsock2.h`/`ws2tcpip.h` at all -- so under MinGW, `sockaddr_in`/`AF_INET`/
  `SOCK_DGRAM` etc. were simply undeclared. Fixed to match `apps/server/src/main.c`'s already-
  correct pattern exactly: `winsock2.h`/`ws2tcpip.h`/`windows.h` + `#pragma comment(lib,
  "ws2_32.lib")` on Windows, the POSIX headers on everything else. Also fixed `fcntl(F_SETFL,
  O_NONBLOCK)` (POSIX-only) → `ioctlsocket(FIONBIO)` on Windows at both non-blocking-socket call
  sites, `close()` → `closesocket()`, and added the `WSAStartup` call Windows sockets need before
  first use. Along the way, found `--connect`/`--queue` were explicitly stubbed out on Windows
  builds entirely ("not supported... yet") -- now that the underlying networking actually
  compiles correctly cross-platform, enabled it for real rather than leaving the stub in place
  once its excuse was fixed. Verified: `scripts/build_arena.sh` (Linux) and the full
  `scripts/test_arena.sh`/`test_10_bots.sh` suites still green; the actual Windows cross-compile
  is CI-verified on push (still no `mingw-w64` locally, no sudo here).

## 2026-07-24 (22)

- fix(ci): `.github/workflows/ci.yml` rebuilt to produce a distributable artifact (S170-54).
  Founder, live: "the github artifact for REDGARDEN is unsuitable... no executable... SDL dll
  not bundled... check shankpit for the protopattern." Root cause: CI only ran `build.sh` (RTS
  server-side binaries) and never built `apps/arena` -- the actual product since today's MOBA
  pivot -- and uploaded bare Linux ELFs with no runtime bundled either way, nothing a founder
  could download and run. Mirrored `SHANKPIT/.github/workflows/release.yml`'s proven pattern:
  cross-compile the arena client to Windows via `mingw-w64` + the official `SDL2-devel-2.30.10-
  mingw` package, zip `RedGarden.exe` + `SDL2.dll` + a `PLAY.bat` as a separate Client artifact
  from the Linux server-side binaries (Server artifact). No `-lglu32` needed, unlike SHANKPIT's
  client -- `apps/arena` is shader-based and never depended on GLU. Also added `test_arena.sh` +
  `test_10_bots.sh` as real CI gates before packaging -- neither was run in CI before this,
  despite being the actual verification for everything built today. Linux side (tests, `build.sh`,
  `build_arena.sh`) re-verified locally; the Windows cross-compile itself is CI-only-verified for
  now (no `mingw-w64` installed locally, no sudo here -- queued as `sudo-queue/09-mingw-w64.sh`
  for a local dry-run if wanted).

## 2026-07-24 (21)

- feat(arena): territory capture redesigned to a real Arathi Basin-style channel + territorial jungle creeps + memorable bot names (S170-50/51). Replaced the S170-46 ambient-pressure territory model entirely with exclusive-presence channel capture: a node flips neutral the instant a channel starts (not on completion), interrupts (mixed presence, Pizza's corruption, damage taken, or the channeling team leaving) lose all progress with no free revert, and stealth (Frog's R) lets a lone capper channel undetected through a crowd of visible enemies -- but starting the channel breaks that stealth, and any damage to the channeling team interrupts it, both matching real Arathi Basin rules exactly. Added territorial jungle creeps: one per node, re-rolled from the node's current owner on every respawn, two flavors with genuinely different rewards (a rare contested-node creep grants a big capture-progress swing; a common owned-node creep heals its own team or helps the enemy flip the node, depending who kills it) -- a real counter-play tool against turtling comps. Activated real WOTAN stats tracking for the persistent bot pool (was silently running on self-minted tickets all session -- an env var oversight, not a code gap) and gave bots a curated pool of memorable display names via a new `--index` flag. 28 new headless tests (251 total). Verified live: a full ~2.5-minute 20-hero match ran to completion without crashing on the redesigned system; confirmed real, named player identities registering and the public leaderboard accumulating real stats.

## 2026-07-24 (20)

- docs(heroes): The Donkey — Paper Glide, a second auto-trigger ability (S170-49). Founder direction: "launching itself into the air while folding into a paper airplane... movement mobility and escape... fly over trees etc." Specified in `docs/HEROES_VS0.md` as Q, consistent with the existing Indirect-Control identity (auto-triggered alongside the Immortal's Fold passive, not player-cast): launches airborne, refolds into a paper-airplane shape mid-launch, glides clear of danger, ignoring ground terrain/obstacles and immune to ground-based CC while airborne. Docs only -- The Donkey (and the rest of the Indirect-Control archetype) stays blocked on a non-piloted-unit system that doesn't exist in `arena_game.c` yet, flagged explicitly in the entry rather than shoehorned into the owner-piloted sim.

## 2026-07-24 (19)

- feat(arena): The Courier, Ratatoskr (S170-48) -- eleventh hero, roster 10 → 11. TYLER `multiverse_heroes.md` #32 is already nicknamed "The Courier"; his messenger-between-two-fixed-points framing (the eagle at Yggdrasil's crown, Nidhogg at its root) maps directly onto the two existing `ArenaNode` positions -- W (Between Eagle and Serpent) is a pure fixed-geography teleport to whichever node is farther away, distinct from every other hero's ally/foe-relative teleport. Q is a Unicorn-shaped dash-strike whose landed cast also cleanses The Courier's own active debuffs (the passive, "editing the message" back to him). R is a flat life-drain execute on the nearest enemy. 7 new headless tests (223 total). Pick-validation bound and draft modulo widened 10→11. Verified live after cleaning up a stray leftover-process port conflict: all 11 hero_ids drafted across a real 22-bot pool, left running on the current build.

## 2026-07-24 (18)

- feat(arena): territory/node system + five new heroes -- Tree, Pizza, Flamel (absorbing the former Druid), Morrigan, and Dagda (S170-46/47). Founder picked territory/resource economy over multi-unit-per-player or non-piloted units as the next system to build, since it unblocks the most queued heroes at once. Extended the two previously-decorative `ArenaNode` markers with signed `pressure` (-100..100), threshold-derived `owner`, and `marked_by_team`/`mark_ms_remaining`; new `arena_tick_nodes()` sums weighted team presence per node each tick (Tree counts double, Root Network) and drifts/decays pressure toward a derived owner, called from both `arena_update()` and `arena_update_teams()` with no special-casing. Added a centralized `apply_damage()` helper (every damage call site now routes through it) so Pizza's R -- a real damage floor, not simplified away -- works consistently everywhere. Mid-build founder redirect ("druid and flamel should be the same hero"): merged Druid into Flamel in `docs/HEROES_VS0.md` first (TYLER lore check confirmed Druid had zero named-character backing, Flamel is a real one), keeping Flamel's identity and folding Druid's kit in as flavor. Then two more founder-driven additions on the same pass: Morrigan ("meta jungler," TYLER #68) built as an affinity for contested/unclaimed node ground since no standalone jungle-camp system exists; Dagda ("the two-natured hammer," TYLER #69) built with a literally dual-natured Q (kills a hittable enemy in range, else heals a hurt ally in range instead -- the same tool, either direction, depending on what's there). `apps/arena_server`'s pick-validation bound and `apps/arena_bot`'s draft modulo widened 5→8→10 heroes along the way. 62 new headless tests (216 total, up from 154), including a caught-and-fixed test bug (an exact-value assertion on Morrigan's execute-tick damage invalidated by a same-tick melee auto-attack and HP-floor clamping -- fixed by comparing damage deltas instead). Verified live: relaunched the persistent bot pool on the freshest build, all 10 hero_ids (0-9) drafted successfully across a real 20-bot match, pool left running (not torn down) so bots are actively playing the current roster.

## 2026-07-24 (17)

- docs(arena): S170-14 (3/3) — ranked matchmaking design pass, `docs/RANKED_MATCHMAKING.md`. Plain ELO (K=32 flat, starting 1000) recommended over Glicko/TrueSkill -- the uncertainty modeling those solve for doesn't apply to a symmetric 1v1-only pool yet. New `redgarden_ranked_stats` table, kept separate from casual `player_game_stats` (ranked rating and casual win/loss are different questions). Widening-rating-search-window queue design, explicitly scoped as its own future build pass since it doesn't fit the existing spawn-on-fill `apps/matchmaker` binary. Design only, no code landed -- this was a design gap, not a code gap, per the backlog item's own framing.

## 2026-07-24 (16)

- feat(arena): allies + fifth hero, Doc Wheel (S170-45). Founder decision: build allies/multi-hero-per-team in arena rather than territory or declaring the 4-hero roster complete. Team-mode infra already existed from the 10v10 pivot -- the actual gap was just an ally-targeting primitive. Added `arena_nearest_ally(int owner)` (mirrors `arena_nearest_enemy` exactly) and threaded an `ally` param through `tick_hero_kit`. Unblocked: Ghost's Recital ally-heal side (previously skipped), Frog's Borrowed Time (W, places a generic `next_cast_refund` buff on the nearest ally -- refunds whoever holds it their next Q/W/R cooldown to zero), and Doc Wheel (Buer) as a full new hero -- HP%-scaled heal+cleanse (Q), teleport-to-ally (W), teamwide cleanse+heal (R, simplified from a literal shield, flagged not faked). `apps/arena_bot`'s draft picker and `apps/arena_server`'s pick-validation bound updated so Doc Wheel is actually draftable over the wire. Found and fixed a real bug writing the Borrowed Time test: a Unicorn with no move target and no foe never reaches its cooldown-setting code path at all, so the refund check silently never ran -- the original assertion was passing by coincidence, not because the mechanism worked. 16 new headless tests, all green alongside the full existing suite. Verified live: two separate real bot matches (10-bot, 20-bot lobbies) both drafted Doc Wheel without incident.

## 2026-07-24 (15)

- feat(arena): player-only matchmaking pool (S170-14, 2/3) -- `scripts/launch_arena_pools.sh` stands up a second, entirely separate matchmaker instance on its own port (7779, `--lobby-size 2`), with zero bots ever configured to queue into it. Pool separation is operational (two matchmaker processes, two ports), not new code inside the matchmaker itself. Lobby size is 1v1, not 10v10, since a 10v10 player-only queue would never fill with near-zero real concurrent players today. Verified live: ran both pools simultaneously (bot pool with 2 bots + player-only pool), two real `--queue` human clients matched into a genuine 1v1 on the player-only pool ("Lobby full (2 players) -- internal bot AI disabled, entering draft"), and confirmed by grepping every bot's logs that none ever touched the player-only pool's ports. Ranked pool (3/3) stays explicitly undesigned -- no rank model, MMR, or queue rules exist yet, a design gap not a code gap.

## 2026-07-24 (14)

- feat(arena): `red_garden_arena --queue <matchmaker_host>` (`--matchmaker-port`, default 7778) -- a human player can now join whatever match the persistent bot pool is currently matchmaking into, instead of only supporting `--connect host:port` to an already-known server. Reuses `apps/arena_bot`'s exact queue pattern (`PACKET_FIND_MATCH`/`PACKET_MATCH_FOUND`, ~5s retry) and `net_connect`'s existing ticket-mint/handshake for the actual game connection -- pure client-side addition, no server changes needed. Verified live: started a real matchmaker + one persistent bot, ran the human client with `--queue 127.0.0.1`, confirmed it queued, matched with the bot, connected, and was assigned hero slot 1 on the same server the bot connected to (slot 0). First attempt failed on a test-setup mistake (matchmaker started without `REDGARDEN_TICKET_SECRET` exported, so the spawned server failed closed on all connects, correctly) -- not a code bug, fixed by restarting the stack with the secret actually set. Still bounded by the same known gap as before: no Xvfb on this box, so the client hits SDL_Init with no display right after connecting -- the join is proven, playing a full match still needs a real display.

## 2026-07-24 (13)

- Verified the actual `--lobby-size 20` (10v10) path live, end-to-end, not just via the headless-tested code shared with 1v1: 20 real `apps/arena_bot` connections, 20 real drafts, correct team assignment (0-9/10-19), combat across 20 heroes, and a real team-wipe win condition (`match_end` winner matched exactly which team had zero living heroes left). All 20 bots then persisted and requeued into a second full 20-player match automatically -- identity stayed stable (1 registration each) across both. Server process count stayed healthy throughout (not the earlier zombie pileup). This closes the "10v10 unverified" gap flagged earlier the same day. Remaining honest gap: the SDL2 client's visual rendering of a live match is still unconfirmed (no Xvfb on this box).

## 2026-07-24 (12)

- MOBA 10v10 scaling + persistent bot pool (NORTHSTAR §13 cont'd): team-mode sim (`arena_game.c`/`.h` -- heroes[2] -> heroes[20], `team`/`active` fields, `arena_nearest_enemy()` generalizing foe lookup, `arena_init_teams`/`arena_update_teams` additive to the existing 1v1 path, 5 new tests, zero regressions in the full existing suite). Draft phase (`PACKET_ARENA_PICK`, `ARENA_PHASE_WAITING/DRAFT/LIVE`) -- heroes were hardcoded, now every real slot picks before the clock starts. `apps/arena_server` generalized to `--lobby-size N`. New `apps/arena_bot` -- a real networked bot (not the sim's internal practice AI), real WOTAN identity, plays via matchmaker, persistent. `apps/matchmaker` generalized (`--lobby-size`/`--listen-port`/`--first-game-port`), one binary serves both the card-RTS and arena roles now.
- Three real bugs found running an actual persistent bot-pool soak test (not by review): (1) bots were re-registering a brand-new WOTAN identity every match instead of keeping one stable identity -- fixed, register once per process; (2) match servers never terminated after match end, flooding a persistent bot's socket with stale packets from every prior match and silently swallowing its next connection's WELCOME packet -- fixed, servers now exit shortly after the match ends; (3) a UDP retry race in the matchmaker protocol could spawn phantom matches nobody ever connects to -- mitigated (slower retry interval) plus a defensive 60s no-progress server timeout so any phantom that still slips through self-cleans instead of leaking forever. Verified via an extensive soak test: 2 persistent bots, stable identity across 20+ matches each, real accumulating win/loss records, zero connect failures.
- Explicitly unverified yet: the actual `--lobby-size 20` path live end-to-end (same tested code as 1v1, not yet run with 20 real connections), and the SDL2 client's visual rendering of a live networked match (no Xvfb on this box).

## 2026-07-24 (11)

- Product pivot (NORTHSTAR §13): apps/arena (the MOBA) is the product now, not the card-RTS. Real 1v1 networked PvP: new `apps/arena_server` (ports connect-ticket/WOTAN pieces from apps/server), `--connect <host>` mode added to apps/arena's client, new `PACKET_ARENA_MOVE/CAST/SNAPSHOT` wire packets. Verified live, catching and fixing two real bugs: `arena_bot_enabled` wasn't gating `bot_cast_kit_if_ready` (a real second player would still get yanked/attacked by the bot's kit AI), and the sim clock started before both real players connected (a match could resolve before player 2 ever joined). Fixed both; server now only ticks once both slots are filled. Two real clients with distinct WOTAN identities verified sitting still at full HP, waiting for real input -- genuine PvP, not bots fighting bots. `scripts/test_arena.sh` (+1 regression test) and `scripts/test_10_bots.sh` both re-verified clean.

## 2026-07-24 (10)

- WOTAN player identity, S170-41 cont'd: `apps/server` now reports match results at match_end via `report_match_result()` -- agent-login, then `POST /api/v1/redgarden/game-result` per connected client's real player_id. Verified live end-to-end with a real 2-bot match played to natural completion: match log's `match_end` winner matched the public leaderboard afterward exactly (winner's wins +1, loser's losses +1). `scripts/test_10_bots.sh` + `scripts/test_arena.sh` both re-verified clean.

## 2026-07-24 (9)

- WOTAN player identity, S170-41: `apps/client/bot_main.c` now tries a real IDUNA register+ticket-mint round trip (falls back to the old self-mint on any failure) instead of always self-minting a fake ticket. Verified live: two bots registered distinct real `player_id`s, connected via the real matchmaker, match log shows real identities on every event. `scripts/test_10_bots.sh` re-verified clean (backward compatible). Companion IDUNA-side change (new `REDGARDEN-BOTS` agent, `player_game_stats` table, `/api/v1/redgarden/{ticket,game-result,leaderboard}` endpoints) landed in the IDUNA repo, verified live end-to-end there too.

## 2026-07-24 (8)

- NORTHSTAR §12 Phase E (S170-36) started: Milestone-6 equivalent (state serializer + action
  decoder) from `gpt2-alpine-c/docs/GAME_AI_NORTHSTAR.md`, extended to arena's hero/ability state
  instead of a REDGARDEN-specific format. New `packages/simulation/arena_ai_bridge.h`/`.c`:
  `arena_serialize_state()` writes a stable self/foe natural-language state string;
  `arena_decode_action()` parses a `move:x,z cast_q/w/r:0|1` action string, defaulting missing
  fields to a safe no-op and failing closed on garbage. 7 new headless tests, all green alongside
  the full existing suite. Not wired into the live bot or the GPT-2 inference server (`:8088`)
  yet -- contract only, same sequencing discipline as Phases B→C.

## 2026-07-24 (7)

- NORTHSTAR §12 Phase D (S170-33) — fourth hero, **The Frog**, the last clean-fit hero from
  S170-32's roster audit: Q (Loop Back) rewinds the Frog's own position/HP to ~3s ago via a new
  per-hero loopback ring buffer (16 slots, 250ms sample rate, sampled generically for every hero);
  degrades to the oldest available sample rather than refusing to cast if less than 3s of history
  exists yet. R (The Secret) reuses Ghost's `intangible_ms` mechanic at a longer duration --
  "reappear at a chosen location" isn't built, flagged as a simplification. W (ally-targeted) and
  the passive (UI-only) are skipped, same reasoning as other skips this phase. Bot heuristic is
  defensive (rewind when hurt, vanish when critical) since Frog deals no damage. 4 new tests, all
  green alongside the full existing suite. Arena has now absorbed every roster-fit hero from the
  audit -- the 8 structurally-blocked heroes need arena to grow new systems first, a real decision
  point flagged in the northstar rather than continuing to just pick the next one.

## 2026-07-24 (6)

- NORTHSTAR §12 Phase D (S170-32) — third hero, **The Ghost**: Q (skillshot simplified to
  instant-hit-if-in-range, damage + Silence), W (instant intangibility on its own cooldown, not a
  toggle), R (fixed-position damage zone, enemy-only side of Recital). First kit needing real
  status-effect state: new generic `silenced_ms`/`intangible_ms` `ArenaHero` fields (any hero can
  carry them) and a `hero_is_hittable()` check used everywhere a hit used to just check
  `foe->alive`. Zone DPS uses a fixed 1000ms tick interval rather than fractional-per-tick math --
  flagged, but did not fix, a related pre-existing rounding bug in Unicorn's W regen (works in
  tests that jump a full second, silently truncates to 0 at real 16ms frame rates). Also: a
  roster-fit audit of the remaining 10 heroes found most (Tree/Pizza/Druid/Doc Wheel/Retrieval
  Cart/Donkey/TYLER/Flamel) structurally blocked by systems arena doesn't have (grid pressure,
  allies, multi-unit, cooking) -- only Frog remains a clean fit. 7 new headless tests, all green
  alongside the full existing suite.

## 2026-07-24 (5)

- NORTHSTAR §12 Phase D (full roster in arena, S170-31) started: generalized `arena_cast_q`/
  `arena_toggle_w`/`arena_cast_r` to dispatch on a new `ArenaHero.hero_id` field instead of
  S170-18's hardcoded `owner == 0` check, then wired **The Duck** as the second kit (Q/R only --
  its W needs objectives that don't exist here, its E's trigger coincides with match-end, both
  flagged and skipped). `arena_init()` now defaults player=Unicorn, bot=Duck with simple
  heuristic bot-casting, giving the bot side a real kit for the first time. 6 new headless tests
  (including cross-slot dispatch verification), all green alongside the full existing suite.
  10 heroes remain, each a separate follow-on pass.

## 2026-07-24 (4)

- NORTHSTAR §12 Phase C (observer mode, S170-30) started, arena half: new
  `packages/simulation/arena_replay.h`/`.c` parses an `apps/arena` match log and drives
  `ArenaState` directly from it (linear interpolation between the 500ms snapshots). New
  `red_garden_arena --observe <path>` flag plays a logged match back through the same render
  loop as live play (camera control active, live-match input disabled, `R` restarts playback).
  6 new headless tests (`tests/test_arena_replay.c`), all green; `build_arena.sh` and
  `test_arena.sh` updated to include the new files. RTS-side playback and true live-tailing
  remain open, separate next steps.

## 2026-07-24 (3)

- NORTHSTAR §12 Phase B (replay logging, S170-29) closed for the MOBA half: `apps/arena` now
  opens `var/matches/arena-<timestamp>.jsonl` (fresh per match, including on restart) and appends
  `match_start`/`snapshot` (every 500ms, both heroes' x/z/hp)/`ability_cast`/`match_end` events.
  No connect-ticket auth exists in this client, so events use `"local_player"`/`"local_bot"`
  placeholders rather than a guessed WOTAN player_id -- flagged as a real gap, not silently faked.
  Verified by clean compile (`scripts/build_arena.sh`) and code review only -- this box has no
  display, so unlike the RTS half this couldn't be run end-to-end. `scripts/test_arena.sh`
  (headless sim tests, untouched by this change) still green.

## 2026-07-24 (2)

- NORTHSTAR §12 Phase B (replay logging, S170-28) started for the RTS half: `apps/server` now
  opens `var/matches/<port>-<timestamp>.jsonl` per match and appends `match_start`/`connect`
  (with Phase A's `player_id`)/`card_play`/`match_end` events — exactly §10's originally-spec'd
  minimum hook, now with player identity attached. Verified real log output from
  `scripts/test_10_bots.sh`. `var/` added to `.gitignore`. The MOBA half (`apps/arena`'s per-tick
  hero-state logging) is not started -- distinct next step, not covered by this pass.

## 2026-07-24 (1)

- NORTHSTAR §12 Phase A (WOTAN player identity, S170-26) started: `apps/server` now captures the
  real IDUNA-minted `player_id` from every connect ticket instead of discarding it after
  verification (`client_player_id`/`client_has_player_id`, keyed per client slot) — the prerequisite
  Phase B (replay logging) needs to attribute matches to real players. Ported
  `packages/common/http_client.h` (verbatim from shankpit-460) and IDUNA agent config loading.
  Reporting REDGARDEN win/loss results into IDUNA is deliberately not wired yet — its
  `/api/v1/players/{id}/session` endpoint is FPS-shaped (kills/deaths), REDGARDEN's `match_winner`
  isn't; flagged as an open schema question rather than forced in wrong. All existing tests
  (`test_10_bots.sh`, `test_arena.sh`) still pass.

## 2026-07-23

- Fixed `GL/glu.h` missing (installed `libglu1-mesa-dev`) — `apps/lobby` and `apps/arena` now build clean.
- Fixed `usleep` implicit-declaration warning at the root cause: `-std=c99` was hiding the POSIX declaration; added `-D_DEFAULT_SOURCE` to `scripts/build.sh`.
- Added connect-ticket accounts (HMAC-SHA256, same scheme as shankpit-460): `packages/common/hmac_sha256.h` ported verbatim, `apps/server` verifies tickets on `PACKET_CONNECT` (fails closed without `REDGARDEN_TICKET_SECRET`), test bots self-mint tickets like shankpit-460's `emily-bot`.
- Added simple matchmaking: new `apps/matchmaker` pairs `PACKET_FIND_MATCH` requests and spawns a dedicated `red_garden_server --port <N>` per match; new `PACKET_FIND_MATCH`/`PACKET_MATCH_FOUND`/`MatchFoundMsg` wire types.
- Validated VS0 (bot-vs-bot match) and VS1 (10 independent headless bots, 5 concurrent matches, matchmaking + accounts, 10s sustained load, zero crashes) via new `scripts/test_10_bots.sh`.

## 2026-07-25 (2)
- feat(arena): real per-hero 3D geometry (S170-118). Founder, real-time: "use shankpit skins as
  a basic jump in graphics for redgarden models" -> "use shankpit og engine models to enhance
  redgarden hero legibility." Every hero previously rendered as one identically-shaped colored
  cube -- S170-89/96 already fixed "who is this" (floating health bars + name labels); this
  fixes "what does this hero actually look like." New `draw_hero_model()` in
  `apps/arena/src/main.c`: a per-`hero_id` switch composing 1-3 `draw_mesh()` boxes with real
  proportions/silhouettes, reusing the design language of the 7 SHANKPIT skins (Duck/Unicorn/
  Ghost/Frog/Tree/Pizza/Tyler) where a hero overlaps one, new equally-simple 2-3-box designs for
  the other 11. Relationship coloring (self=cyan/team=blue/enemy=red, S170-89) is preserved
  unchanged -- shape now encodes hero identity, color still encodes team/self, so neither
  legibility need overrides the other. Can't literally port SHANKPIT's immediate-mode
  `draw_player_skin_*()` code (this renderer is shader-based, no `mat4_rotate`) -- boxes are
  axis-aligned translate+scale only, same convention already used for node rendering.

## 2026-07-25 (3)
- feat(arena): expand map to Arathi Basin size, 5 capture nodes (S170-119). Founder, real-time:
  "expand the redgarden map to arathi size and 5 nodes." `ARENA_NODE_COUNT` 2->5 and its wire
  mirror `ARENA_SNAPSHOT_NODE_COUNT` (packages/common/protocol.h), `ARENA_HALF_EXTENT` 12->20 for
  real room (ground plane, movement clamp, and minimap all derive from this constant already, no
  separate edits needed). New `arena_nodes_reset_layout()` lays out 5 nodes Arathi-style: two
  flanking each team's spawn (Stables/Farm near owner 0, Lumber Mill/Gold Mine near owner 1) plus
  one contested center (Blacksmith, 0,0). Jungle creeps (S170-51) scale to 5 automatically --
  `ARENA_MAX_CREEPS` is `#define`d off `ARENA_NODE_COUNT` and flavor derives from `node->owner`
  dynamically, no hardcoded index. One real hardcode found and fixed: Courier's W
  (`courier_toggle_w`, "Between Eagle and Serpent") assumed exactly 2 nodes ("always lands, there
  are always exactly two nodes to jump between" -- own comment, now false) -- generalized to a
  farthest-of-N loop; `docs/HEROES_VS0.md`'s Courier section and a stale test
  (`test_courier_w_teleports_to_farther_node`, previously hardcoded "node 1 is farther") updated
  to match. Second real bug found via test failure: the new center node at (0,0) collided with
  `test_arena_bot_enabled_gates_kit_casts_too`'s own arbitrary hero test position (also (0,0)) --
  a jungle creep spawning on top of the hero dealt damage the test misattributed to ungated bot
  AI; fixed by moving the test's positions off every node's aggro footprint (z=15), not by moving
  the node (the center node belongs on the direct line between both spawns, same "contested
  middle" design as real Arathi Basin). Verified: scripts/build.sh, scripts/build_arena.sh,
  scripts/test_arena.sh, scripts/test_10_bots.sh all pass; local mingw cross-compile (all 4
  source files) links clean.
