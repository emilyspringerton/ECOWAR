#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_CLIENTS 16
#define MAX_ENTITIES 2048
#define GRID_DIM 20
#define HAND_SIZE 5

#define PACKET_CONNECT 0
#define PACKET_CARD_PLAY 1
#define PACKET_SNAPSHOT 2
#define PACKET_WELCOME 3
#define PACKET_FIND_MATCH 4   /* client -> matchmaker: queue for a match */
#define PACKET_MATCH_FOUND 5  /* matchmaker -> client: game-server port assigned */
#define PACKET_ARENA_MOVE 6     /* client -> arena_server: new move target (x,z) */
#define PACKET_ARENA_CAST 7     /* client -> arena_server: cast q/w/r */
#define PACKET_ARENA_SNAPSHOT 8 /* arena_server -> client: both heroes' state */
#define PACKET_ARENA_PICK 9     /* client -> arena_server: hero pick during draft */
#define PACKET_ARENA_ATTACK 10  /* client -> arena_server: lock onto and auto-attack a specific hero slot, S170-162 */
#define PACKET_ARENA_SHOP_BUY 11  /* client -> arena_server: buy an item by catalog index, S170-175 */
#define PACKET_ARENA_SHOP_SELL 12 /* client -> arena_server: sell an equipped item by slot, S170-175 */

#define ARENA_PHASE_WAITING 0 /* fewer than 2 real players connected yet */
#define ARENA_PHASE_DRAFT   1 /* both connected, waiting on hero picks */
#define ARENA_PHASE_LIVE    2 /* both picked, match clock running */

typedef enum {
    CELL_NEUTRAL = 0,
    CELL_PLAYER = 1,
    CELL_ENEMY = 2,
    CELL_CORRUPTED = 3
} CellState;

typedef enum {
    CARD_MILITIA = 0,
    CARD_SCOUT = 1,
    CARD_SWARMLINGS = 2,
    CARD_OUTPOST = 3,
    CARD_COUNT = 4
} CardId;

typedef enum {
    ENTITY_NONE = 0,
    ENTITY_MILITIA = 1,
    ENTITY_SCOUT = 2,
    ENTITY_SWARMLING = 3,
    ENTITY_OUTPOST = 4,
    ENTITY_VILLAGE = 5
} EntityType;

typedef struct {
    uint8_t type;
    uint8_t client_id;
    uint16_t sequence;
    uint32_t timestamp;
    uint16_t entity_count;
} NetHeader;

typedef struct {
    uint16_t sequence;
    uint32_t timestamp;
    uint8_t card_id;
    int16_t grid_x;
    int16_t grid_z;
} CardPlayCmd;

typedef struct {
    uint16_t id;
    uint8_t type;
    uint8_t owner;
    float x;
    float z;
    uint16_t hp;
    uint8_t state;
} NetEntity;

// Sent by the matchmaker after PACKET_MATCH_FOUND's NetHeader: the UDP port
// of the freshly-spawned red_garden_server instance the client should now
// connect to (see apps/matchmaker/src/main.c).
typedef struct {
    uint16_t port;
} MatchFoundMsg;

// ---- apps/arena_server wire structs (2026-07-24 pivot: the MOBA is the
// product) ----

// PACKET_ARENA_MOVE payload: a new move target for the sending client's own
// hero. No owner field -- the server infers "which hero" from which client
// slot sent the packet (same trust model as PACKET_CARD_PLAY / client_id).
typedef struct {
    float target_x;
    float target_z;
} ArenaMoveCmd;

// PACKET_ARENA_CAST payload: which ability slot (0=Q, 1=W, 2=R) to cast.
// hover_target (S170-143, "hover casting like in wow macros"): which hero slot
// the caster's mouse was over at the moment of casting, -1 (encoded as -1,
// int8_t so it round-trips over the wire unlike uint8_t) if nothing was
// hovered. Consulted by hover-aware abilities (Doc Wheel's Q so far) via
// arena_hover_ally_or_nearest() as a preferred target, falling back to the
// existing nearest-ally targeting when nothing's hovered -- the "macro"
// itself is client-side (only WHICH target rides the packet), matching the
// real WoW mouseover-macro pattern of "cast on unit=mouseover, or default."
typedef struct {
    uint8_t slot;
    int8_t hover_target;
} ArenaCastCmd;

// PACKET_ARENA_PICK payload: which hero (ArenaHeroID) the sending client
// wants to play, sent during ARENA_PHASE_DRAFT.
typedef struct {
    uint8_t hero_id;
} ArenaPickCmd;

// PACKET_ARENA_ATTACK payload (S170-162, NORTHSTAR §17's click-to-attack
// system): which hero slot the sending client's own hero should lock onto
// and auto-attack. Distinct from PACKET_ARENA_MOVE -- §17.1's "right-click
// ground vs right-click a unit" split. The lock persists server-side
// (arena_set_attack_target) until the target dies/becomes unhittable or a
// new PACKET_ARENA_MOVE/PACKET_ARENA_ATTACK arrives; the server chases
// automatically while the target is out of range (pure pursuit, see
// arena_tick_attack_targets), same "yes, automatically, indefinitely" chase
// behavior §17.1 documents for real League.
typedef struct {
    uint8_t target_owner;
} ArenaAttackCmd;

// PACKET_ARENA_SHOP_BUY payload (S170-175, NORTHSTAR §19's shop system):
// which item (index into packages/simulation/arena_game.c's ARENA_ITEMS
// catalog) the sending client's own hero wants to buy. Server validates
// shop-proximity + Flow balance (arena_shop_buy) -- same trust model as
// every other Arena*Cmd, the server infers "which hero" from the sending
// client's own slot.
typedef struct {
    uint8_t item_id;
} ArenaShopBuyCmd;

// PACKET_ARENA_SHOP_SELL payload: which equip slot (ArenaItemSlot) to sell
// back. No bag -- selling just empties the slot (arena_shop_sell).
typedef struct {
    uint8_t slot;
} ArenaShopSellCmd;

// ARENA_SNAPSHOT_ITEM_SLOT_COUNT must match packages/simulation/arena_game.h's
// ARENA_ITEM_SLOT_COUNT (S170-175), same duplication reasoning as every
// other ARENA_SNAPSHOT_* size constant in this file. Needed ahead of
// ArenaHeroSnapshot below since it sizes a fixed array member, unlike the
// others which only size top-level ArenaSnapshotMsg arrays.
#define ARENA_SNAPSHOT_ITEM_SLOT_COUNT 11

// Per-hero state broadcast in PACKET_ARENA_SNAPSHOT. Originally minimal
// (position/HP/alive/hero_id only, no ability-state sync) -- enough for a
// human to see and fight a real remote hero; full status-effect sync
// (silence/intangible/etc.) is a later slice once 1v1 human PvP itself is
// confirmed fun (NORTHSTAR §13).
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t alive;
    uint8_t hero_id;
    // cast_flash_slot (S170-124, "particle effects for spells"): 0 = none,
    // 1/2/3 = Q/W/R -- set the tick a cast clears its gate (alive, not
    // silenced, off cooldown), regardless of whether it goes on to hit
    // anything, same "cast fires the effect, not just a landed hit"
    // convention as any real MOBA. One-tick lifetime: the server clears its
    // own copy right after broadcasting it, so this is only ever nonzero in
    // the single snapshot immediately following the cast.
    uint8_t cast_flash_slot;
    // Ability-readiness fields (S170-137, "QWER animation frames need to
    // indicate visually if an ability is ready to cast or not"): net_mode
    // never calls arena_update() locally -- apps/arena_server is
    // authoritative and the client only ever reads what's broadcast here
    // (see this struct's own history, e.g. the S170-87 node-sync fix) -- so
    // before this, a networked client's own q/w/r_cooldown_ms and mp just
    // sat at zero forever and the HUD's ability tiles rendered every
    // ability as permanently ready, cooldown and mana state notwithstanding,
    // in the one mode (real PvP) where the answer actually matters. Synced
    // per-slot for every hero, not just the local player's, since the
    // struct's already per-slot and singling one out would need its own
    // wire path for one field. max_mp isn't included: it's flat and
    // roster-wide (ARENA_MP_MAX in arena_game.h), so the client already
    // knows it without a wire round-trip.
    uint16_t q_cooldown_ms;
    uint16_t w_cooldown_ms;
    uint16_t r_cooldown_ms;
    uint8_t mp;
    // attack_target (S170-162, NORTHSTAR §17's click-to-attack system,
    // founder: "up our visual affordances for auto attacks so its
    // readable" / "auto target should still have visual affordances"):
    // -1 (encoded as int8_t, same convention as ArenaCastCmd's own
    // hover_target) if this hero has no attack lock right now, else the
    // owner slot it's currently locked onto. Synced for every hero, not
    // just the local player's, so the lock reads clearly to any hero
    // watching the fight -- not just the two heroes actually involved.
    int8_t attack_target;
    // Economy/equipment fields (S170-175, NORTHSTAR §19's Flow/XP + item
    // shop). flow is the current spendable balance; flow_earned only ever
    // increases (the character pane's own lifetime-earned stat, distinct
    // from the spendable balance -- see ArenaHero.flow_earned's own doc
    // comment in packages/simulation/arena_game.h). kills/deaths are hero
    // kills only, same real-MOBA KDA convention. equipped_item is
    // ARENA_SNAPSHOT_ITEM_SLOT_COUNT slots, -1 (int8_t sentinel, same
    // convention as attack_target/hover_target above) if that slot is
    // empty, else an index into the ARENA_ITEMS catalog -- not synced
    // itself, since it's static roster-wide data both sides already build
    // from the same source, same reasoning as max_mp above.
    uint16_t flow;
    uint16_t flow_earned;
    uint16_t xp;
    uint8_t kills;
    uint8_t deaths;
    int8_t equipped_item[ARENA_SNAPSHOT_ITEM_SLOT_COUNT];
    // w_active (S170-180 bugfix, founder: "it seems like toggelable abilities arent
    // working"): real root cause -- this field never existed on the wire at all, so a
    // networked client's own local ArenaHero.w_active sat at whatever memset left it
    // (always 0/off) no matter what arena_toggle_w actually did server-side. The W
    // ability tile's "active" highlight (and anything else client-side that reads
    // w_active) was correspondingly always wrong in net_mode -- looked broken because it
    // was, not a toggle-logic bug. Synced for every hero, not just the local player's,
    // same "the whole battlefield should read clearly" convention as attack_target above.
    uint8_t w_active;
    // Status effects (S170-184 bugfix, found while adding stunned_ms/slowed_ms: NONE of these
    // fields were ever on the wire, the same class of bug w_active's own doc comment above
    // just described -- the client's "status label above the health bar" HUD feature
    // (hero_status_label, S170-133) has been silently non-functional in every real networked
    // match this whole time, since its local ArenaHero copy never received these from the
    // server. Fixed the same way: synced for every hero, not just the local player's own.
    uint16_t silenced_ms;
    uint16_t rooted_ms;
    uint16_t intangible_ms;
    uint16_t burning_ms;
    uint16_t survive_floor_ms;
    uint16_t stunned_ms;   // S170-184: new generic hard-CC field, see ArenaHero's own doc comment
    uint16_t slowed_ms;    // S170-184: new generic move-speed debuff
    uint8_t slow_pct_x100; // 0-100, slow_pct*100 -- quantized same "lossy is fine" precedent as mp (uint8_t)
    uint16_t berserker_ms; // S170-190: Warsong Gulch-style powerup buff, damage bonus
    uint16_t regen_ms;     // S170-190: Warsong Gulch-style powerup buff, HP regen
    // r_zone_x/r_zone_z/r_active_ms (S170-200, founder: "zone abilities dont read at all we
    // need true aoe cast circle... show cast radius... circle on the ground... showing to all
    // participants that the spell was cast there so it reads"): several heroes' R abilities are
    // real fixed-position ground zones (Ghost/Flamel/Morrigan/Paimon/NOOR-1/Vassago/He Xiangu)
    // or a delayed-detonation marker (Beleth) that persist for several real seconds -- none of
    // that state was ever on the wire, so a networked client had no way to know a zone even
    // existed, let alone where or how big (arena_hero_r_zone_radius, arena_game.h, supplies the
    // "how big" half; this supplies "where" and "for how much longer"). Synced for every hero,
    // not just the local player's own, same "the whole battlefield should read clearly"
    // convention as attack_target/w_active above.
    float r_zone_x, r_zone_z;
    uint16_t r_active_ms;
    // casting_slot/cast_time_remaining_ms/cast_total_ms (S170-203, founder: "switch gary w to
    // aimed shot just like wow hunter cast time big damage" -> "ensure cast bar affordance
    // shown to user"): generic cast-time-ability state (ArenaHero's own doc comment in
    // arena_game.h) -- Gary's Aimed Shot (W) is the first ability to use it. Synced for every
    // hero, not just the local player's own, same "the whole battlefield should read clearly"
    // convention as attack_target/w_active/r_zone_x above -- a cast bar only a player can see
    // over their own head isn't the affordance that was asked for; every hero watching a Gary
    // wind up a shot needs to see it too, same as every other cast/status affordance already on
    // this wire. cast_anchor_x/z and cast_target aren't synced -- purely server-side
    // interrupt/damage bookkeeping the client's own cast-bar rendering never needs to read.
    uint8_t casting_slot;
    uint16_t cast_time_remaining_ms;
    uint16_t cast_total_ms;
} ArenaHeroSnapshot;

// ARENA_SNAPSHOT_MAX_HEROES must match packages/simulation/arena_game.h's
// ARENA_MAX_HEROES (ARENA_TEAM_SIZE*2 = 20, S170-183: reverted back to
// 10v10 after briefly being 14/7v7 under S170-178) -- duplicated here
// rather than included, since protocol.h is a lower-level shared header
// that doesn't otherwise depend on the sim package.
#define ARENA_SNAPSHOT_MAX_HEROES 20

// Per-node territory state broadcast in PACKET_ARENA_SNAPSHOT (S170-87 fix
// -- this didn't exist before, and its absence is the real root cause of
// "the two capture nodes render compressed onto one point in net_mode":
// arena_state.nodes[] was never populated client-side at all, left at
// whatever memset zeroed it to (both nodes at the same (0,0) origin).
// capturing_team mirrors ArenaNode's own -1-or-team-index convention but as
// a signed int8 over the wire (uint8 can't represent -1).
typedef struct {
    float x, z;
    uint8_t owner;            /* 0=neutral/contested, 1=team0, 2=team1 -- matches ArenaNode.owner exactly */
    int8_t capturing_team;    /* -1 = no active channel, else 0/1 */
    uint16_t capture_progress_ms;
} ArenaNodeSnapshot;

// ARENA_SNAPSHOT_NODE_COUNT must match packages/simulation/arena_game.h's
// ARENA_NODE_COUNT, same duplication reasoning as ARENA_SNAPSHOT_MAX_HEROES.
#define ARENA_SNAPSHOT_NODE_COUNT 5 /* S170-119: was 2, mirrors arena_game.h's ARENA_NODE_COUNT */

// Per-projectile state broadcast in PACKET_ARENA_SNAPSHOT (S170-136): the
// first travelling skill-shot in this arena (Gary's Q). Only what a client
// needs to render and react to a shot in flight -- position + which spell
// it is (for visual style) + which owner fired it (for the client's
// existing self/team/enemy color convention, same as ArenaHeroSnapshot).
// Damage/radius/velocity stay server-only; the client only ever needs to
// draw where it currently is.
typedef struct {
    float x, z;
    uint8_t owner;
    uint8_t hero_id;
} ArenaProjectileSnapshot;

// ARENA_SNAPSHOT_MAX_PROJECTILES must match packages/simulation/arena_game.h's
// ARENA_MAX_PROJECTILES, same duplication reasoning as the others above.
#define ARENA_SNAPSHOT_MAX_PROJECTILES 32

// Per-jungle-creep state (S170-146, "wire-sync jungle/lane creeps -- the
// single biggest 'looks unfinished in a live match' gap"). Always exactly
// ARENA_SNAPSHOT_CREEP_COUNT entries, index-matched to nodes[] (one creep
// per node, same convention arena_game.h's ArenaCreep already uses) -- a
// fixed-size array like ArenaHeroSnapshot/ArenaNodeSnapshot, not a sparse
// count+array like projectiles, since jungle creeps are always fully
// populated (dead ones still occupy their slot, just alive=0).
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t alive;
    uint8_t flavor; /* 0=neutral, 1=team0, 2=team1 -- matches ArenaCreepFlavor exactly */
} ArenaCreepSnapshot;

// ARENA_SNAPSHOT_CREEP_COUNT must match packages/simulation/arena_game.h's
// ARENA_MAX_CREEPS, same duplication reasoning as the others above.
#define ARENA_SNAPSHOT_CREEP_COUNT 5

// Per-powerup state (S170-190, Warsong Gulch-style Berserker/Regen pickups). Unlike fountains
// (static/deterministic, never synced -- see arena_fountain_position's own doc comment),
// powerups have real dynamic state (grabbed or not) that changes from real gameplay events, so
// the client genuinely needs this over the wire. Always exactly ARENA_SNAPSHOT_POWERUP_COUNT
// entries, same "always fully populated" convention as creeps above (an inactive/just-grabbed
// powerup still occupies its slot, just active=0) -- position synced too rather than requiring
// the client to duplicate arena_powerups_reset_layout's own hardcoded midpoint math.
typedef struct {
    float x, z;
    uint8_t kind;   /* 0=Berserker, 1=Regen -- matches ArenaPowerupKind exactly */
    uint8_t active; /* 1 = sitting on the map, ready to be picked up */
} ArenaPowerupSnapshot;

// ARENA_SNAPSHOT_POWERUP_COUNT must match packages/simulation/arena_game.h's
// ARENA_POWERUP_COUNT, same duplication reasoning as every other ARENA_SNAPSHOT_* size
// constant.
#define ARENA_SNAPSHOT_POWERUP_COUNT 2

// Per-lane-creep state (S170-146). Sparse pool (most slots inactive at any
// given tick, waves come and go), same "count + fixed array" convention as
// projectiles.
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t team;
} ArenaLaneCreepSnapshot;

// ARENA_SNAPSHOT_MAX_LANE_CREEPS must match packages/simulation/arena_game.h's
// ARENA_MAX_LANE_CREEPS, same duplication reasoning as the others above.
#define ARENA_SNAPSHOT_MAX_LANE_CREEPS 12

// PACKET_ARENA_SNAPSHOT payload: up to ARENA_SNAPSHOT_MAX_HEROES hero
// slots, in owner order -- `count` says how many are actually meaningful
// (2 for a 1v1 match, up to 20 for a full 10v10 lobby), same "count +
// fixed-size array" convention as NetEntity/entity_count elsewhere in this
// protocol. Plus the match phase and each side's draft-pick status
// (2026-07-24: draft phase added so players choose a hero instead of it
// being hardcoded Unicorn-vs-Duck). During ARENA_PHASE_WAITING/DRAFT,
// heroes[] content is not meaningful yet -- clients should render a
// lobby/draft UI instead, driven by `phase` and `picked[]`.
typedef struct {
    uint8_t count;
    ArenaHeroSnapshot heroes[ARENA_SNAPSHOT_MAX_HEROES];
    uint8_t winner; /* 0 = none yet, 1 = team/owner 0 won, 2 = team/owner 1 won */
    uint8_t phase;  /* ARENA_PHASE_WAITING/DRAFT/LIVE */
    uint8_t picked[ARENA_SNAPSHOT_MAX_HEROES]; /* 1 once that slot has locked in a hero this draft */
    ArenaNodeSnapshot nodes[ARENA_SNAPSHOT_NODE_COUNT]; /* S170-87 */
    uint8_t projectile_count; /* S170-136 */
    ArenaProjectileSnapshot projectiles[ARENA_SNAPSHOT_MAX_PROJECTILES];
    ArenaCreepSnapshot creeps[ARENA_SNAPSHOT_CREEP_COUNT]; /* S170-146 */
    ArenaPowerupSnapshot powerups[ARENA_SNAPSHOT_POWERUP_COUNT]; /* S170-190 */
    uint8_t lane_creep_count; /* S170-146 */
    ArenaLaneCreepSnapshot lane_creeps[ARENA_SNAPSHOT_MAX_LANE_CREEPS];
    uint16_t resources[2]; /* S170-153: team resource race, capped at ARENA_RESOURCE_CAP */
} ArenaSnapshotMsg;

#endif
