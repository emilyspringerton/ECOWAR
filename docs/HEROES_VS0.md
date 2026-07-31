# RED GARDEN — VS0 Hero Kits & Starting Item Roster

*Written: 2026-07-23 | Status: content pass, not implemented in code yet*

See `docs/BACKSTORY.md` for why these nine specific characters are a roster at all — they're
Tyler's motorcycle gang, not nine strangers a deck-builder happened to draft.

Per founder direction: this is a direct content pass on top of the hero queue in `NORTHSTAR.md`
§7 — no ability framework/engine work first, just concrete kits. Stat system leans heavily on
LoL/DOTA-familiar terms (attack damage, ability power, armor, magic resist, penetration, CDR,
lifesteal) on purpose — the roster already has enough that's unique; a totally foreign stat
system on top of that would be too much. Ability descriptions are written loosely enough to
interact with each other and with the living grid (see the RED GARDEN-specific passives below)
rather than being cleanly siloed — the LoL-style "spaghetti interactions" are a feature, not a
bug, of a healthy kit roster.

Shared archetypes across the roster (a hero can straddle two): **Fighter, Mage, Assassin, Tank,
Support, Indirect-Control** (the last one is a deliberate roster feature per NORTHSTAR §7's
"indirect-control archetypes" note — some heroes aren't directly commanded at all).

---

## Stat Legend

| Stat | Meaning |
|---|---|
| AD | Attack Damage — physical hit/ability scaling |
| AP | Ability Power — magic ability scaling |
| Armor | Reduces incoming physical damage |
| MR | Magic Resist — reduces incoming magic damage |
| Pen | Penetration — ignores a flat or % amount of the target's Armor/MR |
| CDR | Cooldown Reduction |
| AS | Attack Speed |
| MS | Movement Speed |
| HP / HP5 | Health / Health regen per 5s |

---

## Hero Kits

### The Donkey — moved to the item roster (S170-206)

**Update, 2026-07-29:** The Donkey shipped as an equippable item (`docs/HEROES_VS0.md`'s own
"Starting Item Roster" section below, `packages/simulation/arena_game.c`'s `ARENA_ITEMS`
catalog), not a hero. Founder, real-time, when asked to clarify the Indirect-Control/non-piloted-
unit blocker this entry originally described: "donkey should be an item." This sidesteps that
whole blocker entirely — no second targetable entity, no companion-slot system, no new
collision/render path — both of Donkey's original kit pieces (Immortal's Fold, the automatic
low-HP damage-floor + fight-back proc; Paper Glide, now a real tilde-activated high-speed escape
that flies over obstacles) trigger on whichever hero actually wears the item. Kept here as a
historical pointer, not deleted outright, since the roster numbering elsewhere in this doc
references "#38, Middle Kingdom Heirs" by TYLER lore ID. The "Starting Item Roster" table below
predates the current arena MOBA (it's the original card-RTS `local_game.c` item vision, never
actually wired into the live `ARENA_ITEMS` catalog -- Blink Dagger isn't in it either) -- the
real, current, shipped item stats live in `README.md`'s own item table, sourced directly from
`packages/simulation/arena_game.c`.

### The Duck ("A Duck, Reportedly Telekinetic", S, #103) — **Fighter/Assassin**

- **Q — Telekinetic Yank**: Pulls the nearest enemy unit toward the Duck, dealing AD-scaled
  damage on impact.
- **W — Government Clearance**: Passive — the Duck takes reduced damage from towers/objective
  structures ("technically authorized to be here"), and objectives take bonus damage from it.
- **E — Chosen One**: On landing the killing blow on a hero, briefly gain AD + MS ("of course it
  was me").
- **R — Total Telekinesis**: Channel, then violently yank every nearby enemy unit into a cluster
  at the Duck's position, dealing AoE AD damage on landing — combos hard with any AoE mage kit
  standing nearby (an intentional spaghetti interaction, not a bug).

### The Unicorn ("Allegedly a Robot", S, #104) — **Tank**

- **Passive — Chassis Claim**: Bonus flat Armor and MR ("I'm a robot, technically"), plus immunity
  to the first crowd-control effect applied to it each fight ("robots don't feel that").
- **Q — Diagnostic Charge**: Dash forward, dealing AD damage and briefly revealing stealthed units
  hit ("running a scan").
- **W — Spaghetti Vent**: Passive — while eating (channeling this ability does nothing visible),
  regenerates HP5; can be cast defensively to bait enemies into wasting an engage on a Unicorn
  that's just standing there regenerating.
- **R — Full Disclosure**: Taunts all nearby enemies to attack the Unicorn for 3s, during which its
  Armor/MR are doubled.

### The Ghost ("Not Really a Ghost, I'm an Alien", S, #105) — **Mage/Support**

- **Passive — Mid-Piano**: Every few seconds, the Ghost's next ability cast is silent (undodgeable
  — no visible cast animation), themed as it never actually stops playing piano.
- **Q — Alien Frequency**: AP-scaled magic damage skillshot; enemies hit are Silenced briefly.
- **W — Not a Ghost**: Become intangible (untargetable, can pass through units/terrain) for 1.5s.
- **R — Recital**: Channel an AoE zone that deals AP damage over time and heals allies standing in
  it — the same zone, same tick, opposite effect depending on team, a deliberately spaghetti
  double-edged ultimate.

### The Frog (Keeper of an Untold Secret, S, #106) — **Support/Indirect-Control**

- **Passive — Never Told Anyone**: The Frog's abilities have no visible cooldown UI for enemies
  (real cooldowns still apply) — bluffing is part of the kit.
- **Q — Loop Back**: Rewind the Frog (and only the Frog) to its position/HP from 3s ago.
- **W — Borrowed Time**: Target ally's next ability has its cooldown refunded on cast.
- **R — The Secret**: The Frog vanishes from the map entirely (no presence, can't be targeted or
  seen) for 5s, then reappears at any visited location this game — a hero that opts out of a fight
  entirely, on demand, rather than escaping it.

### The Tree (Keeper of the Universe's Greatest Secret, in French, S, #107) — **Tank — RED GARDEN passive**

- **Passive — Root Network (map interaction)**: While the Tree stands on a cell, that cell's
  `alignment_pressure` (see §3's `GridCell` struct) drifts toward the Tree owner's side over time
  — a slow, standing-still form of territory conversion distinct from card-deployed pressure.
  Rewards planting the Tree and defending it rather than roaming.
- **Q — Vine Lash**: AoE root in a cone in front of the Tree.
- **W — Untranslated**: Passive — abilities used near the Tree that would normally be interrupted
  by CC get one guaranteed cast per fight instead ("the secret" protects nearby allies once).
- **R — Grand Secret**: The Tree roots itself permanently in place, becoming immune to
  displacement and gaining massive HP/Armor, but cannot move again until the ability is
  recast (min. 8s) — a full commitment to holding ground, which pairs directly with the passive.

### The Pizza (On Fire, Uninvestigated, S, #108) — **Mage — RED GARDEN passive**

- **Passive — Uninvestigated Fire**: The Pizza is permanently on fire and immune to its own burn
  damage. Enemies near it take AP-scaled burn damage over time; the cell the Pizza currently
  stands on gains `CORRUPTED`-leaning pressure the longer it stays — the Pizza is, mechanically,
  a slow-moving corruption source nobody in the fiction ever notices.
  fire) get one guaranteed cast per fight instead.
- **Q — Nobody Checked**: Throw a burning slice; deals AP damage and leaves a burning patch on the
  ground.
- **W — I Am The Chosen One**: Passive — the Pizza's declared "ultimate form" (visual only) makes
  it look like it's about to cast something huge; enemies who flinch/reposition in response take
  no actual consequence either way (a pure mind-game ability with a real bait use case).
- **R — Nobody Ever Checks**: The Pizza can't be reduced below 1 HP for 4s (true to the lore — it's
  on fire this entire time and nobody has ever confirmed it's actually hurt by that).

### The Retrieval Cart (S, #10 — Jiangshi Syndicate) — **Indirect-Control (world event, not a unit)**

Matches NORTHSTAR §7's framing exactly: not a piloted hero. It's a recurring, unrequested
delivery event attached to a lane.

- **Passive only — Already Waiting**: On an irregular timer (not player-triggered), the Cart
  appears at a random point along a lane with a random buff/resource, and grants it to whichever
  allied unit is nearest when it arrives — no requester was ever logged, none is needed.
- **RED GARDEN passive (map interaction)**: Each delivery nudges the cell it appears on slightly
  toward the delivering side's `alignment_pressure`, modeling a passive supply line quietly
  converting territory over the course of a match.
- Design note: has no active-use kit at all by design — the entire hook is "you don't control
  this, you just benefit from it on its own schedule."

### Buer, "Doc Wheel" (S, #25 — Goetia Court) — **Support — RED GARDEN passive**

Played mundane on purpose, per the lore: no combat power, no flashy ultimate. The entire kit is
being the correct ally to have nearby.

- **Passive — Extremely Good At Medicine**: Doc Wheel's heals scale up the lower the target's
  current HP% is (best-in-class emergency healer, worst-in-class throughput healer on full-HP
  targets).
- **Q — Bedside Manner**: Single-target heal + cleanses one debuff. **S170-143, "add hover
  casting like in wow macros for healing, start with doc wheel":** now targets a real WoW-style
  mouseover -- whoever the caster's mouse is over at the moment of casting, falling back to the
  existing nearest-ally targeting when nothing's hovered or the hover target isn't a valid ally
  (an enemy, a corpse, nobody at all). Implemented as `arena_hover_ally_or_nearest()`, a drop-in
  swap for the plain `arena_nearest_ally()` every other ally-targeted ability still uses --
  picked as the first hover-aware ability specifically because it's the roster's clearest
  "you want to choose exactly who this lands on" case (a healer's whole kit is picking the right
  target), not because the mechanism is Doc-Wheel-specific. The cast itself carries the hover
  target over the wire (`ArenaCastCmd.hover_target`, -1 = none) rather than the client resolving
  targeting locally, matching the real WoW-macro shape of "the macro decides what unit=mouseover
  means, the server still owns whether the cast actually lands." Visual feedback lands on both
  ends: `cast_flash_slot` already covered the caster's own position; a new generic heal-flash
  (any HP increase, any source, not Doc-Wheel-specific) now fires at the TARGET's position too --
  the actual gap a mouseover heal exposed, since the target can be standing far from the caster.
- **W — House Call**: Move to an ally's location instantly, on a long cooldown ("always shows up").
- **RED GARDEN passive (map interaction)**: Whenever Doc Wheel heals a unit standing on a
  `CORRUPTED` cell, that cell's corruption pressure decays slightly faster — thematically, good
  medicine calms the ground it's practiced on.
- **R — No Combat Power, As Advertised**: Doc Wheel has no ultimate that deals damage. Instead,
  this fully removes all debuffs and grants max-HP-scaled shields to every ally in a wide radius —
  a pure defensive teamwide panic button, nothing else.

### TYLER — **Fighter/Assassin — exact reskin of DOTA's OG Meepo** (implemented S170-111)

Founder-requested: an exact copy of Meepo's classic kit, including the original (pre-rework,
unforgiving) "OG" clone-death rule, reskinned as TYLER rather than renamed into something softer.
Written here well before code existed for it (docs before software) — implemented for real in
S170-111, simplified from the literal multi-clone design below, documented honestly rather than
silently narrowed: this engine's `ArenaHero` slots are one-per-connected-client, not
multi-entity-per-player, so true clones sharing one HP pool weren't buildable without touching the
draft/pick/connection model the whole roster depends on. What shipped: **Q — Earthbind** roots +
a DoT (folds in Geostrike's poison, since there's no generic per-melee-attack passive hook to hang
a separate armor-shred off; S170-140: now a real travelling net-projectile, not an instant hit, per
its own "fires a net at a target area" wording above). **W — Poof** is a real instant blink-strike
to the nearest enemy — one body, one blink, not "every clone" (still true, see S170-141 below).
**R — Divided We Stand** keeps the actual point of the OG rule — real risk/reward — as a self-buff
that hits hard on cast and leaves Tyler's own armor negative for the window after.

**S170-141, "add tyler true meepo parity" (founder, real-time) — real puppet clones, team mode
only.** The blocker above was specifically about *player-controlled* clones (a second human/bot
independently piloting a body) needing draft/connection changes -- Meepo's actual identity doesn't
require that: the clones are the same one player's extension, not a second player. Built as AI-
driven puppets, not client-owned slots: R now claims up to `ARENA_TYLER_R_CLONE_COUNT` (2) free
slots from a small dedicated puppet pool (`ARENA_MAX_CLONE_SLOTS`, appended after the real
per-player range so it never competes with an actual connecting client for a slot), spawns each as
a real `ArenaHero` entry (`is_clone=1`, `clone_owner`=Tyler's own owner index) at Tyler's position
with half his max HP, on top of the existing self-buff. Puppets mirror Tyler's own move-target every
tick (click once, the whole clone army goes) and fight through the same generic nearest-enemy/melee
loop every real hero already uses -- extended to see the puppet range -- so enemy heroes can find
and kill a clone exactly like a real hero, no special-cased combat path. **Real shared fate, for the
first time**: `apply_damage`'s death branch, on killing Tyler or any clone linked via
`clone_owner`, kills every linked entity outright in the same tick -- the literal OG rule, not the
armor-debuff stand-in alone (which stays, on top). Clones are excluded from
`arena_team_alive_count` (a lone surviving clone doesn't keep a wiped team's match alive) and from
`arena_tick_respawns` (they don't respawn independently -- Tyler has to recast R after his own
respawn to rebuild the clone army).

**S170-170 follow-up, "true meepo parity" continued:** W (Poof) now teleports every active clone
linked to Tyler alongside him to the exact same point, each independently landing its own
arrival-damage check against the same target -- the original design's "TYLER and every active
clone teleport to the target point," simplified the same honest way the rest of this kit already
is (single-target instant damage on arrival, not a true two-point AoE at both the departure and
landing spot). Concentrates the whole clone army's arrival damage onto the one enemy Tyler jumped
to -- the real "full-team dive tool" identity, expressed through this engine's existing simplified
hit model.

**2026-07-30, "Divided We Stand" rework -- founder: "his kit was stubbed in" / "clones multi
control drag click all of it."** Two real, separate gaps closed in the same pass:

1. **Clones were never actually visible in a real networked match at all.** The hero-snapshot
   wire-sync loop only ever covered owner slots 0..lobby_size-1 -- Tyler's puppet-clone pool
   (`ARENA_MAX_HEROES..ARENA_HEROES_ARRAY_SIZE-1`) existed and fought server-side since S170-141,
   but no client, not even Tyler's own, ever received a single byte of clone state over the wire.
   Not a control-scheme gap underneath a working render -- a rendering gap the control-scheme work
   below surfaced. Fixed: `ArenaHeroSnapshot` gained `is_clone`/`clone_owner`, the hero-chunk system
   widened to cover the full clone-inclusive range (`ARENA_SNAPSHOT_HEROES_ARRAY_SIZE`, chunk size
   10->14, still exactly 2 chunks -- no new MTU-fragmentation risk), and `apps/arena` gained a
   dedicated clone-body draw pass (`draw_hero_model`, same self/team/enemy relationship-color
   convention real heroes use) plus real floating health bars for clones.
2. **"Mirror Tyler's own move-target every tick" was never true Meepo parity -- it was the
   opposite of it.** Real Meepo nets are independently commanded; this engine's own puppets just
   auto-followed whatever Tyler himself clicked, with no way to split them up at all. Removed
   entirely (`arena_update_teams` no longer has any clone-specific tick logic -- a clone is just
   another hero with its own real `target_x`/`target_z`/`moving` state now, ticked by the exact
   same generic motion loop everyone else uses). New `arena_owner_controls(sender, target)`
   authorizes a client to command itself OR one of its own active clones; `PACKET_ARENA_MOVE`/
   `PACKET_ARENA_ATTACK` both gained a `unit_owner`/`commander_unit` field naming which of the
   sender's own units a given command is for. Client-side, `apps/arena` gained real RTS drag-select
   (industry-standard drag-vs-click disambiguation on mouse-up, no new keybind): a plain click
   commands whatever's currently selected (defaults to just Tyler himself, so every other hero's
   muscle memory is completely unaffected), dragging a box over Tyler's own clones selects them
   individually for their own move/attack commands, exactly like a real Meepo player splits their
   army. `apps/arena_bot`'s own 19-real-bot pool updated too (its move/attack senders now populate
   the new field explicitly with its own owner slot -- an uninitialized stack byte there would have
   been a real, silent authorization-check bug).

**What's still simplified, flagged not faked:** clones are still melee-only -- they don't
independently cast Q/W/R (only Tyler's own real input drives ability casts), so "every melee
attack from TYLER or any clone" applies Geostrike's DoT for Tyler's own hits only, not the clones'.
Clones still aren't targetable by jungle/lane creeps or projectiles (only real hero-vs-hero melee
sees them) and don't participate in node-capture presence. No bot AI exists for independently
piloting a bot-controlled Tyler's own clones (a bot always just commands its own single body,
same as before this rework) -- a real, separate, much bigger scope than client-side human control.
A clone's on-screen facing is computed fresh each frame from its own current move target rather
than the smoothed per-frame tracking real heroes get (`hero_facing_rad`) -- a clone snaps to face
its target instantly instead of easing into it, and clones don't get the move/cast squish-pulse
animation real heroes do -- both real, minor rendering simplifications, not incorrect gameplay.
Still simulated in team mode only (`arena_update_teams`) -- the 1v1 local practice demo's own tick
functions are hardcoded to exactly two heroes and don't loop over the puppet range at all, so
casting R there still grants the self-buff but any claimed clone slot sits inert, unmoved and
non-combatant, until a real team match runs it.

- **Q — Earthbind** *(original design)*: Fires a net at a target area; any enemy hit is rooted and
  treated as a bigger hitbox for a few seconds (classic setup for the blink-strike below).
- **W — Poof** *(original design)*: After a short delay, TYLER and every active clone teleport to
  the target point, dealing AoE AD damage both where each one left from and where each one lands.
  Chainable with itself across clones — a full-team dive tool in the hands of a good player.
- **E — Geostrike (passive)** *(original design)*: Every melee attack from TYLER or any clone
  reduces the target's Armor and applies a stacking poison DoT.
- **R — Divided We Stand** *(original design)*: Splits TYLER into an additional clone (up to 5
  total), each with a percentage of TYLER's stats and sharing TYLER's items and cooldowns. **OG
  rule, exactly as requested**: all clones share a single pool of fate — if any one TYLER dies,
  every TYLER dies, no exceptions. High-risk, high-reward, exactly like the original.

### Flamel — **Support — Alchemist-Gardener** (merged with the former "Druid" entry, 2026-07-24)

Nicolas Flamel, already in `TYLER/multiverse_heroes.md`'s roster of reframed real historical
figures. Founder direction: Druid and Flamel should be the same hero, not two separate roster
slots — the generic "Druid" archetype below was never actually a named TYLER lore character (no
entry for it in `multiverse_heroes.md`), while Flamel is; consolidating a nameless archetype into
an already-established named figure rather than carrying two adjacent support/growth heroes.
Flamel keeps his name and his lore ("The Great Work" is the real historical term for the
alchemical magnum opus, and reads naturally as *cultivation*, not just cooking); Druid's kit
supplies the actual moveset, since it was the more self-contained and less economy-blocked of the
two (three of its four pieces need no cooking/resource economy at all — only Q needed nothing
extra, and the old Flamel kit was entirely ally/economy-dependent with no self-contained piece).
The result: Flamel is the roster's first hero whose entire kit is the territory/growth system
made literal, not the cooking system — cooking (`docs/CONSUMABLES_AND_COOKING.md`) stays a
future, separate economy layer this kit doesn't require to function.

- **Passive — The Great Work**: Flamel's presence marks the ground he crosses (folded in from
  Druid's Overgrowth) — a marked cell that's still `NEUTRAL` at tick's end has an increased
  chance of converting toward his side. Cooked consumables prepared on his own marked ground are
  stronger and cheaper than the same recipe prepared elsewhere — alchemy is literally cultivation
  here, not recipe-following.
- **Q — Vine Growth** (from Druid, self-contained — no ally or economy needed): grows a temporary
  wall of vines in a line, rooting anything caught in it; decays after a few seconds.
- **W — Philosopher's Bloom** (Druid's Bloom + Flamel's Philosopher's Batch, merged): heals every
  nearby ally at once instead of one at a time, healing for more if cast on ground Flamel has
  marked — rewards playing on your own seeded plot.
- **R — Elixir of Wild Growth** (Flamel's Elixir framing, Druid's Wild Growth shape): a single
  powerful team-wide cast, cooked/grown once per game (long cooldown) — a large area rapidly
  overgrows, heavy-slowing enemies caught in it, healing-over-time to allies standing in it, and
  marking every cell inside simultaneously. The "master" part of master cook/gardener, reserved
  for exactly the moment a team decides to commit.

### The Morrigan (TYLER `multiverse_heroes.md` #68) — **Assassin/Duelist — meta jungler**

Built to resolve this doc's own earlier "flagged, not built" note below: the founder-observed
Morrigan/Flamel (formerly Druid) relationship — a shared Highland-Court territory tie with real
rock-paper-scissors counter-play (her war/death kit against Flamel's life/growth kit) — realized
directly through the territory/node system both heroes now hook into. Founder direction called her
a "meta jungler": with no standalone jungle-camp system in this arena yet, her jungler identity is
expressed as an affinity for *contested* ground rather than claimed territory — she belongs to the
unresolved fight, not the settled one.

- **Passive — Contested Ground**: gains bonus armor while standing on a node that's still
  contested (neither team has claimed it) — she's drawn to ground that hasn't picked a side yet.
- **Q — The Washer's Strike**: a ranged strike that hits harder the lower the target's current HP
  is — an execute, not flat damage; "the crow confirms the kill" the closer death already is.
- **W — Three Forms** (the eel underfoot, the wolf stampeding cattle, the hornless heifer leading
  it): closes distance instantly onto the nearest enemy and roots them on arrival — she appears
  where he doesn't expect, in another shape entirely.
- **R — The Crow Confirms It**: a fixed battlefield zone that ticks execute-scaled damage to any
  enemy standing in it for its duration — the lower an enemy's HP drops inside the zone, the harder
  each following tick lands. No ally-support side, unlike Ghost/Flamel's R zones — a war goddess's
  ultimate isn't a support tool.

### The Dagda (TYLER `multiverse_heroes.md` #69) — **Bruiser/Support — two-natured**

"The wheeled club settles every argument twice" — one end kills, the other revives, same tool,
depending only on which end swings first. Paired with the Morrigan in the lore (the one alliance
in TYLER's roster "both parties remember happening and neither one explains") and built alongside
her here.

- **Passive — The Undry**: the cauldron that never runs empty — passive, always-on HP regeneration,
  no cast required. "No one leaves it unsatisfied."
- **Q — The Wheeled Club**: literally two-natured, exactly as the lore describes — swings the
  killing end at a hittable enemy in range if one's there; otherwise swings the reviving end,
  healing a hurt living ally in range instead. Simplified from a literal revive (no respawn system
  exists in this arena to revive a dead ally into) to a strong heal — the same tool, either
  direction, depending only on what's actually in range when it swings.
- **W — Uaithne, Called By Name**: the harp's three master strains — sorrow, joy, and sleep —
  played over the whole hall in one go, exactly as in the myth. One AoE cast: allies in range get
  joy (healed), hittable enemies in range get sorrow and sleep at once (rooted and silenced).
- **R — The Porridge**: force-fed enough to kill an ordinary man, eaten unhurt, then still fighting
  the next day regardless — a real damage floor (HP cannot drop below 1 for the duration) plus a
  real heal on top, not just survival. Enduring **and** coming out ahead.

### The Courier (TYLER `multiverse_heroes.md` #32, "Ratatoskr's Debt-Collector") — **Fighter/Assassin**

Has carried insults up and down Yggdrasil, between the eagle at the crown and Nidhogg at the root,
for centuries — and has started editing them along the way, a job that was never meant to involve
judgment. That fixed-endpoint framing maps directly onto this arena's `ArenaNode` positions rather
than needing a new system: The Courier's whole kit is about running the line between fixed points
and taking a cut off whatever passes through. (S170-119: the arena grew from 2 nodes to 5 —
Ratatoskr runs the whole tree now, not just the one branch, same "always real progress" property.)

- **Passive — Lightly Edited**: whenever The Courier's Q lands a hit, it cleanses any active debuff
  on him (silence, root) — he edits the message addressed back to him out of the delivery.
- **Q — The Insult, Lightly Edited**: dashes toward the nearest enemy (same shape as Unicorn's
  Diagnostic Charge) and deals damage on arrival — the insult delivered in person.
- **W — Between Eagle and Serpent**: instantly repositions to whichever map node is farthest from
  his current position — always making real progress along the tree, a pure mobility tool distinct
  from every other hero's teleport (which are all ally- or foe-relative; this one is
  fixed-geography).
- **R — The Debt Collector's Due**: a long-tenured job has, over time, started to involve judgment
  it was never meant to — seizes a flat amount of HP from the nearest enemy and delivers it to
  himself, a forced collection rather than a fair trade.

---

### Loki (TYLER `multiverse_heroes.md` #37, "Loki, Who Isn't Here") — **Trickster/Bruiser** (S170-79)

The one figure in his own myth the compendium left out of the document — every other account of
him arrives secondhand, through Sigyn (#34) holding the venom-bowl "for as long as the myth
demands, and then kept holding it." His whole kit works the same way: not a straightforward
stat-check like most of the roster, but repositioning and endurance, presence registered only as
interference on someone else's reading rather than a signal of his own.

- **Q — Interference, Not a Signal**: an instant positional swap with the nearest enemy — no
  travel time, no dash arc, unlike every other dash-shaped Q in this roster. He's simply, suddenly,
  where the enemy was; a small hit lands on arrival if the swap puts them in range of each other.
  No range limit — the trade-off is a real cooldown, not a whiff condition.
- **W — Bound Where the Myth Says**: a free toggle (no cooldown, same convention as Unicorn's
  regen), granting a flat armor bonus while active — a defensive stance, not sustain.
- **R — Held For As Long As The Myth Demands**: self-cast survive-floor window (same
  `survive_floor_ms` mechanic Pizza and Dagda's ultimates already use) — HP can't drop below 1 for
  the duration. Someone else holds the outcome open for him, the way the bowl does in the myth.

---

### Gary (TYLER `multiverse_heroes.md` #35, "Gary, Bifrost Security (Off-Duty)") — **Marksman** (S170-91)

No magic — "extraordinary eyesight, extraordinary aim," and a job that never actually ends because
someone always has to be watching the bridge. The one hero in this roster with no dash, no
teleport, no gap-closer of any kind: Gary doesn't chase, he watches from where he's standing.

- **Q — The Property**: a stationary long-range precision shot at the nearest enemy — no movement
  at all, range-gated instead of a hit-radius-after-a-dash like most of the roster's Qs. Fired as a
  real travelling projectile (S170-136, the arena's first) rather than an instant hit: it takes real
  flight time to cross the distance, so a foe that steps off the firing line before it arrives
  genuinely dodges it — no homing. The range check still gates the cast itself (Gary needs a shot
  lined up to fire at all), but landing the hit is no longer guaranteed.
- **W — Aimed Shot** (S170-203): no longer a free toggle — a real WoW Hunter-style cast-time
  nuke, big single-target damage on a real cooldown. Needs a hittable foe in range to even begin
  (same "needs a shot lined up" commitment as Q); once the 1.5s wind-up starts, moving (a fresh
  move command or a forced displacement — a pull, a knockback) cancels it, and being silenced
  cancels it, but taking damage does not. A cast bar renders over Gary's head the whole time,
  visible to everyone, not just him.
- **R — "Slow Down, This Isn't a Track Meet"**: a fixed-duration root on the nearest enemy — the
  same "slow simplified to a full stop" convention Tree's R/Flamel's R already use.

---

### Flute Debt (TYLER `multiverse_heroes.md` #42, "Han Xiangzi's Flute-Debt") — **DoT/Payoff** (S170-91)

One of the Eight Immortals, patron of musicians — "owes something to every wrong note ever played
near him, and eventually collects." The kit is a real debt mechanic, not a metaphor: apply it with
Q, cash it in with R.

- **Q — The Wrong Note**: modest immediate damage plus the shared `burning_ms`/`burn_dps` DoT
  fields (Pizza's mechanic, S170-46) — the debt accruing.
- **W — Recouping Interest**: a free toggle self-heal-over-time (same shape as Unicorn's regen) —
  passively collecting even outside a fight.
- **R — Eventually Collects**: always lands and consumes the cooldown, but deals real bonus damage
  if the target still has the Q's debt active, base damage otherwise. The actual payoff of the
  kit's theme — the debt has to still be open for it to collect big.

---

### Bacon+Puck (TYLER `multiverse_heroes.md` #5 + #67, merged) — **Trickster/Skirmisher** (S170-94)

Two entries merged into one hero, same pattern as Flamel/Druid. Bacon's whole character is
withholding — "custodian of the one location nobody's allowed to know yet," seed phrase "ask
again later." Puck's is an unresolved duality nobody can confirm the real version of. Combined:
a kit built around not being pinned down.

- **Q — Ask Again Later**: self `intangible_ms` (the shared can't-be-hit status, S170-32) — for
  longer while W is toggled on.
- **W — Which One Is The Real One**: a free toggle (no cooldown) that extends Q's own
  intangibility duration, rather than granting a stat like most toggles in this roster.
- **R — The Trick Was Always the Same**: real damage plus a self-heal off a fraction of it —
  the mischief pays for itself either way.

---

### Abraham the Mage (TYLER `multiverse_heroes.md` #113, "Abraham of Worms, the Mage") — **Caster** (S170-103)

The disputed author of a real grimoire — *The Book of the Sacred Magic of Abramelin the Mage* —
whose own existence has never been confirmed, but whose ritual real occultists (Crowley) actually
organized their lives around centuries later.

- **Q — The Sacred Magic**: a real ranged magic bolt, stronger while W is toggled on.
- **W — The Book, Unattested**: a free toggle (no cooldown) that boosts Q's own damage rather than
  granting a stat like most toggles in this roster.
- **R — The Guardian Angel, Contacted**: a full self-cleanse (every debuff this roster tracks)
  plus a real heal — the ritual's actual promised payoff.

---

### Ada Lovelace (TYLER `multiverse_heroes.md` #112, "Ada Lovelace, Pilot") — **Tank/Controller** (S170-103)

Wrote the first computer program for a machine that wasn't built for a century — cast here as a
mech pilot, someone who wrote the operating logic for a frame before the frame existed.

- **Q — The Analytical Engine**: computes the nearest enemy's movement to a halt — a real root,
  same "slow simplified to a stop" convention Tree's/Flamel's/Gary's R already use, here on Q.
- **W — Poetical Science**: a free toggle (no cooldown) flat armor bonus — the frame's own plating.
- **R — The First Program, Run a Century Late**: the engine finally executes — real damage plus a
  short follow-up root.

---

### Paimon (TYLER `multiverse_heroes.md` #20, "Paimon, the Court Voice"; channeled by John Dee) — **Commander/Caster** (S170-55)

Founder: "add john DEE /paimon as the same hero" — same merge discipline as Flamel/Druid (S170-46/47)
and Bacon+Puck (S170-94): checked `multiverse_heroes.md` first. Paimon has a real entry (#20, a
Goetia king who "commands two hundred legions and would rather talk than fight," "teaches all
arts," and spends his political capital keeping the rest of the Court from escalating disputes);
John Dee — the real Elizabethan mathematician, astrologer, and royal advisor who spent his later
career on Enochian spirit-communication with Edward Kelley — has no entry of his own here. Same
resolution as Flamel/Druid: keep the name with the real compendium backing (Paimon), fold the other
in as the vessel/practitioner whose real work manifests the character's power — Dee is the historical
mage whose scrying summons the Court Voice, not a separate identity.

- **Passive — Keeping the Peace**: periodically silences the nearest enemy in range without being
  cast — the same always-on aura-tick pattern as Pizza's Uninvestigated Fire (S170-46), themed as
  Paimon talking a fight down before it escalates rather than burning.
- **Q — Teaches All Arts**: a ranged bolt that damages and roots — a demonstration of technique,
  same instant-hit-if-in-range simplification as Ghost's/Tree's/Flamel's Q.
- **W — Speaks With Total Authority**: an instant decree — damages and silences the nearest enemy on
  its own cooldown, the Court Voice backed by real consequence, not just a talking-down.
- **R — Two Hundred Legions**: a fixed battlefield zone (same shape as Ghost's Recital/Flamel's
  Elixir of Wild Growth) — enemies inside take periodic damage, allies inside are periodically
  healed, the literal presence of a commanded army felt by both sides at once.

---

### NOOR-1 ("Four Days Behind", TYLER `multiverse_heroes.md` #3, Jiangshi Syndicate MUNDANE) — **Scout** (S170-104)

Founder: "add NOOR-1 as a snowman." NOOR-1 has a real entry — dispatched with no Tyler history and
no file access, on explicit instruction "find him, do not approach, file what is actually there,"
and assessed by her own subject as "about four days behind where they think they are": an operative
sent to observe, being read by the person she's observing before she's filed a single word on him.
"As a snowman" reads as an in-game FORM directive, the same convention the original roster already
uses (a Duck, a Unicorn, a Pizza, a Tree — ordinary objects/figures given a hero identity), not a
change to her lore. A snowman standing quietly in a scene, unremarkable, watching without being
seen, is the honest visual match for "sent in clean" and "do not approach" both at once.

- **Passive — About Four Days Behind**: periodically silences the nearest enemy in range without
  being cast — the same always-on aura-tick pattern as Pizza's Uninvestigated Fire/Paimon's Keeping
  the Peace, themed as NOOR-1 reading the enemy's next move before they've committed to it, cutting
  it off before it lands.
- **Q — File What Is Actually There**: a ranged bolt that damages and roots — the read becomes a
  report, and the report is binding; same instant-hit-if-in-range shape as Ghost's/Tree's/Paimon's Q.
- **W — Sent In Clean**: a brief self-cast intangibility on its own cooldown, same mechanic as
  Ghost's Not a Ghost — she goes quiet and unreadable herself for a moment, an escape or reposition
  tool, not just a stealth flourish.
- **R — Do Not Approach**: a fixed cold zone, same shape as Ghost's Recital/Paimon's Two Hundred
  Legions — enemies standing inside take periodic damage, the literal instruction made into ground
  nobody can safely stand on.

---

### Cain, East of Eden (TYLER `multiverse_heroes.md` #80, "Cain") — **Duelist** (S170-105)

Founder: "add Adelle" → "to the guide in tyler first" → "and then to the game" → "then the boys do
a podcast with her." Asked which identity anchor "Adelle" should draw on, since — unlike every
other new hero this session (Loki, Abraham, Paimon, NOOR-1), all of which map to a real
mythological/historical figure or an existing Jiangshi file — "Adelle" alone had zero anchor
anywhere in the TYLER corpus. Founder's answer: "replace adelle with Cain." Cain already has a
real entry (#80, Faction 7/the Abyssal Choir) — Genesis's own account taken at face value: killed
his brother Abel, was cursed to wander the earth as a fugitive, and was given a mark by the same
authority that cursed him, specifically so no one who found him would be permitted to kill him in
turn — "the oldest example on this entire roster of a punishment that is also, unmistakably, a
mercy, and scripture never resolves which one it actually is." Went on, after the exile, to found
the first city anyway. No new lore needed — the existing entry already carries the whole kit.

- **Passive — Founded the First City**: a flat, always-on armor bonus, same shape as Unicorn's own
  passive (minus the R-doubling) — the one permanent thing about a man cast out to wander forever.
- **Q — The First Murder**: a ranged bolt, execute-scaled (bonus damage the closer the target
  already is to death), same shape as Morrigan's Q — a killing blow that gets easier to land the
  more inevitable it already is.
- **W — Cursed to Wander**: an instant dash directly *away* from the nearest enemy plus a self-
  cleanse, the mirror of Courier's Q (which dashes toward and cleanses) — the fugitive shakes off
  what's chasing him and puts distance between them in the same motion.
- **R — The Mark**: a survive-floor panic button, same shape as Pizza's/Loki's R — "a mark that is
  a curse and a protection at the same time," made literal: for its duration he cannot be killed,
  even by the thing that marked him.

Podcast stage (S170-105's third leg, "the boys do a podcast with her/him") not started this pass —
lore and kit land first, matching the founder's own sequencing.

---

### Gunnr, Who Argued With a Raven (TYLER `multiverse_heroes.md` #30, "The Raven-Caller") — **Duelist** (S170-93)

Founder, in a rapid real-time batch (S170-93): "add gunnr." A shieldmaiden with no magic of her
own — "told one of Odin's own ravens it was wrong, and was right," then quietly proven right about
three more things since, none of it formally acknowledged. A plain, mundane fighter, not a mythic
or magical one — the roster's own honesty about who actually gets credited.

- **Passive — Quietly Been Right**: a flat, always-on armor bonus, same shape as Cain's/Unicorn's
  own — the shieldmaiden's stance, always ready for the fight everyone else dismissed.
- **Q — Argued With a Raven**: a plain melee-range strike, damage only, no status effect — a
  correction, not a flourish.
- **W — Consecration** (2026-07-30, founder: "gunnr w switch it to consecration just like wow" ->
  "same dot cast radius cd"): no longer a free toggle self-regen — a real WoW Paladin Consecration,
  a ground zone at Gunnr's own feet on a real cooldown, damaging any enemy standing in it every
  second for its duration. A shieldmaiden holding ground, not kiting for sustain. DPS/radius/
  duration/cooldown copied from Ghost's own Recital zone (the simplest existing "flat DPS zone, no
  extra mechanic" template) — unlike Ghost's, Gunnr's has no ally-heal side, matching real
  Consecration's enemies-only damage. Reuses the same `r_zone_x`/`r_zone_z`/`r_active_ms` state
  every other zone ability already shares, just triggered from W instead of R — a zone is a zone
  regardless of which slot cast it.
- **R — Valhalla Has Yet To Admit It**: an execute-scaled burst, same shape as Morrigan's/Cain's Q
  — the vindication finally lands, hardest against a target who's already nearly beaten.
  (2026-07-31, founder: "give gunnrs e a stun"): also stuns whatever it hits, same range check as
  the damage, no separate targeting pass. Duration copied from Zagan's own W (The Standstill,
  S170-230) — this roster's second-ever `arena_apply_stun` call.

---

### Vassago, the Soft Foresight (TYLER `multiverse_heroes.md` #16, "Vassago") — **Support/Diviner** (S170-93)

Founder, same rapid batch as Gunnr (S170-93): "add vassago." Not just a lore entry — real TYLER
canon: the Eastwind Owls' whole working frequency (11.11 Hz) is his, named directly in
`TYLER/CLAUDE.md`'s own Goetia frequency table. "Reveal the gentle maybe" is his seed phrase —
foresight that arrives soft, not declared boldly.

- **Passive — The Frequency**: a small, always-on HP regen, same shape as Dagda's Undry —
  ambient restorative foresight, sensing and softening harm before it fully lands.
- **Q — Reveal the Gentle Maybe**: a ranged bolt, damage + silence, same shape as Ghost's Q —
  foresight cuts off the enemy's next intended action before they take it.
- **W — The Soft Foresight**: grants the nearest ally `next_cast_refund`, same mechanic as Frog's
  Borrowed Time — the foresight extended outward, a teammate's next cast comes free.
- **R — The Gentle Maybe**: a fixed zone, silence-only, no damage at all — the one hero on this
  roster whose ultimate is pure control. Not a hit. A held breath.

---

### He Xiangu, Who Stopped Eating (TYLER `multiverse_heroes.md` #39, "He Xiangu") — **Support/Sustain** (S170-93)

Founder, same batch (S170-93): "add xiangu." One of the traditional Eight Immortals — achieved
immortality by subsisting on mother-of-pearl and moonlight, and never once explained why, never
once framed it as sacrifice. Self-denial taken past the point anyone else was willing to follow.

- **Passive — Subsists on Almost Nothing**: a small, always-on HP regen, same shape as Dagda's
  Undry.
- **Q — Mother-of-Pearl and Moonlight**: a ranged bolt that heals her for a fraction of the
  damage it deals, same heal-off-a-fraction mechanic as Bacon+Puck's R — but repeatable, on every
  Q, not a one-off burst. The first real sustain-through-combat on this roster.
- **W — Self-Denial**: a free toggle, no cooldown, a second layer of regen on top of the passive
  while active, same shape as Flute Debt's Recouping Interest — discipline, not deprivation.
- **R — Never Once Framed It As Sacrifice**: a fixed zone, heal-only, no enemy damage at all — the
  mirror of Vassago's purely-controlling R. She shares her sustenance. She doesn't hurt anyone.

### Beleth, the Detonation (TYLER `multiverse_heroes.md` #14, "Beleth") — **Burst/Control** (S170-93)

Founder, same batch (S170-93): "add beleth." Real TYLER canon — 2.22 Hz, the emotional-detonation
frequency. "No love story in the show's history has resolved without her frequency somewhere in
its last act," and Beleth has started to suspect that isn't coincidence so much as something
closer to authorship. Seed phrase: "hope is a terror I leash with song."

- **Passive — Every Love Triangle**: a small, always-on flat armor bonus, same shape as Cain's/
  Gunnr's own — she's survived every escalation she's ever caused.
- **Q — Every Love Triangle**: a ranged bolt that damages and burns, same shape as Pizza's Q —
  damage that keeps paying out after contact, matching "no love story... resolves without her
  frequency somewhere in its last act."
- **W — Hope Is a Terror I Leash With Song**: an instant decree on the nearest enemy, same
  in-range shape as Paimon's Speaks With Total Authority, but silence-only — no damage component
  at all. Pure escalation-denial, not a hit.
- **R — The Detonation**: the roster's first delayed-payoff ultimate. Marks the target's current
  position at cast time and starts a silent fuse; the instant the fuse hits zero, whoever's still
  standing in the zone takes one large burst — not a continuously-ticking zone like Ghost's/
  Vassago's/He Xiangu's own R zones. The threat builds in total silence and only resolves once,
  all at once, exactly like the thing she's named for.

### MnM, the Shapeshifting Crab (TYLER `multiverse_heroes.md` #114, "MnM") — **Tank** (S170-134)

Founder, real-time: "add MnM a shapeshifting rapping crab tank from detroit to the lore docs
first" → "have tyler and mid-piano cowrite it." The lore entry itself is framed as literally
co-written by Tyler and Mid-Piano — a crab the size of a doorway who raps and never holds one
shell long enough to finish an argument. Mid-Piano's own contribution to the bit: the
shapeshifting isn't a power, it's just what happens to a body that's spent that long absorbing
hits meant for somebody else. That line is the kit, mechanically, not just flavor text.

- **Passive — Detroit's Own**: a flat, always-on armor bonus, same shape as Cain's/Gunnr's/
  Beleth's own — the tank archetype's baseline stat profile, not a conditional buff.
- **Q — Clamp Down**: a melee-range poke that damages and roots, same shape as Paimon's Q.
- **W — Burrow** (reworked S170-208, founder: "switch MnM w to burrow where he digs down below
  the map and is untargetable in that time dealing small aoe damage when he comes back up"): a
  real cast on a real cooldown now, not a free toggle. MnM digs below the map — untargetable and
  rooted in place for the duration, same intangible+root combo his own R reaches for, just
  shorter — then erupts back up at the exact spot he went under (no reposition component: he
  "comes back up," not somewhere else), dealing a small AoE hit to whoever's standing there.
- **R — Absorbing Hits Meant For Somebody Else**: the literal mechanical translation of
  Mid-Piano's line. Self-root + a guaranteed-survival window (HP cannot drop below 1 for the
  duration, same real damage-floor mechanic as Pizza's R) — combining two existing generic
  fields the same way Tree's Grand Secret does, just with `survive_floor_ms` standing in for
  Tree's armor buff. The shell takes the hit instead of the crab underneath it.

---

### Weatherman (TYLER `multiverse_heroes.md` #45, "Ao Guang's Weather-Debt Collector") — **Fighter/Support** (S170-206)

9.0 Hz, "collects on storms owed and storms overdrawn, for the Dragon King of the East Sea" — a
demigod whose entire ledger is meteorological, every flood/drought/unseasonable calm somewhere on
his books, balanced against a debt system nobody outside his office fully understands. Seed
phrase: "the debt compounds with the barometer." The roster's first kit built around wind/
displacement rather than direct damage.

- **Passive — The Ledger**: flavor-only for this first pass — reuses Dagda's own Undry always-on
  regen shape rather than a real alternating storm-debt buff/debuff cycle, a legitimate richer
  follow-on.
- **Q — Barometric Shove**: a ranged wind gust that knocks the target back a fixed distance —
  displacement only, no damage at all, the roster's first push-*outward* Q (Duck's own Q/R pull
  inward).
- **W — Collects On What's Owed**: cast on the nearest hero, ally or enemy. Cast on an enemy
  currently mid-Paper-Glide (see the Donkey item, "Starting Item Roster" below): immediately
  grounds them — "the debt catches up to you no matter how far you fly." Cast on an ally
  currently mid-glide: extends it instead, a tailwind, not a headwind — same "same zone, opposite
  effect depending on team" shape Ghost's Recital already established, applied to a targeted cast.
  Whiffs (no cooldown spent) if nobody nearby is currently airborne at all, the overwhelmingly
  common case.
- **R — The Debt Compounds**: a fixed AoE zone dealing periodic damage to enemies standing in it —
  the literal storm finally collecting, biggest and simplest ability on the kit by design, so the
  real design surface stays on W.

---

### Zagan, the Standstill's Confessor (TYLER `multiverse_heroes.md` #19, "Zagan") — **Control/Disruptor** (S170-230)

*"Presided over a forty-seven-minute monologue that's still being peer-reviewed."* Field signature
0.618 Hz · Δφ 47° — well outside the Golden Band, still being peer-reviewed. Seed phrase: "the
standstill confesses." Built directly from `TYLER/lore/activation_47_transmutation.md`, the full
monologue transcript itself — six classical alchemical stages mapped onto a proof of the Riemann
Hypothesis (Calcination → Dissolution → Conjunction → Fermentation → Distillation →
Coagulation) — plus two okemily.com posts about it ("Activation #114," "Ten Heroes Worth a Closer
Look") that both independently land on the same thesis: Zagan's power should stay an unconfirmed,
hedged claim, funnier and more interesting for being unresolved rather than a clean verified one.
Founder: "hero ZAGAN" → "unique kit adds stun" → "think of a way to give ZAGAN a unique kit that
changes meta."

- **Passive — Base Metal Screams**: the transcript's own COAGULATION line made literal ("base metal
  screams when it remembers that it was never anything but gold with a wrong address") — the first
  time ANY enemy hero's HP crosses below 50% in their current life, Zagan "hears" it and gains a
  flat Flow bounty. No proximity or damage-source requirement at all — he presides, he doesn't have
  to land the hit. A threshold-*crossing* trigger, not periodic or proximity-gated, new to this
  roster.
- **Q — Calcination**: the transcript's own first stage ("primes are the incorruptible seeds...
  everything else is alloy") — a single hit plus a lingering armor-shred debuff, the impurity
  burned away to reveal what was underneath. Control/setup, deliberately no periodic burn damage
  on top.
- **W — The Standstill**: the literal mechanical translation of "Standstill's Confessor" — forces
  stillness onto a nearby enemy. This roster's first-ever kit to actually apply a stun; the generic
  infrastructure (`stunned_ms`, gating move/cast/attack) has existed since S170-184 but no kit used
  it until now.
- **R — Conjunction**: the transcript's own CONJUNCTION stage made literal ("the exact
  conjunction... where body and spirit are wed in equal measure," mirrored at Re(s)=1/2, "the only
  place the mirror permits mass to rest") — for the duration, Zagan's TOTAL armor becomes exactly
  equal to a locked target's, a true live mirror, not an additive steal. This is the actual
  meta-changing hook, not just a new number: R against a squishy target makes ZAGAN squishier too,
  a real cost that punishes always-R-the-biggest-threat play and rewards diving a tank instead — no
  other ability on this roster can make its own caster weaker as the direct cost of using it.

---

### Warrior — **DragonsNShit job, not a TYLER hero** (GoblinFoxDragon `docs2/REDGARDEN_GUI_NORTHSTAR.md` Milestone 1, 2026-07-31)

Not part of the TYLER `multiverse_heroes.md` roster every other hero on this page draws from.
DragonsNShit (`GoblinFoxDragon/apps2/mud`)'s real 22-job system is being ported into Battlegrounds
as playable content — Warrior is the first, "the simplest real kit" per the northstar's own
Milestone 1 language. Appended to `ArenaHeroID` as the cheapest correct home for it until
Milestone 3's real job-select entry point exists; picking Warrior over a TYLER hero is a
presentation-layer problem that milestone still has to solve, not this one.

Three real Great Sword weapon skills from `server/skillchain.CanonicalWeaponSkills`, matching
WAR's real FFXI-archetype weapon per `server/job.jobStats[WAR]`'s STR-8/VIT-8 stat block (this
roster's most physically front-loaded job), in real FFXI progression order:

- **Q — Hard Slash** (Scission): the real starter Great Sword weapon skill, a plain melee-range
  hit, same shape as Gunnr's Q.
- **W — Power Slash** (Transfixion): the real mid-tier Great Sword weapon skill, hits harder than
  Hard Slash on a longer cooldown.
- **R — Frostbite** (Induration + Reverberation, dual resonance): the real Great Sword finisher
  weapon skill, the hardest of the three, longest cooldown.

`apps2/mud`'s weapon skills all share one real, uniform cost — `server/combat.TPWSThreshold`
(100 TP) via `TPState.UseWeaponSkill()`. REDGARDEN has no TP resource, so MP substitutes (this
file's own existing `ARENA_MP_COST_*` affordance) rather than a new TP bar being invented — an
honest amendment, not a literal port, per founder direction ("we want our old systems like
skillchains etc [to] work with redgarden affordances"). No passive, no status effect on any of
the three — real FFXI Great Sword weapon skills are plain damage, and this kit doesn't invent one
to pad it out.

**Milestone 2, same day:** real skillchain resonance detection now lands too. Casting a second
real weapon skill on the same target within 8 seconds of the first checks the real combination
table (`server/skillchain.combinationTable`, ported to C as `resonance_combo`) for a chain — Q
(Scission) into R (Induration+Reverberation) closes a real Tier 2 Distortion chain, +35% damage,
in-kit and testable with Warrior alone. Tracked per-target, not per-caster, matching real FFXI
("a chain forms on whoever gets hit twice, from any source"), so this works for teammates
weapon-skilling the same target too, not just self-chains.

---

### The Cart (TYLER `multiverse_heroes.md` #10, "The Retrieval Cart") — **Indirect-Control** (`NORTHSTAR.md` §24 Milestone 2, 2026-07-31)

Founder pick from §7's own hero queue — the one entry never built. Real lore constraint, not an
invented one: `multiverse_heroes.md`'s own 2026-07-23 gameplay note already named the design
problem precisely — "a requested document turns out to already be waiting on the cart, with no
requester logged," and "nobody, including its own controller, gets to request what." Asked
directly (this session, via a clarifying question) whether that should be honored or overridden
for a directly-commanded WC3-style kit: founder chose to honor it. **This is Indirect-Control**,
same archetype `NORTHSTAR.md` §16.1 already built for Donkey — not a directly-commanded unit, and
`NORTHSTAR.md` §24's own "give a hero real directly-controlled units" goal stays open after this.

- **Q — Maintenance**: a small self-heal. The Cart isn't a combatant per its own lore, so this
  stays deliberately minimal rather than padded out with an invented attack.
- **W — No Requester in the Ledger**: opens a delivery zone at the Cart's own position (no target
  needed). Whoever steps into it first — ally, enemy, or the Cart's own controller, no team check
  — triggers ONE of four real, equally-weighted outcomes: heal 25% max HP, restore 25% max MP,
  a 30% slow for 3s, or +50 Flow. Not always good, matching the lore directly. Single-use: the
  zone deactivates the instant it delivers.
- **R — Already Waiting**: the same mechanic, a bigger zone, on a longer cooldown — matching every
  other hero's own "R is the bigger version of the kit's theme" convention.

Real, honest interaction found while building this, not hidden: W and R share the same
zone-position/duration fields every other zone-ability hero already uses (Ghost's Recital,
Gunnr's Consecration, Vassago's silence zone) — the Cart is the first hero with TWO zone-shaped
abilities on that shared state, so casting one while the other's zone is still active replaces
it, last-cast-wins. No existing hero's kit could reach that interaction before the Cart.

---

## Starting Item Roster (VS0)

LoL Season 3 is the explicit stylistic northstar here — not its item *names*, its item *stat-line
archetypes* and the distinct build paths they enable (crit carry, on-hit carry, burst mage, DPS/
utility mage, tank initiator, tank/MR, lifesteal duelist, anti-tank penetration, support aura).
Mixed in, not copy-pasted 1:1.

| Item | Archetype (LoL/DOTA analogue) | Stat line |
|---|---|---|
| Seedling Charm | Cheap starter stat-stick (Doran's-line) | +AD or +AP (choose on purchase), +HP, minor HP5 |
| Bramble Fang | AD crit carry core (Infinity Edge-line) | +AD, +Crit Chance, +Crit Damage |
| Thornrender | On-hit AD carry (Blade of the Ruined King-line) | +AD, +AS, on-hit: bonus %-current-HP damage + slow |
| Bloomheart Core | Burst-AP mage core (Rabadon's-line) | Large +AP, % bonus to total AP |
| Wanecall Grimoire | DPS/utility mage (Athene's/Morello-line) | +AP, +CDR, applies Grievous Wounds (reduced healing) on ability hit |
| Ironbark Plate | Tank initiator (Sunfire/Randuin's-line) | +HP, +Armor, passive AoE damage aura, slows attackers |
| Willowveil | Tank/MR (Banshee's-line) | +MR, +HP, one-time incoming-spell block (recharges) |
| Vampiric Bloom | Lifesteal duelist (Bloodthirster-line) | +AD, +Lifesteal, overheal converts to a temporary shield |
| Splinterfang | Anti-tank physical penetration (Last Whisper-line) | +AD, % Armor Penetration |
| Hollow Needle | Anti-tank magic penetration (Void Staff-line) | +AP, % Magic Penetration |
| Rootrunner Treads | Mobility tier (Boots-line) | Flat +MS, minor situational bonus (choose on upgrade) |
| Gardener's Ward | Support aura (Locket/Redemption-line) | +HP, active: AoE shield + heal to allies, long cooldown |

---

## Open / Deferred

- No numeric balance pass — every number above is a placeholder to be tuned once these are wired
  into `packages/simulation/local_game.c`.
- No card-hand integration decided yet: whether heroes replace or sit alongside the existing
  4-card roster (Militia/Scout/Swarmlings/Outpost) is a separate design question.
- Remaining compendium entries beyond this queue are out of scope for this pass.
- **Built (2026-07-24, S170-47)**: the Morrigan/Druid relationship flagged in the previous version
  of this note is now realized — Druid merged into Flamel (see the Flamel entry above), and the
  Morrigan built alongside the Dagda, both hooking into the territory/node system so the
  rock-paper-scissors counter-play (Morrigan's war/death kit vs. Flamel's life/growth kit) is real,
  not just a design intention.
