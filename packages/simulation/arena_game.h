#ifndef ARENA_GAME_H
#define ARENA_GAME_H

#define ARENA_HALF_EXTENT 28.0f /* NORTHSTAR §8 jungle pass: was 20.0f -- more room for jungle terrain between spawn and the flank nodes without cramming it against the 1v1 mid-lane */
#define ARENA_HERO_SPEED 4.0f      /* units/sec */
#define ARENA_ATTACK_RANGE 1.6f
#define ARENA_ATTACK_DAMAGE 8
#define ARENA_ATTACK_COOLDOWN_MS 700
#define ARENA_NODE_COUNT 5 /* S170-119: was 2 -- real Arathi Basin has 5 (Stables/Farm/Blacksmith/Lumber Mill/Gold Mine) */

/* Static jungle terrain (NORTHSTAR §8, "add rocks and trees so we naturally
 * start to create some lanes"): rock/tree boxes -- same "boxes for now"
 * silhouette approach as the hero models below, not sculpted geometry.
 * Fixed layout, never mutated at runtime (no owner/HP/state), so unlike
 * nodes/creeps there's no wire sync -- apps/arena and apps/arena_server
 * both call arena_obstacles_reset_layout() from the same init path and end
 * up with identical local copies, one less thing for ArenaSnapshotMsg to
 * carry. Placed in two flank walls between each team's spawn column
 * (x=+-8) and the flank nodes (x=+-18, see arena_nodes_reset_layout),
 * spanning roughly z=-5.5..5.5 -- wide enough to block a straight line to
 * a flank node, but never touching the mid lane (heroes going straight to
 * the x=0 center node never cross x=+-9) or the 1v1 local demo's own
 * movement-test coordinates (spawn (-6,0)/(6,0), all test paths stay
 * within |x|<7). The result: reaching a flank node means routing around
 * the top or the bottom of the wall -- a top/bottom lane either side of a
 * jungle you can't walk through, same shape as the real MOBA reference
 * this map is already modeled on (NORTHSTAR §8's Arathi Basin comparison). */
typedef enum {
    ARENA_OBSTACLE_ROCK = 0,
    ARENA_OBSTACLE_TREE = 1,
} ArenaObstacleKind;
#define ARENA_OBSTACLE_COUNT 22
#define ARENA_HERO_COLLISION_RADIUS 0.6f /* how close a hero's own footprint can get to an obstacle's edge before being pushed back out */

/* Healing fountains (S170-147). Founder: "add healing fountains at 2
 * corners of the map across from each other." Two static, fixed-position
 * healing zones at diagonally-opposite map corners -- a real MOBA fountain
 * ("go here to top off") rather than a passive regen tick. Deliberately
 * NEUTRAL, not team-exclusive: the founder's own wording asked for "2
 * corners... across from each other" (a real map-geography placement), not
 * "one per team's base" (which real MOBA fountains usually are) -- read as
 * a genuinely contestable resource, matching this map's existing "structures
 * are neutral/contestable" pattern (nodes, jungle creeps) rather than
 * guessing which team owns which corner. Flagged here as a real design
 * choice, not silently assumed, in case the founder actually meant
 * team-exclusive home-base fountains -- easy to flip later (gate the heal
 * on `hero->team == fountain_owner_team` instead of healing anyone) if so.
 * Positions are deterministic and computed identically client- and
 * server-side (same "no wire sync needed for a static layout" precedent
 * jungle obstacles already established) -- only the resulting HP change
 * needs to reach the client, and it already does via the existing hero HP
 * sync + S170-143's generic heal-flash (fires on ANY HP increase, any
 * source). */
#define ARENA_FOUNTAIN_COUNT 2
#define ARENA_FOUNTAIN_RADIUS 3.0f
#define ARENA_FOUNTAIN_HEAL_PER_SEC 15 /* strong, deliberate -- "go here to top off," not a passive trickle */

/* Territorial dynamic jungle creeps (S170-51). Founder direction: territory
 * is the macro/economy layer, objectives (the team-wipe win condition) are
 * how the game is actually won, and gameplay should let territory control
 * shape what jungle creeps emerge -- "controlling the flavor and cadence of
 * the jungle helps create the meta to counter certain comps or play
 * styles." One creep per node, tied to that node's `owner`, re-rolled on
 * every respawn rather than fixed at map-init -- the jungle's own
 * population reacts to who currently controls the ground under it, not a
 * static camp table (matching the earlier NORTHSTAR §8 "alive and dynamic,
 * not static camps" direction, now built rather than just specified).
 * Numbers are the "beginner/danger-tier" spirit of GoblinFoxDragon's real
 * mob archetypes (server/mob/hills.go: passive-until-attacked low-HP vs. a
 * tougher, rarer high-value target), adapted rather than ported verbatim --
 * GFD's mobs carry a full aggro-cone/leash-range system this arena's
 * click-to-move model has no equivalent for; this keeps just the
 * difficulty-tiering idea. Two flavors, two different rewards, not just
 * two HP totals -- that's the actual "flavor" half of the ask:
 *   - A CONTESTED node's creep (owner==0) is the rare, tanky, slow-respawn
 *     prize -- killing it hands the killer's team a large one-time capture-
 *     progress bonus toward THAT node, a real tempo swing worth fighting
 *     over regardless of side.
 *   - An OWNED node's creep is common, weak, fast-respawning -- a small
 *     steady "home-turf resupply" heal when the OWNING team kills it (their
 *     own jungle sustains them), but a smaller capture-progress kick toward
 *     FLIPPING the node when the OPPOSING team kills it instead -- a real
 *     counter-play tool against a team that's turtled onto a lot of
 *     territory: you can whittle down their jungle advantage by farming it
 *     out from under them, not just by fighting them directly. */
typedef enum {
    ARENA_CREEP_NEUTRAL = 0, /* mirrors node owner 0 exactly -- see ArenaCreep.node_index */
    ARENA_CREEP_TEAM0 = 1,
    ARENA_CREEP_TEAM1 = 2,
} ArenaCreepFlavor;
#define ARENA_MAX_CREEPS ARENA_NODE_COUNT /* one creep per node, index-matched */
#define ARENA_CREEP_NEUTRAL_HP              80
#define ARENA_CREEP_TEAM_HP                 40
#define ARENA_CREEP_NEUTRAL_RESPAWN_MS       30000 /* the rare prize -- slow cadence */
#define ARENA_CREEP_TEAM_RESPAWN_MS          12000 /* home-turf resupply -- fast cadence, rewards holding ground */
#define ARENA_CREEP_AGGRO_RADIUS             4.0f  /* passive-until-approached, same spirit as GFD's Rabbit */
#define ARENA_CREEP_DAMAGE                   6
#define ARENA_CREEP_ATTACK_COOLDOWN_MS       1500
#define ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS 5000 /* big swing for winning the contested prize */
#define ARENA_CREEP_TEAM_KILL_HEAL                20   /* home-turf resupply, owning team only */
#define ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS 1500 /* counter-play: farming an enemy's own jungle creep helps flip their node */

/* Team-scale arena (2026-07-24, NORTHSTAR §13 cont'd): the array grows from
 * 2 to ARENA_MAX_HEROES so a full 10v10 match fits in the same ArenaState
 * the 1v1 local demo and apps/arena_server (1v1) already use. The 1v1 path
 * (arena_init/arena_init_with_heroes) still only ever populates heroes[0]/
 * [1] and leaves the rest zeroed/inactive -- see the `active` field below. */
#define ARENA_TEAM_SIZE 10
#define ARENA_MAX_HEROES (ARENA_TEAM_SIZE * 2)

/* ARENA_MAX_CLONE_SLOTS/ARENA_HEROES_ARRAY_SIZE (S170-141, Tyler's puppet
 * clones): a small pool of extra hero slots APPENDED after the real
 * per-player range (0..ARENA_MAX_HEROES-1, always exactly claimed by real
 * connected clients in every lobby size this codebase actually runs --
 * always either 2 or ARENA_MAX_HEROES, never partial) so a puppet clone
 * never competes with an actual connecting client for a slot. Real owner
 * indices (draft picks, PACKET_ARENA_PICK, the wire snapshot) never reach
 * into this range -- it exists purely for arena_state.heroes[]' own
 * simulation-side bookkeeping, not networking (ARENA_SNAPSHOT_MAX_HEROES in
 * protocol.h stays at ARENA_MAX_HEROES, unchanged; clones aren't wire-synced
 * yet, same "sim-only for now" precedent as jungle/lane creeps). Sized for
 * up to 4 simultaneous Tyler R casts' worth of clones -- generous headroom,
 * not a hard design target. */
#define ARENA_MAX_CLONE_SLOTS 8
#define ARENA_HEROES_ARRAY_SIZE (ARENA_MAX_HEROES + ARENA_MAX_CLONE_SLOTS)

/* Hero roster (docs/HEROES_VS0.md), NORTHSTAR §12 Phase D. hero_id
 * generalizes kit dispatch away from S170-18's "owner 0 == The Unicorn"
 * hardcoding -- either owner slot can carry either hero now. Growing this
 * list is the actual "full roster" work; ten more heroes from the doc are
 * follow-on passes, not this one (EMILY/BACKLOG.md S170-31). */
typedef enum {
    ARENA_HERO_UNICORN = 0,
    ARENA_HERO_DUCK = 1,
    ARENA_HERO_GHOST = 2,
    ARENA_HERO_FROG = 3,
    ARENA_HERO_DOC_WHEEL = 4,
    ARENA_HERO_TREE = 5,
    ARENA_HERO_PIZZA = 6,
    ARENA_HERO_FLAMEL = 7, /* merged with the former "Druid" archetype, 2026-07-24 -- see docs/HEROES_VS0.md */
    ARENA_HERO_MORRIGAN = 8,
    ARENA_HERO_DAGDA = 9,
    ARENA_HERO_COURIER = 10, /* Ratatoskr, TYLER multiverse_heroes.md #32 */
    ARENA_HERO_LOKI = 11, /* TYLER multiverse_heroes.md #37, "Loki, Who Isn't Here" (S170-79) */
    ARENA_HERO_GARY = 12, /* TYLER multiverse_heroes.md #35, "Gary, Bifrost Security (Off-Duty)" (S170-91) */
    ARENA_HERO_FLUTE_DEBT = 13, /* TYLER multiverse_heroes.md #42, "Han Xiangzi's Flute-Debt" (S170-91) */
    ARENA_HERO_BACON_PUCK = 14, /* TYLER multiverse_heroes.md #5 + #67, merged (S170-94) */
    ARENA_HERO_ABRAHAM = 15, /* TYLER multiverse_heroes.md #113, "Abraham of Worms, the Mage" (S170-103) */
    ARENA_HERO_ADA = 16, /* TYLER multiverse_heroes.md #112, "Ada Lovelace, Pilot" (S170-103) */
    ARENA_HERO_TYLER = 17, /* docs/HEROES_VS0.md's own pre-existing design, never implemented until now (S170-111) */
    ARENA_HERO_PAIMON = 18, /* TYLER multiverse_heroes.md #20, "Paimon, the Court Voice", channeled by the real John Dee (S170-55) */
    ARENA_HERO_NOOR1 = 19, /* TYLER multiverse_heroes.md #3, "NOOR-1 (Four Days Behind)", in-game form: a snowman (S170-104) */
    ARENA_HERO_CAIN = 20, /* TYLER multiverse_heroes.md #80, "Cain, East of Eden" (S170-105, founder: "replace adelle with Cain") */
    ARENA_HERO_GUNNR = 21, /* TYLER multiverse_heroes.md #30, "Gunnr, Who Argued With a Raven" (S170-93) */
    ARENA_HERO_VASSAGO = 22, /* TYLER multiverse_heroes.md #16, "Vassago, the Soft Foresight" (S170-93); also real TYLER canon, Goetia 11.11 Hz */
    ARENA_HERO_HE_XIANGU = 23, /* TYLER multiverse_heroes.md #39, "He Xiangu, Who Stopped Eating" (S170-93) */
    ARENA_HERO_BELETH = 24, /* TYLER multiverse_heroes.md #14, "Beleth, the Detonation" (S170-93) */
    ARENA_HERO_MNM = 25, /* TYLER multiverse_heroes.md #114, "MnM, the Shapeshifting Crab" (S170-134) */
} ArenaHeroID;
#define ARENA_HERO_COUNT 26

/* The Unicorn — first real hero kit wired in (S170-18). */
#define ARENA_UNICORN_ARMOR         4    /* passive: Chassis Claim, flat dmg reduction */
#define ARENA_UNICORN_Q_DASH_DIST   4.0f /* Diagnostic Charge */
#define ARENA_UNICORN_Q_DAMAGE      12
#define ARENA_UNICORN_Q_HIT_RADIUS  1.8f
#define ARENA_UNICORN_Q_COOLDOWN_MS 4000
#define ARENA_UNICORN_W_REGEN_PER_SEC 6  /* Spaghetti Vent, while toggled on */
#define ARENA_UNICORN_R_COOLDOWN_MS 15000
#define ARENA_UNICORN_R_DURATION_MS 3000 /* Full Disclosure: armor doubled */

/* The Duck — second hero kit (S170-31). Q/R only: W (Government Clearance)
 * needs towers/objective structures that don't exist in this 1v1 arena, and
 * E (Chosen One) triggers on a killing blow, but arena's win condition ends
 * the match on that same kill -- the buff window and match-end coincide, so
 * it would have zero observable effect here. Both skipped, not faked. */
#define ARENA_DUCK_Q_PULL_DIST      5.0f /* Telekinetic Yank: how far the foe gets pulled */
#define ARENA_DUCK_Q_DAMAGE         10
#define ARENA_DUCK_Q_RANGE          6.0f /* max distance the yank can reach */
#define ARENA_DUCK_Q_COOLDOWN_MS    5000
#define ARENA_DUCK_R_PULL_DIST      9.0f /* Total Telekinesis: bigger yank */
#define ARENA_DUCK_R_DAMAGE         20
#define ARENA_DUCK_R_RANGE          9.0f
#define ARENA_DUCK_R_COOLDOWN_MS    18000

/* The Ghost — third hero kit (S170-32). First kit needing real status-effect
 * state (silence, intangibility) rather than just cooldowns/toggles. R's
 * ally-heal side (docs/HEROES_VS0.md: "same zone, opposite effect depending
 * on team") has no target in a 1v1 -- only the enemy-damage side is
 * implemented, flagged not faked. Passive (Mid-Piano, silent undodgeable
 * casts) is a cast-animation/UI concept with no gameplay effect to model in
 * this arena -- skipped, flagged, same reasoning as other UI-only passives. */
#define ARENA_GHOST_Q_RANGE         7.0f  /* Alien Frequency: skillshot range */
#define ARENA_GHOST_Q_DAMAGE        9
#define ARENA_GHOST_Q_SILENCE_MS    1500
#define ARENA_GHOST_Q_COOLDOWN_MS   4500
/* S170-140: Alien Frequency is explicitly documented as a skillshot
 * (docs/HEROES_VS0.md) but was still an instant hit-if-in-range check --
 * the second real travelling projectile in this arena, same convention as
 * Gary's Q (S170-136). Faster than Gary's shot (a "frequency," not a bullet)
 * -- reads as a quick zap rather than a slow, dodgeable sniper round. */
#define ARENA_GHOST_Q_PROJECTILE_SPEED  18.0f
#define ARENA_GHOST_Q_PROJECTILE_RADIUS 0.5f
#define ARENA_GHOST_W_INTANGIBLE_MS 1500 /* Not a Ghost */
#define ARENA_GHOST_W_COOLDOWN_MS   10000
#define ARENA_GHOST_R_RADIUS        4.0f  /* Recital: zone stays fixed where cast */
#define ARENA_GHOST_R_DURATION_MS   4000
#define ARENA_GHOST_R_DPS           6     /* damage/sec to enemies standing in the zone */
#define ARENA_GHOST_R_COOLDOWN_MS   20000

/* The Frog — fourth hero kit (S170-33), the last clean-fit pick from
 * S170-32's roster audit at the time (before allies existed, S170-45 below).
 * R (The Secret) is simplified to reuse Ghost's intangible_ms mechanic at a
 * longer duration; "reappear at any visited location" needs its own
 * location-memory system, deferred, not faked as the full ability. Passive
 * (Never Told Anyone, no visible cooldown UI for enemies) is a bluffing/UI
 * concept -- arena has no separate enemy-facing view to hide anything from,
 * skipped, flagged.
 * W (Borrowed Time) was originally skipped for having no ally target in
 * 1v1 -- wired for real (S170-45) now that arena_nearest_ally exists. Uses
 * the generic next_cast_refund buff field, same mechanism any future
 * ally-buff kit would reuse. */
#define ARENA_FROG_LOOPBACK_SAMPLE_MS 250 /* Q — Loop Back: how often position/HP is sampled */
#define ARENA_FROG_LOOPBACK_SLOTS     16  /* 16 * 250ms = 4000ms of history, enough to rewind 3s */
#define ARENA_FROG_Q_REWIND_MS      3000
#define ARENA_FROG_Q_COOLDOWN_MS    8000
#define ARENA_FROG_R_VANISH_MS      5000  /* The Secret, simplified (see comment above) */
#define ARENA_FROG_R_COOLDOWN_MS    25000
#define ARENA_FROG_W_COOLDOWN_MS    12000 /* Borrowed Time: places the refund buff on an ally */

/* Doc Wheel (Buer) — fifth hero kit (S170-45), the first ally-targeted-only
 * kit ("the entire kit is being the correct ally to have nearby" per
 * docs/HEROES_VS0.md) and the reason arena_nearest_ally exists at all. The
 * RED GARDEN passive (CORRUPTED-cell decay on heal) is skipped -- arena has
 * no GridCell/territory system (same blocker as Tree/Pizza/Druid, S170-32's
 * audit). R ("No Combat Power, As Advertised" -- teamwide debuff-cleanse +
 * shield) is simplified to teamwide cleanse + heal, not a literal absorb-
 * shield -- shields would be a new generic damage-absorption mechanic
 * touching every damage call site in this file for a single ability's
 * sake; deferred rather than built shallow, same reasoning as other
 * simplified (not faked) pieces elsewhere in this roster. */
#define ARENA_DOC_WHEEL_Q_HEAL_BASE   14   /* Bedside Manner: heal at 100% target HP */
#define ARENA_DOC_WHEEL_Q_HEAL_LOW_HP 28   /* heal amount at ~0% target HP -- passive scaling */
#define ARENA_DOC_WHEEL_Q_COOLDOWN_MS 3500
#define ARENA_DOC_WHEEL_W_COOLDOWN_MS 16000 /* House Call: teleport to ally's location */
#define ARENA_DOC_WHEEL_R_RADIUS      6.0f
#define ARENA_DOC_WHEEL_R_HEAL        20   /* teamwide heal (R, simplified from a shield) */
#define ARENA_DOC_WHEEL_R_COOLDOWN_MS 30000

/* Territory / node system (S170-46, NORTHSTAR §13 cont'd; redesigned
 * S170-50). The founder's original "territory/resource economy" pick over
 * allies-scaling or non-piloted units, then explicitly redirected away from
 * ambient presence-math toward a real Arathi Basin-style flag: "true click
 * to channel capture, interruptable, a neutral period after the flag flips
 * as you wait for it to finish capturing -- adds objective-focused play and
 * the possibility of losing due to ignoring the objective, not just
 * presence-based." The old model (signed `pressure` drifting toward
 * whichever team had more weighted bodies nearby, owner derived from a
 * threshold) is gone entirely, not layered under this -- it was exactly
 * the "just presence based" thing being moved away from.
 *
 * New model: exactly one team can be channeling a node at a time.
 *   - Exclusive presence (only team A's living heroes in radius, zero from
 *     team B) starts or continues team A's channel.
 *   - The instant a channel starts against a node NOT already owned by the
 *     channeling team, the node flips to neutral (owner=0) immediately --
 *     this is the "neutral period... as you wait for it to finish
 *     capturing": the node sits open, genuinely uncaptured, for the whole
 *     channel duration, not just at the end.
 *   - Mixed presence (both teams in radius) or the channeling team fully
 *     leaving interrupts the channel: progress resets to 0, capturing_team
 *     clears. The node does NOT revert to its pre-channel owner -- a
 *     defender who interrupts an attacker still has to walk over and start
 *     their own channel to reclaim it. This is the actual teeth behind
 *     "the possibility of losing due to ignoring the objective": leaving a
 *     flag undefended costs it the instant the enemy commits, and even a
 *     successful defense doesn't hand it back for free.
 *   - Reaching ARENA_NODE_CAPTURE_CHANNEL_MS flips owner to the channeling
 *     team and clears the channel state.
 *
 * This is the enabling system for Tree (Root Network), Pizza (corruption),
 * and Flamel (Overgrowth marking, absorbed from the former Druid) -- the
 * three heroes S170-32's roster audit flagged as blocked on exactly this;
 * all three are redesigned below to hook into the channel instead of the
 * retired pressure-drift. */
#define ARENA_NODE_CAPTURE_RADIUS        5.0f
#define ARENA_NODE_CAPTURE_CHANNEL_MS    8000  /* base channel duration, no bonuses -- Arathi Basin's own real cap timer is in this ballpark */
#define ARENA_TREE_CHANNEL_SPEED_MULT    2.0f  /* Root Network: a Tree among the channeling team's present heroes doubles progress this tick */
#define ARENA_FLAMEL_MARK_MS             6000  /* Overgrowth: how long a mark persists once Flamel leaves */
#define ARENA_FLAMEL_MARK_CHANNEL_BONUS_MS 200 /* extra channel progress per tick while capturing on ground the capturing team has marked -- deterministic simplification of the doc's "increased chance of converting," flagged */

/* Tree — sixth hero kit (S170-46). Passive (Root Network) needs no ability
 * code at all -- arena_tick_nodes reads hero_id directly and applies
 * ARENA_TREE_CHANNEL_SPEED_MULT. Q (Vine Lash) simplifies "AoE root in a
 * cone in front" to an instant hit-if-in-range check, same precedent as
 * Ghost's Alien Frequency. W (Untranslated, ally CC-immunity) is
 * unbuildable -- arena's own ability casts are all instant, nothing to
 * interrupt there -- skipped, flagged, same reasoning as other
 * mechanic-less passives (the node *capture* channel added by S170-50 is a
 * map-objective mechanic, a different thing from an ability-cast channel).
 * R (Grand Secret) simplifies "roots permanently until recast, min 8s" to a
 * fixed-duration self-root + armor buff, same "fixed duration" simplification
 * already used for Frog's R and Ghost's R zone. */
#define ARENA_TREE_Q_RANGE         6.0f
#define ARENA_TREE_Q_DAMAGE        10
#define ARENA_TREE_Q_ROOT_MS       1500
#define ARENA_TREE_Q_COOLDOWN_MS   5000
#define ARENA_TREE_R_ROOT_MS       8000  /* Grand Secret: self-root, min 8s per the doc */
#define ARENA_TREE_R_ARMOR_BONUS   8
#define ARENA_TREE_R_HEAL          30
#define ARENA_TREE_R_COOLDOWN_MS   25000

/* Pizza — seventh hero kit (S170-46, corruption redesigned S170-50). Passive
 * (Uninvestigated Fire) is an always-on burn aura (AP-scaling simplified to
 * flat DPS, same precedent as Ghost's flat R_DPS) plus a corruption effect
 * on the channel-capture mechanic, handled generically in arena_tick_nodes:
 * Pizza's mere presence in radius forces any in-progress channel on that
 * node to interrupt, regardless of which team she's on or whether her
 * presence would otherwise count as "exclusive" -- corruption doesn't care
 * whose side you're on, a direct carry-over of the same "regardless of team
 * composition" framing from the old pressure model. Q (Nobody Checked)
 * simplifies "throw a burning slice + ground patch" to direct damage + a
 * burn DoT applied straight to the foe -- no persistent ground-hazard
 * system exists, so the lingering-patch half is dropped, not faked. W (I Am
 * The Chosen One) is pure-visual, zero mechanical effect per the doc
 * itself -- skipped, flagged, same reasoning as Duck's W and Ghost's
 * passive. R (Nobody Ever Checks) is built for real: a damage floor status
 * effect, the one piece of this roster's simplifications that needed
 * apply_damage() centralized rather than shortcut. */
#define ARENA_PIZZA_AURA_RADIUS    3.5f
#define ARENA_PIZZA_AURA_DPS       4
#define ARENA_PIZZA_Q_RANGE        6.0f
#define ARENA_PIZZA_Q_DAMAGE       8
#define ARENA_PIZZA_Q_BURN_MS      3000
#define ARENA_PIZZA_Q_BURN_DPS     5
#define ARENA_PIZZA_Q_COOLDOWN_MS  4500
#define ARENA_PIZZA_R_FLOOR_MS     4000
#define ARENA_PIZZA_R_COOLDOWN_MS  28000

/* Flamel — eighth hero kit (S170-46), merged with the former "Druid" archetype
 * per founder direction ("druid and flamel should be the same hero") --
 * docs/HEROES_VS0.md carries the full merge rationale. Passive (Great Work +
 * Overgrowth) needs no ability code for the marking half (arena_tick_nodes
 * reads hero_id directly, same as Tree); the cooking-bonus half is out of
 * scope this pass (docs/CONSUMABLES_AND_COOKING.md isn't wired to any hero
 * kit yet) -- skipped, flagged, not faked. Q (Vine Growth) simplifies "wall
 * of vines in a line" to an instant root-if-in-range check on the nearest
 * enemy, same cone/line-to-single-target-range simplification as Tree's Q.
 * W (Philosopher's Bloom) merges Bloom + Philosopher's Batch into one AoE
 * ally heal with a marked-node bonus. R (Elixir of Wild Growth) merges
 * Elixir's team-ultimate framing with Wild Growth's AoE shape: a fixed
 * zone (reusing Ghost's r_zone_x/z/tick_ms fields) that roots enemies and
 * heals allies each tick for its duration, plus a one-time mass-mark of
 * nodes in radius at cast time. The "heavy slow" from the doc is simplified
 * to a full root -- no per-hero movement-speed-multiplier system exists in
 * this arena yet, flagged. */
#define ARENA_FLAMEL_Q_RANGE         5.5f
#define ARENA_FLAMEL_Q_ROOT_MS       1500
#define ARENA_FLAMEL_Q_COOLDOWN_MS   5500
#define ARENA_FLAMEL_W_RADIUS        4.5f
#define ARENA_FLAMEL_W_HEAL_BASE     10
#define ARENA_FLAMEL_W_HEAL_MARKED   18  /* Philosopher's Bloom: more healing cast on Flamel's own marked ground */
#define ARENA_FLAMEL_W_COOLDOWN_MS   9000
#define ARENA_FLAMEL_R_RADIUS        5.0f
#define ARENA_FLAMEL_R_DURATION_MS   4000
#define ARENA_FLAMEL_R_ROOT_MS       1200 /* refreshed each 1000ms tick an enemy stays in the zone */
#define ARENA_FLAMEL_R_HEAL_PER_TICK 8
#define ARENA_FLAMEL_R_COOLDOWN_MS   32000

/* Morrigan — ninth hero kit (S170-47, TYLER multiverse_heroes.md #68). A
 * war/death goddess whose whole hook (per the doc's own "flagged, not
 * built" note in HEROES_VS0.md) is rock-paper-scissors counter-play
 * against Flamel's life/growth kit -- founder direction calls her a "meta
 * jungler." No standalone jungle-camp system exists in this arena, so her
 * jungler identity is expressed the same way Tree/Pizza/Flamel's territory
 * hooks are: tied to the ArenaNode contest that already exists, rather than
 * inventing a second system. Passive rewards standing in neutral/contested
 * ground (a war goddess belongs to the unresolved fight, not settled
 * territory). Q and R both scale up against a low-HP target -- "the crow
 * confirms the kill," matching the lore's death-omen framing, and mirroring
 * (inverted) Doc Wheel's heal-more-when-hurt math. W (the eel/wolf/heifer
 * animal-form harassment scene) is a sudden gap-close + root onto the
 * nearest enemy -- "she appears where he doesn't expect." */
#define ARENA_MORRIGAN_PASSIVE_ARMOR_BONUS 4   /* Contested Ground: bonus armor while standing on a contested (owner==0) node */
#define ARENA_MORRIGAN_Q_RANGE          6.0f
#define ARENA_MORRIGAN_Q_DAMAGE_BASE    8      /* The Washer's Strike, at 100% target HP */
#define ARENA_MORRIGAN_Q_DAMAGE_LOW_HP  18     /* at ~0% target HP -- an execute, damage scales up as the target dies */
#define ARENA_MORRIGAN_Q_COOLDOWN_MS    4000
#define ARENA_MORRIGAN_W_ROOT_MS        1200   /* Three Forms: gap-close + root on arrival */
#define ARENA_MORRIGAN_W_COOLDOWN_MS    7000
#define ARENA_MORRIGAN_R_RADIUS         4.5f
#define ARENA_MORRIGAN_R_DURATION_MS    3500
#define ARENA_MORRIGAN_R_DAMAGE_BASE    4      /* The Crow Confirms It: per-tick execute DPS, at 100% target HP */
#define ARENA_MORRIGAN_R_DAMAGE_LOW_HP  12     /* per-tick DPS at ~0% target HP */
#define ARENA_MORRIGAN_R_COOLDOWN_MS    24000

/* Dagda — tenth hero kit (S170-47, TYLER multiverse_heroes.md #69). "The
 * wheeled club settles every argument twice" -- one end kills, the other
 * revives, same tool, depending only on which end swings first. Built
 * literally: Q checks what's in range and picks the end. The cauldron
 * (Undry, "never runs empty") is a passive sustain regen. The harp
 * (Uaithne's three master strains, sorrow/joy/sleep, played over an entire
 * hall in one go) is one AoE cast hitting everyone in range at once --
 * enemies get sorrow+sleep (root+silence), allies get joy (heal). The
 * force-fed porridge scene ("eats every bite, unhurt, fights the next day
 * regardless") is a damage floor + a real heal, not just survival --
 * enduring AND coming out ahead. */
#define ARENA_DAGDA_PASSIVE_REGEN_PER_SEC 3   /* The Undry: passive self HP regen, always on */
#define ARENA_DAGDA_Q_RANGE           5.5f
#define ARENA_DAGDA_Q_KILL_DAMAGE     16      /* the killing end of the club */
#define ARENA_DAGDA_Q_REVIVE_HEAL     16      /* the reviving end, simplified to a heal -- no respawn system exists to revive into */
#define ARENA_DAGDA_Q_COOLDOWN_MS     5000
#define ARENA_DAGDA_W_RADIUS          4.5f
#define ARENA_DAGDA_W_ROOT_MS         1200    /* sorrow */
#define ARENA_DAGDA_W_SILENCE_MS      1200    /* sleep */
#define ARENA_DAGDA_W_ALLY_HEAL       10      /* joy */
#define ARENA_DAGDA_W_COOLDOWN_MS     11000
#define ARENA_DAGDA_R_FLOOR_MS        3000    /* the porridge: a real damage floor */
#define ARENA_DAGDA_R_HEAL            30      /* ...and still comes out ahead, not just surviving */
#define ARENA_DAGDA_R_COOLDOWN_MS     26000

/* The Courier — eleventh hero kit (S170-48, TYLER multiverse_heroes.md #32,
 * "Ratatoskr's Debt-Collector"). Runs constantly between two fixed points
 * (the eagle at Yggdrasil's crown, Nidhogg at its root) -- maps directly
 * onto this arena's two existing ArenaNode positions rather than needing a
 * new system. Passive cleanses The Courier's own debuffs on a landed Q hit
 * ("editing the message" addressed back to him). Q is a dash-strike, same
 * shape as Unicorn's Diagnostic Charge. W is a pure fixed-geography
 * teleport (distinct from every other hero's ally/foe-relative teleports --
 * this one always jumps to whichever node is farther away, "making
 * progress along the tree"). R is a flat single-target life-drain execute
 * ("started to involve judgment" -- taking a cut by force). */
#define ARENA_COURIER_Q_DASH_DIST   5.0f
#define ARENA_COURIER_Q_DAMAGE      10
#define ARENA_COURIER_Q_HIT_RADIUS  1.8f
#define ARENA_COURIER_Q_COOLDOWN_MS 4500
#define ARENA_COURIER_W_COOLDOWN_MS 9000
#define ARENA_COURIER_R_RANGE       6.0f
#define ARENA_COURIER_R_DRAIN       18
#define ARENA_COURIER_R_COOLDOWN_MS 20000

/* Loki, Who Isn't Here (S170-79, TYLER multiverse_heroes.md #37) -- a hero
 * defined by absence, so his kit works through repositioning and endurance
 * rather than a straightforward stat-check. Q is an instant swap with the
 * nearest enemy (no travel time, no dash arc -- he's just suddenly where the
 * enemy was, which is what "present only as interference on adjacent
 * readings" means mechanically) plus a small hit on arrival. W is a toggled
 * flat armor bonus ("bound where the myth says," a defensive stance, not
 * regen -- distinct from Unicorn's toggle). R borrows the same
 * survive_floor_ms mechanic Pizza/Dagda already use, cast on himself: "the
 * bowl does not need to be believed to be held" (Sigyn, #34) -- someone else
 * holding the outcome open for him for a fixed window, same as the myth. */
#define ARENA_LOKI_Q_DAMAGE         10
#define ARENA_LOKI_Q_HIT_RADIUS     2.0f
#define ARENA_LOKI_Q_COOLDOWN_MS    5000
#define ARENA_LOKI_W_ARMOR_BONUS    5 /* free toggle, no cooldown -- same convention as Unicorn's W */
#define ARENA_LOKI_R_FLOOR_MS       3500
#define ARENA_LOKI_R_COOLDOWN_MS    24000

/* Gary, Bifrost Security (Off-Duty) (S170-91, TYLER multiverse_heroes.md #35) -- pure
 * marksman, no magic, "extraordinary eyesight, extraordinary aim." Q is a stationary
 * long-range precision shot (no dash, no movement -- Gary doesn't chase, he watches).
 * W is a free toggle that extends Q's own range rather than granting a stat like every
 * other toggle so far ("watching the bridge" further out, a genuinely different toggle
 * shape). R is a fixed-duration root on the nearest enemy -- "slow down, this isn't a
 * track meet," simplified to a full stop the same way Tree's R/Flamel's R already
 * simplify a slow down to a root rather than adding a real speed-multiplier system. */
#define ARENA_GARY_Q_RANGE          6.0f
#define ARENA_GARY_Q_RANGE_WATCHING 9.0f /* Q's range while W is toggled on */
#define ARENA_GARY_Q_DAMAGE         11
#define ARENA_GARY_Q_COOLDOWN_MS    3500
/* S170-136: Q is now a real travelling projectile (first one in the game),
 * not an instant hit -- fired straight at the foe's position at cast time,
 * no homing, so a foe that moves off the line after the shot is fired
 * genuinely dodges it. Speed is fast enough to still read as "precision
 * shot" but slow enough (relative to ARENA_HERO_SPEED) that sidestepping is
 * a real, learnable counterplay: at 14 u/s over up to 9 units of range, the
 * shot is in the air for up to ~0.64s, in which a hero moving at 4 u/s can
 * shift ~2.5 units off the original line. */
#define ARENA_GARY_Q_PROJECTILE_SPEED  14.0f
#define ARENA_GARY_Q_PROJECTILE_RADIUS 0.6f
#define ARENA_GARY_R_RANGE          6.0f
#define ARENA_GARY_R_ROOT_MS        2000
#define ARENA_GARY_R_COOLDOWN_MS    16000

/* Han Xiangzi's Flute-Debt (S170-91, TYLER multiverse_heroes.md #42) -- "owes something to
 * every wrong note ever played near him, and eventually collects." Q applies a real debt:
 * modest damage plus the shared burning_ms/burn_dps DoT fields (S170-46), standing in for
 * the wrong note accruing. W is a free toggle self-heal-over-time ("recouping interest"
 * passively, even outside a fight -- reuses the same toggle-regen shape as Unicorn's W,
 * distinct role). R is the actual payoff, "eventually collects": bonus damage against a
 * target that still has the Q debt active when R lands, base damage otherwise -- always
 * commits and consumes the cooldown either way (same "always lands" convention as Doc
 * Wheel's/Flamel's R), the debt just decides how much it collects. */
#define ARENA_FLUTE_DEBT_Q_DAMAGE      6
#define ARENA_FLUTE_DEBT_Q_HIT_RADIUS  1.8f
#define ARENA_FLUTE_DEBT_Q_BURN_DPS    4
#define ARENA_FLUTE_DEBT_Q_BURN_MS     4000
#define ARENA_FLUTE_DEBT_Q_COOLDOWN_MS 3800
#define ARENA_FLUTE_DEBT_W_REGEN_PER_SEC 3
#define ARENA_FLUTE_DEBT_R_RANGE        5.5f
#define ARENA_FLUTE_DEBT_R_DAMAGE_BASE  8
#define ARENA_FLUTE_DEBT_R_DAMAGE_DEBT  22 /* dealt instead of BASE if the target's debt (burning_ms) is still active */
#define ARENA_FLUTE_DEBT_R_COOLDOWN_MS  18000

/* TYLER (S170-111) -- docs/HEROES_VS0.md already wrote this kit as "an exact copy of Meepo's
 * classic kit... reskinned as TYLER," including the original OG clone-death rule: every clone
 * shares one pool of fate, one dies, all die. That's not buildable as written on this engine
 * -- ArenaHero slots are one-per-connected-client, not multi-entity-per-player, and adding real
 * clone spawning would mean touching the draft/pick/connection model this whole roster depends
 * on. Simplified, documented here rather than silently narrowed the way every other "doesn't
 * fit this engine" gap in this roster already is (Frog's R, Tree's R, Courier's cleansed-debuff
 * passive): Q keeps Earthbind's root+setup role, W keeps Poof's blink-and-strike shape, E's
 * "geostrike on every melee attack" folds into Q's DoT since there's no generic per-attack
 * status hook to hang a real passive off, and R keeps the actual point of "Divided We Stand" --
 * real risk/reward -- as a self-buff that hits harder while making Tyler take more damage for
 * its duration (his own armor goes negative), rather than literal clones sharing literal HP. */
#define ARENA_TYLER_Q_DAMAGE          8
#define ARENA_TYLER_Q_RANGE           4.5f
#define ARENA_TYLER_Q_ROOT_MS         1600
#define ARENA_TYLER_Q_BURN_DPS        3
#define ARENA_TYLER_Q_BURN_MS         3500
#define ARENA_TYLER_Q_COOLDOWN_MS     4200
/* S170-140: Earthbind's own original-design wording ("Fires a net at a
 * target area," docs/HEROES_VS0.md) is a real thrown-object skillshot, not
 * an instant hit -- the third real travelling projectile in this arena.
 * Slower than Ghost's zap (a thrown net, not a beam) -- the root+burn payoff
 * is real counterplay-able, matching the same "dodgeable, not guaranteed"
 * bar Gary's Q set. */
#define ARENA_TYLER_Q_PROJECTILE_SPEED  10.0f
#define ARENA_TYLER_Q_PROJECTILE_RADIUS 0.7f
#define ARENA_TYLER_W_DAMAGE          12
#define ARENA_TYLER_W_HIT_RADIUS      1.8f
#define ARENA_TYLER_W_COOLDOWN_MS     5500
#define ARENA_TYLER_R_DAMAGE          16
#define ARENA_TYLER_R_RANGE           4.0f
#define ARENA_TYLER_R_VULNERABLE_MS   3500 /* r_active_ms window: Tyler's own armor goes negative for this long */
#define ARENA_TYLER_R_NEGATIVE_ARMOR  6.0f
#define ARENA_TYLER_R_COOLDOWN_MS     19000
/* S170-141: real puppet clones, on top of the existing self-buff -- see
 * docs/HEROES_VS0.md's Tyler section for the full design/scope note.
 * ARENA_TYLER_R_CLONE_COUNT is a deliberate simplification of the OG kit's
 * "up to 5" (Divided We Stand can be cast more than once in the original;
 * this arena's R is a single-cast-per-cooldown ability like every other R,
 * so a fixed, modest count per cast reads better than trying to replicate
 * stacking casts). ARENA_TYLER_CLONE_HP_PCT matches the OG kit's "each with
 * a percentage of TYLER's stats." */
#define ARENA_TYLER_R_CLONE_COUNT     2
#define ARENA_TYLER_CLONE_HP_PCT      0.5f

/* Bacon+Puck, merged (S170-94, TYLER multiverse_heroes.md #5 + #67) -- Bacon's whole
 * character is withholding ("custodian of the one location nobody's allowed to know yet,"
 * seed phrase "ask again later"); Puck's is an unresolved duality between two versions of
 * himself nobody can confirm is the real one. Combined kit: Q is a real "ask again later" --
 * self intangible_ms, the shared can't-be-hit status effect (S170-32) -- and W (Puck's
 * duality) is a free toggle that extends how long the secret stays withheld, i.e. Q's own
 * intangibility duration, rather than granting a stat like most toggles. R pays off the
 * mischief: real damage plus a self-heal off it, "the trick was always the same" either way. */
#define ARENA_BACON_PUCK_Q_INTANGIBLE_MS          1500
#define ARENA_BACON_PUCK_Q_INTANGIBLE_MS_WATCHING 3000 /* Q's intangible duration while W is toggled on */
#define ARENA_BACON_PUCK_Q_COOLDOWN_MS             6000
#define ARENA_BACON_PUCK_R_RANGE                   2.2f
#define ARENA_BACON_PUCK_R_DAMAGE                  16
#define ARENA_BACON_PUCK_R_HEAL_PCT                0.5f /* fraction of R's damage returned as self-heal */
#define ARENA_BACON_PUCK_R_COOLDOWN_MS              15000

/* Abraham of Worms, the Mage (S170-103, TYLER multiverse_heroes.md #113) -- a caster whose
 * whole real-world hook is a book whose ritual made a real man (Crowley) organize a life
 * around it. Q is a real ranged magic bolt, stronger while W (channeling the book) is
 * toggled on -- the first toggle in this roster that boosts a Q's damage rather than its
 * range/duration or granting armor/regen. R is "the Guardian Angel, contacted": a full
 * self-cleanse (every debuff field this roster tracks) plus a real heal, the ritual's
 * actual real-world promised payoff. */
#define ARENA_ABRAHAM_Q_DAMAGE            9
#define ARENA_ABRAHAM_Q_DAMAGE_CHANNELING 15 /* Q's damage while W is toggled on */
#define ARENA_ABRAHAM_Q_RANGE             5.5f
#define ARENA_ABRAHAM_Q_COOLDOWN_MS       3200
#define ARENA_ABRAHAM_R_HEAL              20
#define ARENA_ABRAHAM_R_COOLDOWN_MS       17000

/* Ada Lovelace, Pilot (S170-103, TYLER multiverse_heroes.md #112) -- "wrote the operating
 * logic for a frame before the frame existed." A heavy, deliberate tank/controller: Q
 * computes the nearest enemy's movement to a halt (a real root, matching Tree's/Flamel's
 * "slow simplified to a stop" convention), W is a free-toggle armor bonus (the frame's own
 * plating, same shape as Loki's but a different hero's reason for it), R is the engine
 * finally executing: a real burst of damage plus a short follow-up root, "the first
 * program, run a century late." */
#define ARENA_ADA_Q_RANGE          5.0f
#define ARENA_ADA_Q_ROOT_MS        1800
#define ARENA_ADA_Q_COOLDOWN_MS    6000
#define ARENA_ADA_W_ARMOR_BONUS    6
#define ARENA_ADA_R_RANGE          2.5f
#define ARENA_ADA_R_DAMAGE         18
#define ARENA_ADA_R_ROOT_MS        1200
#define ARENA_ADA_R_COOLDOWN_MS    16000

/* Paimon (channeled by John Dee) -- nineteenth hero kit (S170-121, docs/HEROES_VS0.md). Passive
 * (Keeping the Peace) is an always-on silence aura, same aura_tick_ms pattern as Pizza's burn
 * (S170-46). Q (Teaches All Arts) is an instant-hit-if-in-range damage+root, same simplification
 * as Ghost/Tree/Flamel's Q. W (Speaks With Total Authority) is an instant damage+silence decree,
 * same shape as Ghost's Q but on the W slot with its own cooldown. R (Two Hundred Legions) is a
 * fixed zone dealing periodic damage to enemies and healing allies, same shape as Ghost's
 * Recital/Flamel's Elixir of Wild Growth. */
#define ARENA_PAIMON_PASSIVE_AURA_RADIUS   3.5f
#define ARENA_PAIMON_PASSIVE_SILENCE_MS    800
#define ARENA_PAIMON_PASSIVE_INTERVAL_MS  4000 /* "periodically," not every tick like Pizza's DPS aura -- talking a fight down takes longer than burning */
#define ARENA_PAIMON_Q_RANGE                5.5f
#define ARENA_PAIMON_Q_DAMAGE               9
#define ARENA_PAIMON_Q_ROOT_MS              1400
#define ARENA_PAIMON_Q_COOLDOWN_MS          4500
#define ARENA_PAIMON_W_RANGE                6.0f
#define ARENA_PAIMON_W_DAMAGE                7
#define ARENA_PAIMON_W_SILENCE_MS           1800
#define ARENA_PAIMON_W_COOLDOWN_MS          8000
#define ARENA_PAIMON_R_RADIUS                4.5f
#define ARENA_PAIMON_R_DURATION_MS          4000
#define ARENA_PAIMON_R_DPS                    6
#define ARENA_PAIMON_R_HEAL_PER_TICK          6
#define ARENA_PAIMON_R_COOLDOWN_MS         26000

/* NOOR-1 (S170-104, "add NOOR-1 as a snowman"): passive periodic-silence aura (same idiom as
 * Pizza's/Paimon's), Q a ranged damage+root bolt, W a self-cast intangibility on its own
 * cooldown (same mechanic as Ghost's Not a Ghost, themed as "sent in clean" -- going quiet and
 * unreadable herself), R a fixed cold zone dealing periodic damage to enemies, same shape as
 * Ghost's Recital/Paimon's Two Hundred Legions but with no ally-heal side -- "do not approach"
 * is a one-sided instruction. */
#define ARENA_NOOR1_PASSIVE_AURA_RADIUS    3.5f
#define ARENA_NOOR1_PASSIVE_SILENCE_MS      700
#define ARENA_NOOR1_PASSIVE_INTERVAL_MS    4000
#define ARENA_NOOR1_Q_RANGE                  6.0f
#define ARENA_NOOR1_Q_DAMAGE                 8
#define ARENA_NOOR1_Q_ROOT_MS              1300
#define ARENA_NOOR1_Q_COOLDOWN_MS          4500
#define ARENA_NOOR1_W_INTANGIBLE_MS        1500
#define ARENA_NOOR1_W_COOLDOWN_MS         10000
#define ARENA_NOOR1_R_RADIUS                  4.0f
#define ARENA_NOOR1_R_DURATION_MS          4000
#define ARENA_NOOR1_R_DPS                     7
#define ARENA_NOOR1_R_COOLDOWN_MS         24000

/* Cain (S170-105, "replace adelle with Cain"): passive flat armor bonus, always on -- "the man
 * cast out to wander settled down and built civilization anyway," the one thing about him that's
 * permanent (same "always-on flat armor" shape as Unicorn's own passive, minus the R-doubling).
 * Q an execute-scaled damage bolt ("the first murder," same shape as Morrigan's Q -- a killing
 * blow that gets easier the closer the target already is to death). W a self-dash directly AWAY
 * from the nearest enemy plus a self-debuff cleanse ("cursed to wander," the mirror of Courier's
 * Q dash-toward). R a survive-floor panic button, same shape as Pizza's/Loki's R -- "a mark that
 * is a curse and a protection at the same time," made literal: for its duration he cannot be
 * killed, even by the thing that marked him. */
#define ARENA_CAIN_PASSIVE_ARMOR            4
#define ARENA_CAIN_Q_RANGE                  6.0f
#define ARENA_CAIN_Q_DAMAGE_BASE            8   /* at 100% target HP */
#define ARENA_CAIN_Q_DAMAGE_LOW_HP         18   /* at ~0% target HP -- an execute */
#define ARENA_CAIN_Q_COOLDOWN_MS         4200
#define ARENA_CAIN_W_DASH_DIST              4.0f
#define ARENA_CAIN_W_COOLDOWN_MS         9000
#define ARENA_CAIN_R_FLOOR_MS            3800
#define ARENA_CAIN_R_COOLDOWN_MS        27000

/* Gunnr (S170-93): passive flat armor bonus, always on -- "quietly been right about three more
 * things," the shieldmaiden's stance, same shape as Cain's own passive. Q a melee-range direct
 * strike, no status effect -- "argued with a raven and was right," a plain correction, not a
 * flourish. W a free toggle self-regen, same shape as Flute Debt's Recouping Interest -- being
 * quietly right keeps paying off over time. R an execute-scaled burst, same shape as Morrigan's/
 * Cain's Q -- "Valhalla has yet to admit it," the vindication finally landing hardest against a
 * target who's already nearly beaten. */
#define ARENA_GUNNR_PASSIVE_ARMOR            4
#define ARENA_GUNNR_Q_RANGE                  2.2f  /* melee range -- close, not a skillshot */
#define ARENA_GUNNR_Q_DAMAGE                10
#define ARENA_GUNNR_Q_COOLDOWN_MS         3200
#define ARENA_GUNNR_W_REGEN_PER_SEC          4
#define ARENA_GUNNR_R_RANGE                  6.0f
#define ARENA_GUNNR_R_DAMAGE_BASE           10   /* at 100% target HP */
#define ARENA_GUNNR_R_DAMAGE_LOW_HP         24   /* at ~0% target HP -- an execute */
#define ARENA_GUNNR_R_COOLDOWN_MS        20000

/* Vassago (S170-93): passive small HP regen, always on, same shape as Dagda's Undry -- ambient
 * restorative foresight, sensing and softening harm before it fully lands. Q a ranged bolt,
 * damage + silence (same shape as Ghost's Q) -- foresight cuts off the enemy's next intended
 * action before they take it. W grants the nearest ally next_cast_refund (same mechanic as
 * Frog's Borrowed Time) -- "the soft foresight," extended outward, lets a teammate's next cast
 * come free. R a fixed zone, silence-only, no damage at all -- the one hero on this roster whose
 * ultimate is pure control, matching "soft" literally: not a hit, a held breath. */
#define ARENA_VASSAGO_PASSIVE_REGEN_PER_SEC   2
#define ARENA_VASSAGO_Q_RANGE                 6.5f
#define ARENA_VASSAGO_Q_DAMAGE                 7
#define ARENA_VASSAGO_Q_SILENCE_MS          1400
#define ARENA_VASSAGO_Q_COOLDOWN_MS         4500
#define ARENA_VASSAGO_W_COOLDOWN_MS        11000
#define ARENA_VASSAGO_R_RADIUS                 4.5f
#define ARENA_VASSAGO_R_DURATION_MS         3500
#define ARENA_VASSAGO_R_SILENCE_MS          1200  /* > the 1000ms tick interval, same margin as Flamel's ROOT_MS -- a shorter value would leave real gaps where a continuously-standing foe isn't silenced between ticks */
#define ARENA_VASSAGO_R_COOLDOWN_MS        23000

/* He Xiangu (S170-93): passive small HP regen, always on, same shape as Dagda's Undry --
 * subsisting on almost nothing, one of the traditional Eight Immortals. Q a ranged bolt that
 * heals her for a fraction of the damage it deals -- "moonlight is also a kind of eating," the
 * same heal-off-a-fraction-of-damage mechanic as Bacon+Puck's R, but on a repeatable Q instead
 * of a one-off burst: the first hero on this roster with real sustain-through-combat on every
 * cast, not a single moment of it. W a free toggle boosting her own regen further, same shape as
 * Flute Debt's Recouping Interest -- self-denial as discipline, not deprivation. R a fixed zone,
 * heal-only, no enemy damage at all -- the roster's first purely-supportive ultimate, the mirror
 * of Vassago's purely-controlling one: she shares her sustenance, doesn't hurt anyone. */
#define ARENA_HE_XIANGU_PASSIVE_REGEN_PER_SEC   2
#define ARENA_HE_XIANGU_Q_RANGE                 6.0f
#define ARENA_HE_XIANGU_Q_DAMAGE                 7
#define ARENA_HE_XIANGU_Q_HEAL_PCT               0.6f  /* fraction of Q's damage returned as self-heal */
#define ARENA_HE_XIANGU_Q_COOLDOWN_MS         4200
#define ARENA_HE_XIANGU_W_REGEN_PER_SEC          4
#define ARENA_HE_XIANGU_R_RADIUS                 4.5f
#define ARENA_HE_XIANGU_R_DURATION_MS         4000
#define ARENA_HE_XIANGU_R_HEAL_PER_TICK          7
#define ARENA_HE_XIANGU_R_COOLDOWN_MS        25000

/* Beleth, the Detonation (S170-93 batch, TYLER multiverse_heroes.md #14, "Beleth, the
 * Detonation" -- 2.22 Hz, emotional detonation/escalation, "every love triangle in the record
 * traces back to her," seed phrase "hope is a terror I leash with song"). Passive flat armor,
 * same always-on shape as Cain's/Gunnr's own (S170-105/S170-93) -- she's survived every
 * escalation she's ever caused. Q a ranged bolt + burn, same shape as Pizza's Q (S170-46) --
 * damage that keeps paying out after contact, matching "no love story... resolves without her
 * frequency somewhere in its last act." W an instant silence-only decree on the nearest enemy,
 * same instant-in-range shape as Paimon's Speaks With Total Authority (S170-55) but with the
 * damage component removed -- pure escalation-denial, not a hit. R, "The Detonation" itself: a
 * genuinely novel shape on this roster -- marks the target's position at cast time (not a
 * continuously-ticking zone like Ghost's/Vassago's/He Xiangu's own R zones), counts down a fuse
 * via r_active_ms, and deals ONE large burst to whoever's still standing in it the instant the
 * fuse hits zero -- "hope is a terror I leash with song": the threat builds in total silence and
 * only resolves once, all at once, exactly like the thing she's named for. */
#define ARENA_BELETH_PASSIVE_ARMOR              3
#define ARENA_BELETH_Q_RANGE                    6.5f
#define ARENA_BELETH_Q_DAMAGE                   7
#define ARENA_BELETH_Q_BURN_MS               2800
#define ARENA_BELETH_Q_BURN_DPS                 6
#define ARENA_BELETH_Q_COOLDOWN_MS           4300
#define ARENA_BELETH_W_RANGE                    6.0f
#define ARENA_BELETH_W_SILENCE_MS            1900
#define ARENA_BELETH_W_COOLDOWN_MS           8500
#define ARENA_BELETH_R_RANGE                    7.0f
#define ARENA_BELETH_R_RADIUS                   3.0f
#define ARENA_BELETH_R_FUSE_MS                1800
#define ARENA_BELETH_R_DAMAGE                  32
#define ARENA_BELETH_R_COOLDOWN_MS           26000

/* MnM, the Shapeshifting Crab (S170-134, TYLER multiverse_heroes.md #114). Founder: "add MnM a
 * shapeshifting rapping crab tank from detroit" -- "tank" is the archetype ask, translated into
 * this roster's existing toolkit rather than a new mechanic: high passive armor (Cain's/Gunnr's
 * flat-armor shape), a toggle for sustained extra tankiness while active (Loki's/Ada's W-armor
 * shape), a melee root+poke Q (Paimon's Q shape), and an R that's the literal mechanical
 * translation of the lore's own framing of "shapeshifting" -- Mid-Piano's line that it's just
 * what happens to a body that's absorbed hits meant for somebody else, built here as a
 * self-root + guaranteed-survival window (Tree's R root+buff shape, with survive_floor_ms in
 * place of Tree's armor bonus): for a few seconds nothing can bring MnM below 1 HP, the shell
 * takes the hit instead of the crab underneath it. */
#define ARENA_MNM_PASSIVE_ARMOR              6
#define ARENA_MNM_W_ARMOR_BONUS               5 /* free toggle, no cooldown -- same convention as Loki's/Ada's own */
#define ARENA_MNM_Q_RANGE                     2.4f /* melee-range clamp, not a skillshot */
#define ARENA_MNM_Q_DAMAGE                    9
#define ARENA_MNM_Q_ROOT_MS                1300
#define ARENA_MNM_Q_COOLDOWN_MS            4000
#define ARENA_MNM_R_ROOT_MS                6000
#define ARENA_MNM_R_SURVIVE_FLOOR_MS        6000
#define ARENA_MNM_R_COOLDOWN_MS            27000

/* Lane creep waves (S170-139). Founder: "add subsystems needed to make
 * creeps a reality" -- clarified as classic MOBA lane-pushing waves,
 * distinct from S170-51's jungle creeps (per-node, stationary, aggro-only).
 * This arena's map has no lanes in the geometric sense (NORTHSTAR §8: "no
 * single chokepoint deciding the match," the whole point of the Arathi
 * Basin open-field design) -- rather than inventing a second map layout, the
 * lane is a straight path along the existing spawn axis: each team's spawn
 * line (x=-8/+8, matching arena_find_owned_node_for_respawn's home_x) to the
 * contested center node (0,0, "Blacksmith" in arena_nodes_reset_layout) to
 * the enemy's spawn line. Waves spawn on a fixed timer per team (no scaling
 * or catch-up rubber-banding -- the simplest honest MVP), march toward the
 * enemy spawn, and stop to fight the nearest hittable enemy hero OR
 * opposing-team lane creep within aggro range instead of marching past a
 * fight in progress -- the actual "push" mechanic: a wave that wins its
 * clash keeps advancing, one that loses stops mattering.
 *
 * No structure/tower/base-HP system exists in this arena yet (the same gap
 * Duck's W, "Government Clearance," is already blocked on) -- a wave that
 * survives all the way to the enemy spawn line currently just despawns
 * rather than damaging anything, flagged not faked, same as every other
 * "doesn't fit this engine yet" gap in this roster. Likewise no gold/XP
 * economy exists to reward a kill -- lane creep kills (by heroes or by each
 * other) remove a threat and nothing more; wiring them into a future
 * economy is a natural follow-on once one exists, not invented here as a
 * standalone reward just for this pass. */
#define ARENA_LANE_WAYPOINT_COUNT      3
#define ARENA_LANE_CREEPS_PER_WAVE     3
#define ARENA_MAX_LANE_CREEPS          (ARENA_LANE_CREEPS_PER_WAVE * 2 * 2) /* both teams, generous headroom for the previous wave still marching when the next spawns */
#define ARENA_LANE_WAVE_INTERVAL_MS    20000
#define ARENA_LANE_WAVE_INITIAL_DELAY_MS 5000 /* real MOBA precedent (LoL's own first wave isn't at 0:00 either) -- also gives a match's opening seconds breathing room before waves are on the board, same spirit as a real "minions spawn in..." countdown */
#define ARENA_LANE_CREEP_HP            60
#define ARENA_LANE_CREEP_SPEED         2.5f /* units/sec -- slower than ARENA_HERO_SPEED (4.0) so a hero can always outrun or intercept a wave */
#define ARENA_LANE_CREEP_DAMAGE        7
#define ARENA_LANE_CREEP_ATTACK_COOLDOWN_MS 1000
#define ARENA_LANE_CREEP_AGGRO_RADIUS  3.5f /* doubles as attack range, same simplification as ARENA_CREEP_AGGRO_RADIUS */
#define ARENA_LANE_CREEP_WAYPOINT_EPSILON 0.15f

typedef struct {
    int active; /* pool slot in use */
    int alive;
    int team;   /* which team this creep fights for -- attacks the OTHER team's heroes/lane creeps only */
    float x, z;
    int hp, max_hp;
    int waypoint_index; /* 0..ARENA_LANE_WAYPOINT_COUNT-1: next waypoint this creep is marching toward */
    int attack_cooldown_ms;
} ArenaLaneCreep;

/* ARENA_HERO_RESPAWN_MS (S170-121, "controlling a node enables its spawn
 * for your team"): team-mode-only hero respawn timer. Before this, death
 * was permanent within a match (arena_update_teams only checked team-wipe
 * for the win condition) -- there was no respawn system at all. A dead
 * hero's timer counts down independently of node ownership, but the actual
 * respawn is withheld (timer pins at 0 and rechecks every tick) until the
 * hero's team owns at least one ArenaNode: territory control is the gate,
 * not just a modifier, matching the founder's framing literally. */
#define ARENA_HERO_RESPAWN_MS 8000

/* Mana (S170-132): flat, roster-wide -- see ArenaHero.mp's own doc comment above. Regen fills
 * an empty pool in a bit under 17s; Q is the cheapest, spammable a few times before running dry,
 * R is the most expensive, deliberately not repeatable back-to-back even when off cooldown. */
#define ARENA_MP_MAX             100
#define ARENA_MP_REGEN_PER_SEC     6
#define ARENA_MP_COST_Q            20
#define ARENA_MP_COST_W            20
#define ARENA_MP_COST_R            45

typedef struct {
    float x, z;
    float target_x, target_z;
    int moving;
    int hp;
    int max_hp;
    /* mp/max_mp (S170-132, founder: "add mp so toggling stuff has a cost spells cant be
       spammed unless its a zero mana spell or ability"): a second resource layered on top of
       cooldowns, not a replacement for them -- a Q/W/R can be off cooldown and still blocked
       for lack of mana. Regenerates passively (see tick_hero_kit); ARENA_MP_COST_Q/W/R are the
       current flat per-slot rate, applied uniformly across the roster. The "zero mana ability"
       exception the founder named isn't in use by any kit yet, but the cost is already a named
       constant per slot rather than inlined at each call site, so making one specific ability
       free later is a one-line change, not a redesign. */
    int mp;
    int max_mp;
    int attack_cooldown_ms;
    int owner; /* 0 = player, 1 = bot in the 1v1 local demo; a slot index 0..ARENA_MAX_HEROES-1 in team mode */
    int alive;
    int team;   /* 2026-07-24: which side, for team-mode nearest-enemy targeting. 1v1 local demo sets 0/1 explicitly. */
    int active; /* 2026-07-24: was this slot ever populated by arena_init_with_heroes/arena_init_teams? Distinct from `alive` (which also goes 0 on death) -- lets a generalized loop over ARENA_MAX_HEROES skip never-used padding slots in 1v1 mode without mistaking them for "already dead" participants. */
    ArenaHeroID hero_id;
    /* Generic ability state, shared field names across kits (Unicorn's
     * Q/W/R and Duck's Q/R both use these) rather than one struct per hero
     * -- simplest thing that works for a 2-kit roster; revisit if a future
     * kit needs state shape these fields can't express. */
    int q_cooldown_ms;
    int w_active;      /* Unicorn's Spaghetti Vent toggle; unused by Duck/Ghost */
    int w_cooldown_ms; /* Ghost's Not a Ghost; Unicorn's W is a free toggle and doesn't use this */
    int r_cooldown_ms;
    int r_active_ms;   /* Unicorn's armor-double / Ghost's Recital zone duration; unused by Duck */
    float r_zone_x, r_zone_z; /* Ghost's Recital: fixed zone position at cast time */
    int r_zone_tick_ms; /* Ghost's Recital: counts up to 1000ms, then ticks one DPS-worth of damage --
                          * a fixed-interval tick rather than fractional-per-tick accumulation, so it
                          * behaves correctly at any real frame rate, not just in a single big test step. */
    /* Status effects -- generic, any hero's kit can apply these to any
     * other hero, not just Ghost's own state (S170-32 is the first kit to
     * apply them, but the fields aren't Ghost-specific). */
    int silenced_ms;    /* > 0: cannot cast Q/W/R */
    int intangible_ms;  /* > 0: cannot be hit by attacks or ability damage */
    /* rooted_ms (S170-46, Tree's Q/R and Flamel's Q/R): > 0: cannot move,
     * even with a move command already queued -- gated in
     * update_hero_motion. Also read by duck_pull_foe as "immune to
     * displacement," honoring Tree's R without a separate generic
     * displacement-immunity field: rooted already means "an external force
     * can't move you" is a natural extension of "you can't move yourself." */
    int rooted_ms;
    /* burning_ms/burn_dps (S170-46, Pizza's Q): a generic damage-over-time
     * debuff, any hero's kit could apply it, not Pizza-specific storage --
     * same reasoning as the other status-effect fields above. Ticks down
     * and deals burn_dps once per 1000ms via burn_tick_ms, mirroring Ghost's
     * R zone's fixed-interval tick. */
    int burning_ms;
    int burn_dps;
    int burn_tick_ms;
    /* survive_floor_ms (S170-46, Pizza's R "Nobody Ever Checks"): > 0: this
     * hero's HP cannot be reduced below 1 by apply_damage, no matter how
     * much raw damage lands -- built for real (not simplified away) since
     * it's the entire point of the ability, using the same centralized
     * apply_damage() every damage call site already routes through. */
    int survive_floor_ms;
    /* aura_tick_ms (S170-46, Pizza's always-on burn aura passive): generic
     * fixed-interval accumulator for a passive that ticks independently of
     * any cast, distinct from r_zone_tick_ms (which is cast-scoped). */
    int aura_tick_ms;
    /* damaged_this_tick (S170-51 cont'd): set by apply_damage() on ANY hit
     * from ANY source (melee, ability, creep, burn tick -- matching real
     * WoW Arathi Basin's "any damage interrupts your capture channel"),
     * read and cleared once per tick by arena_tick_nodes. A simplification
     * of the real mechanic's "the specific channeling character" down to
     * "any hero of the channeling team gets hit interrupts the team's
     * channel" -- this arena tracks capture channels per-team, not per-
     * individual-capturing-hero, flagged here rather than silently
     * narrowed. */
    int damaged_this_tick;
    /* next_cast_refund: generic ally-buff flag (S170-45, Frog's Borrowed
     * Time places this on an ally, not itself) -- the next successful Q/W/R
     * cast by whoever carries this flag has its cooldown refunded to 0
     * instead of the normal value, then the flag clears. Generic so any
     * future ally-buff kit can reuse it, same reasoning as the status-
     * effect fields above. */
    int next_cast_refund;
    /* The Frog's Loop Back (S170-33): a small ring buffer of this hero's
     * own past (x, z, hp), sampled every ARENA_FROG_LOOPBACK_SAMPLE_MS.
     * Generic per-hero state, not Frog-specific storage, same reasoning as
     * the status-effect fields above -- nothing else uses it yet. */
    float loopback_x[ARENA_FROG_LOOPBACK_SLOTS];
    float loopback_z[ARENA_FROG_LOOPBACK_SLOTS];
    int loopback_hp[ARENA_FROG_LOOPBACK_SLOTS];
    int loopback_count;       /* how many slots have ever been written (caps at ARENA_FROG_LOOPBACK_SLOTS) */
    int loopback_next_slot;   /* next slot to write (wraps) */
    int loopback_since_sample_ms;
    /* respawn_ms_remaining (S170-121): only meaningful while !alive in team
     * mode. Set to ARENA_HERO_RESPAWN_MS on death; counts down to 0, then
     * arena_update_teams holds the hero at 0 and retries the node-control
     * check each tick until the team owns a node to respawn onto. Unused by
     * the 1v1 local demo (arena_update), which still ends on first death. */
    int respawn_ms_remaining;
    /* cast_flash_slot (S170-124, "particle effects for spells"): 0 = none,
     * 1/2/3 = Q/W/R -- set unconditionally the instant a cast clears its
     * gate (alive, not silenced, off cooldown) in arena_cast_q/toggle_w/
     * cast_r, regardless of whether that specific cast goes on to hit
     * anything. A real cast animation fires on cast, not just on a landed
     * hit, same convention as any real MOBA. Consumed once per tick by
     * server_broadcast (packaged into the wire snapshot) and cleared right
     * after, same one-tick-lifetime idiom as damaged_this_tick. Unused by
     * the 1v1 local demo, which renders straight off arena_state with no
     * wire hop needed. */
    int cast_flash_slot;
    /* is_clone/clone_owner (S170-141, Tyler's "true Meepo parity" puppet
     * clones): is_clone=1 marks this slot as an AI-driven puppet, never
     * client-owned -- clone_owner is the real owner index (Tyler's own
     * slot) it's linked to for move-mirroring and the shared-fate death
     * rule. Unused (0/-1) by every real, client-owned hero slot. */
    int is_clone;
    int clone_owner;
} ArenaHero;

typedef struct {
    float x, z;
    /* owner, capturing_team, capture_progress_ms, marked_by_team (S170-46,
     * capture mechanic redesigned S170-50): the territory contest state,
     * all recomputed every tick by arena_tick_nodes -- not set directly
     * anywhere else except test setup. */
    int owner;              /* 0 = neutral/contested, 1 = team 0, 2 = team 1 */
    int capturing_team;     /* -1 = no active channel, else 0/1: which team is currently channeling this node */
    int capture_progress_ms; /* 0..ARENA_NODE_CAPTURE_CHANNEL_MS (plus bonuses); resets to 0 on interrupt or on completion */
    int marked_by_team;  /* -1 = unmarked, else team index (Flamel's Overgrowth, absorbed from Druid) */
    int mark_ms_remaining;
} ArenaNode;

/* ArenaCreep (S170-51): one jungle creep per node, index-matched
 * (creeps[i] always belongs to nodes[i]). See the header comment above
 * ARENA_MAX_CREEPS for the full design. */
typedef struct {
    float x, z;
    int hp, max_hp;
    int alive;
    ArenaCreepFlavor flavor;
    int attack_cooldown_ms;
    int respawn_ms_remaining; /* only meaningful while !alive */
    int last_attacked_by_owner; /* -1 = never hit since spawning, else the owner index of whoever last damaged it -- who gets credit on the kill */
} ArenaCreep;

/* ArenaProjectile (S170-136, on-hit status effects generalized S170-140): a
 * real travelling skill-shot, not an instant hit. Straight-line, no homing:
 * velocity is fixed at spawn from the caster's position toward the target's
 * position AT CAST TIME, so a target that moves after the shot is fired can
 * genuinely dodge it by stepping off the original line. One shared pool
 * serves every projectile-casting hero; hero_id is carried along so the
 * client can pick a distinct visual per spell (not just per Q/W/R slot).
 *
 * on_hit_silence_ms/on_hit_root_ms/on_hit_burn_ms/on_hit_burn_dps (S170-140):
 * generic optional status effects applied to whoever the shot actually hits,
 * on top of the flat `damage` every projectile already deals -- 0 means "this
 * shot doesn't apply that effect," same "generic field, not hero-specific
 * storage" convention as the identically-named fields already on ArenaHero.
 * Added converting Ghost's Q (Alien Frequency, silence) and Tyler's Q
 * (Earthbind, root+burn) from instant-hit to real projectiles -- Gary's Q
 * (plain damage only) leaves all four at their zeroed default via
 * arena_spawn_projectile. Server-only, same as damage/radius/velocity --
 * the client only ever needs to draw where the shot currently is. */
#define ARENA_MAX_PROJECTILES 32

typedef struct {
    int active;
    int owner;   /* hero slot that fired it -- for the client's self/team/enemy color convention */
    int team;    /* cached at spawn: which team it damages the OPPOSITE of, even if the caster dies/respawns mid-flight */
    ArenaHeroID hero_id; /* which spell this is, for client-side visual style */
    float x, z;
    float vx, vz;     /* units/sec */
    float radius;
    int damage;
    float max_range;  /* total travel distance before despawning unhit (a whiff) */
    float traveled;
    int on_hit_silence_ms;
    int on_hit_root_ms;
    int on_hit_burn_ms;
    int on_hit_burn_dps;
} ArenaProjectile;

/* ArenaObstacle: static jungle terrain, see the ARENA_OBSTACLE_COUNT
 * comment above for placement rationale. `radius` is the collision/visual
 * footprint (a circle -- the client draws it as one or two boxes, see
 * draw_obstacle in apps/arena, but collision itself stays circle-vs-circle
 * for the same cheap-and-good-enough reason hero-vs-hero would be). */
typedef struct {
    float x, z;
    float radius;
    ArenaObstacleKind kind;
} ArenaObstacle;

typedef struct {
    ArenaHero heroes[ARENA_HEROES_ARRAY_SIZE]; /* S170-141: real per-player range 0..ARENA_MAX_HEROES-1, puppet-clone range after it -- see ARENA_HEROES_ARRAY_SIZE's own doc comment */
    ArenaNode nodes[ARENA_NODE_COUNT];
    ArenaCreep creeps[ARENA_MAX_CREEPS];
    ArenaProjectile projectiles[ARENA_MAX_PROJECTILES];
    ArenaObstacle obstacles[ARENA_OBSTACLE_COUNT];
    ArenaLaneCreep lane_creeps[ARENA_MAX_LANE_CREEPS]; /* S170-139 */
    int lane_wave_timer_ms[2]; /* S170-139: per-team countdown to next wave; starts at 0 (memset), so both teams' first wave spawns on the first tick, matching a real MOBA's 0:00 wave */
    int fountain_tick_ms; /* S170-147: fixed-interval (1000ms) accumulator for the fountain heal tick, same idiom as every other DPS/heal zone's own r_zone_tick_ms -- global, not per-hero, since a fountain heals whoever's nearby, not a single caster's target */
    /* hover_target (S170-143, "hover casting like in wow macros"): per-owner,
     * real per-player range only (clones never cast independently, see
     * S170-141) -- which hero slot owner[i] was hovering the instant they
     * last cast, -1 = none. Set by arena_set_hover_target() right before
     * dispatching a cast (both the networked path via apps/arena_server and
     * the local 1v1 demo's own direct keybind handler), consulted by
     * arena_hover_ally_or_nearest(). Explicitly reset to -1 after every
     * memset (0 would wrongly mean "owner slot 0", not "no hover target" --
     * same sentinel-after-memset idiom as ArenaCreep's
     * last_attacked_by_owner). */
    int hover_target[ARENA_MAX_HEROES];
    int winner; /* 0 = none yet, 1 = player/team 0, 2 = bot/team 1 */
} ArenaState;

extern ArenaState arena_state;

/* When nonzero (the default), arena_update() drives owner 1 via the
 * internal hand-authored bot brain (arena_bot_tick) every tick -- correct
 * for local single-player-vs-bot play (apps/arena's existing local mode).
 * apps/arena_server (2026-07-24 pivot, NORTHSTAR §13) sets this to 0 once a
 * real second client connects, so a real remote player's own move/cast
 * commands aren't immediately overwritten by the bot AI each tick. */
extern int arena_bot_enabled;

/* arena_init defaults to player=Unicorn, bot=Duck (S170-31) -- both slots
 * carry a real kit now, proving Phase D's "both sides" requirement, not
 * just a second player-selectable option. arena_init_with_heroes lets a
 * caller (tests, a future hero-select menu) pick explicitly. */
void arena_init(void);
void arena_init_with_heroes(ArenaHeroID player_hero, ArenaHeroID bot_hero);
void arena_update(unsigned int dt_ms);
void arena_set_move_target(int owner, float x, float z);
void arena_bot_tick(unsigned int dt_ms);

/* Team-mode entry points (2026-07-24, NORTHSTAR §13 cont'd): a real N-vs-N
 * match (up to ARENA_TEAM_SIZE per side). arena_init_teams sets up
 * ARENA_MAX_HEROES slots (team 0 = owners 0..ARENA_TEAM_SIZE-1, team 1 =
 * the rest), all defaulting to ARENA_HERO_UNICORN until each slot's real
 * client sends its own draft pick (apps/arena_server owns that protocol,
 * not this sim layer). arena_update_teams drives all active heroes each
 * tick via nearest-enemy targeting -- no internal bot AI involved (every
 * slot in team mode is filled by a real network client, human or bot). */
void arena_init_teams(void);
void arena_update_teams(unsigned int dt_ms);
ArenaHero *arena_nearest_enemy(int owner);

/* arena_nearest_ally (S170-45): the nearest active, living hero on the SAME
 * team as `owner`, excluding `owner` itself. Mirrors arena_nearest_enemy's
 * exact shape/NULL-safety, the enabling primitive for every ally-targeted
 * kit piece previously skipped for having no target in 1v1 (Ghost's R heal
 * side, Frog's W, Doc Wheel's entire kit). Returns NULL in 1v1 (no
 * teammate exists) or if owner has no living ally right now -- callers
 * must already be NULL-safe the same way they are for arena_nearest_enemy. */
ArenaHero *arena_nearest_ally(int owner);

/* arena_set_hover_target (S170-143): records which hero slot `owner` was
 * hovering at the moment of a cast, -1 for none. Called right before
 * dispatching a cast from both apps/arena_server's PACKET_ARENA_CAST
 * handler and the local 1v1 demo's own direct keybind handler -- generic on
 * purpose (not Doc-Wheel-specific storage), so any future hover-aware
 * ability reuses the same field, same "generic, not hero-specific" idiom as
 * the status-effect fields on ArenaHero. No-op if owner is out of the real
 * per-player range. */
void arena_set_hover_target(int owner, int target);

/* arena_hover_ally_or_nearest (S170-143): the real WoW-macro fallback chain
 * -- "cast on unit=mouseover, or default" -- for ally-targeted abilities.
 * Returns the hovered hero if `owner` has a hover_target recorded AND it's
 * a valid, active, alive, SAME-TEAM hero other than owner itself; otherwise
 * falls back to arena_nearest_ally(owner) exactly as before this existed.
 * Same NULL-safety as arena_nearest_ally (returns NULL if neither
 * resolves). */
ArenaHero *arena_hover_ally_or_nearest(int owner);

/* arena_tick_nodes (S170-46, capture mechanic redesigned S170-50): advances
 * the Arathi Basin-style channel capture for every ArenaNode by dt_ms --
 * exclusive single-team presence within ARENA_NODE_CAPTURE_RADIUS starts or
 * continues that team's channel (flipping an enemy-owned node to neutral
 * immediately, not just on completion); mixed presence, a Pizza's
 * corruption, or the channeling team leaving interrupts it (progress lost,
 * owner unchanged -- no free revert); Flamel's Overgrowth mark
 * decays/refreshes and grants a channel-speed bonus on the marking team's
 * own capture. Called from both arena_update() (1v1, nodes[] already
 * positioned) and arena_update_teams(), same "generalizes cleanly, no
 * special-casing" precedent as arena_nearest_ally/arena_nearest_enemy. */
void arena_tick_nodes(unsigned int dt_ms);

/* arena_fountain_position (S170-147): fills (x,z) with the deterministic,
 * fixed position of fountain `index` (0..ARENA_FOUNTAIN_COUNT-1). Shared by
 * both the sim's own tick (below) and the client's renderer
 * (apps/arena/src/main.c), so the two never drift out of sync -- the same
 * "one source of truth" reasoning arena_obstacles_reset_layout's static
 * table already follows for jungle obstacles. Clamps out-of-range index
 * defensively rather than reading past the internal table. */
void arena_fountain_position(int index, float *x, float *z);

/* arena_tick_fountains (S170-147): heals every active, alive, hittable hero
 * within ARENA_FOUNTAIN_RADIUS of either fountain by ARENA_FOUNTAIN_HEAL_PER_SEC
 * per second (fixed-interval tick, same 1000ms-accumulator idiom as every
 * other DPS/heal zone in this file), capped at max_hp. Neutral -- heals any
 * team, see the header comment above ARENA_FOUNTAIN_COUNT for why. Called
 * from both arena_update() and arena_update_teams(). */
void arena_tick_fountains(unsigned int dt_ms);

/* arena_tick_creeps (S170-51): advances every jungle creep by dt_ms --
 * respawns a dead creep once its timer elapses (re-rolling flavor/HP from
 * its node's CURRENT owner, not whatever it was when it last spawned),
 * ticks its attack cooldown, and has it auto-attack the nearest hittable
 * hero within ARENA_CREEP_AGGRO_RADIUS if one's there (passive-until-
 * approached). Does not itself apply any hero-side damage to the creep or
 * grant kill rewards -- that's arena_hero_attack_creeps, called
 * separately so both halves of creep combat can be reasoned about
 * independently. Called from both arena_update() and arena_update_teams(). */
void arena_tick_creeps(unsigned int dt_ms);

/* arena_spawn_projectile (S170-136, returns a pointer S170-140): fills the
 * first free slot in arena_state.projectiles with a straight-line shot from
 * (x,z) toward (target_x,target_z) at `speed` units/sec, and returns a
 * pointer to it so the caller can optionally set on_hit_silence_ms/
 * on_hit_root_ms/on_hit_burn_ms/on_hit_burn_dps right after (all default to
 * 0 -- "no extra effect" -- so a plain-damage caster like Gary's Q can
 * ignore the return value entirely). Returns NULL if the pool is full
 * (ARENA_MAX_PROJECTILES headroom is generous relative to current cast-rate,
 * so this should never actually happen in practice) -- callers that use the
 * return value must check it first, same NULL-safety convention as
 * arena_nearest_enemy/arena_nearest_ally. */
ArenaProjectile *arena_spawn_projectile(int owner, int team, ArenaHeroID hero_id,
                             float x, float z, float target_x, float target_z,
                             float speed, float radius, int damage, float max_range);

/* arena_tick_projectiles (S170-136, on-hit status effects S170-140):
 * advances every active projectile by dt_ms along its fixed velocity, checks
 * collision against every hittable enemy hero within `radius`, applies
 * damage + armor plus any nonzero on_hit_* status effect on the first hit,
 * and deactivates -- and deactivates unhit projectiles once `traveled`
 * reaches `max_range` (a whiff). Called from both arena_update() and
 * arena_update_teams(), same convention as arena_tick_creeps. */
void arena_tick_projectiles(unsigned int dt_ms);

/* arena_hero_attack_creeps (S170-51): each active, alive hero without a
 * closer enemy HERO in range instead auto-attacks a living creep within
 * ARENA_ATTACK_RANGE if one's there -- creeps are a secondary objective,
 * so a hero already trading blows with an enemy hero doesn't get split
 * attention. On a kill, applies the flavor-specific reward (see the
 * ARENA_MAX_CREEPS header comment) to the killer's team. Called from both
 * arena_update() and arena_update_teams(), after resolve_combat/the melee
 * loop so hero-vs-hero combat is always resolved first each tick. */
void arena_hero_attack_creeps(unsigned int dt_ms);

/* arena_tick_lane_creeps (S170-139): see the header comment above
 * ARENA_LANE_WAYPOINT_COUNT for the full design. Advances each team's wave
 * spawn timer (spawning a fresh ARENA_LANE_CREEPS_PER_WAVE-strong wave at
 * that team's spawn line when it elapses), then advances every active lane
 * creep: if a hittable enemy hero or opposing-team lane creep is within
 * ARENA_LANE_CREEP_AGGRO_RADIUS, stops to fight it instead of advancing;
 * otherwise marches toward its current waypoint, advancing to the next one
 * on arrival, or despawning (no reward, no structure to hit) once it reaches
 * the final waypoint at the enemy's spawn line. Called from both
 * arena_update() and arena_update_teams(), same convention as
 * arena_tick_creeps. */
void arena_tick_lane_creeps(unsigned int dt_ms);

/* arena_hero_attack_lane_creeps (S170-139): mirrors arena_hero_attack_creeps
 * exactly -- each active, alive hero without a closer enemy HERO in range
 * instead auto-attacks the nearest OPPOSING-team lane creep within
 * ARENA_ATTACK_RANGE if one's there. Shares the same attack_cooldown_ms gate
 * as arena_hero_attack_creeps (called immediately after it in both update
 * loops), so a hero that already spent this tick's attack on a jungle creep
 * does not also get a free hit on a lane creep the same tick. No kill
 * reward (see the ARENA_LANE_WAYPOINT_COUNT header comment on why). Called
 * from both arena_update() and arena_update_teams(). */
void arena_hero_attack_lane_creeps(unsigned int dt_ms);

/* Kit casts dispatch on the hero's hero_id, not a hardcoded owner check
 * (S170-31 generalized this from S170-18's Unicorn-only version). No-ops
 * if the hero's kit doesn't have that ability, or if it's on cooldown. */
void arena_cast_q(int owner);
void arena_toggle_w(int owner);
void arena_cast_r(int owner);
float arena_hero_armor(const ArenaHero *h); /* effective armor, incl. Unicorn R's buff */

#endif
