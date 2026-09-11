/* packages/livingmap/creep.h -- classic-RTS creep aggro/chase/leash/reset for ECOWAR's Living Map
 * (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377 Phase 2).
 *
 * Founder: "make sure they have classic RTS interactions like agro chase and leash." Real,
 * deliberate precedent this mirrors rather than reinvents: packages/simulation/arena_game.h's own
 * ArenaCampMinion (aggro radius vs. attack range as two distinct real values, leash measured from
 * HOME not current position, reset to full HP only once actually back home) -- the exact same
 * "LoL jungle camp" shape, adapted to this layer's own hex-cell granularity instead of continuous
 * float x/z. See each constant's own comment below for the direct analog.
 *
 * This is a standalone, generic hostile-unit system -- it doesn't know or care which town type
 * spawned a creep (Jungle Enclave's hunters, Blighted Settlement's cultists, and a future neutral
 * map-fauna system would all be real, later CALLERS of creep_spawn; none of that exists yet, only
 * the real, tested mechanic itself). Walled Hamlet's own "shoots hostile creeps" (town.c's
 * town_tick_with_creeps) is the first real consumer -- a town defending against creeps, not a
 * creep spawner itself.
 */
#ifndef LIVINGMAP_CREEP_H
#define LIVINGMAP_CREEP_H

#include "hex_grid.h"
#include "living_map_events.h"

/* LIVING_MAP_CREEP_AGGRO_RANGE / _ATTACK_RANGE: same real DETECT-vs-HIT distinction
 * ARENA_CAMP_MINION_AGGRO_RADIUS/_ATTACK_RANGE draws -- a creep can SEE a hostile target several
 * cells away and start chasing before it's actually close enough to swing at it. */
#define LIVING_MAP_CREEP_AGGRO_RANGE 3
#define LIVING_MAP_CREEP_ATTACK_RANGE 1
/* LIVING_MAP_CREEP_LEASH_RANGE: same real ARENA_CAMP_MINION_LEASH_RANGE shape -- double the
 * aggro range, measured from the creep's own HOME coord (never its current position, so leash
 * range can't silently grow as a creep is kited further and further away one aggro-range at a
 * time). */
#define LIVING_MAP_CREEP_LEASH_RANGE (LIVING_MAP_CREEP_AGGRO_RANGE * 2)
#define LIVING_MAP_CREEP_ATTACK_DAMAGE 10
#define LIVING_MAP_CREEP_ATTACK_COOLDOWN_MS 1000
/* One hex step per this many ms while chasing or returning -- a real, tunable pace distinct from
 * arena_game.c's own continuous movement speed, since this layer has no float velocity at all. */
#define LIVING_MAP_CREEP_MOVE_INTERVAL_MS 500

#define LIVING_MAP_CREEP_MAX_COUNT 128

/* Cows (founder: "add cows") -- real Living Map wildlife, same WC3-critter convention as any
 * neutral RTS map: harmless, passive, wandering flavor that never fights back and never
 * initiates aggro on its own. A faction creep's own aggro scan still finds one (faction_owner 0
 * is a real, ordinary faction value, not immunity -- see LivingMapCreep.faction_owner's own doc
 * comment), so cows CAN be killed for target practice, they just never chase or attack back.
 * Deliberately low HP, no death reward this pass -- purely a "the world has life in it" detail,
 * not a farmable resource yet. */
#define LIVING_MAP_COW_HP 10
#define LIVING_MAP_COW_FACTION_OWNER 0
/* A cow never wanders more than this many hexes from where it was placed -- same real "measured
 * from HOME, not current position" convention LIVING_MAP_CREEP_LEASH_RANGE already uses, so a
 * cow can't accumulate drift step by step. */
#define LIVING_MAP_COW_WANDER_RADIUS 2
#define LIVING_MAP_COW_WANDER_INTERVAL_MS 4000

typedef enum {
    LIVING_MAP_CREEP_IDLE = 0,   /* at or near home, scanning for a hostile target every tick (or wandering, if passive) */
    LIVING_MAP_CREEP_CHASING,    /* has a live target within leash range -- closing distance or attacking */
    LIVING_MAP_CREEP_RETURNING   /* gave up (target died, fled, or leash exceeded) -- walking home to reset */
} LivingMapCreepState;

/* faction_owner: same 0=neutral/1=Dominion/2=Symbiosis/3=Corruption numbering as HexCell/Town
 * (hex_grid.h) -- a creep only ever aggros a DIFFERENT faction_owner's creep, including a neutral
 * (0) creep aggroing/being aggroed by any of the 3 real factions (0 is not "immune", it's simply
 * its own distinct faction value the equality check treats like any other). */
typedef struct {
    int active;
    int alive;
    HexCoord home;
    HexCoord pos;
    int faction_owner;
    int hp, max_hp;
    LivingMapCreepState state;
    int chase_target; /* valid only while state == CHASING; -1 otherwise */
    int attack_cooldown_ms;
    int move_timer_ms;
    /* passive (cows, and any future harmless wildlife): 1 = never scans for or initiates aggro --
     * IDLE ticks wander instead (see wander_step_count below). 0 (default, every existing
     * creep_spawn caller) = normal aggressive creep, completely unchanged behavior. A passive
     * creep can still be attacked/killed by an aggressive creep's own aggro scan -- this flag
     * only ever gates THIS creep's own outgoing behavior, never whether others can target it. */
    int passive;
    /* wander_step_count: only meaningful while passive -- a plain, deterministic counter (not a
     * random walk; see creep_tick_wander's own doc comment in creep.c for why) driving which of
     * the 6 real hex directions a wandering cow steps toward next. */
    int wander_step_count;
} LivingMapCreep;

typedef struct {
    LivingMapCreep creeps[LIVING_MAP_CREEP_MAX_COUNT];
    int creep_count; /* high-water mark of ever-spawned creeps -- ids are never reused, same convention as TownRegistry.town_count */
} CreepRegistry;

void creep_registry_init(CreepRegistry *reg);

/* Spawns a new creep at home (which doubles as its starting position), full HP. Fails (-1) if the
 * registry is full. Emits LIVING_MAP_EVENT_CREEP_SPAWNED. Returns the new creep's id on success.
 * Deliberately takes no HexGrid -- unlike a Town, a creep doesn't claim/own its hex cell, so there
 * is nothing to write back into HexCell for this to fail against (no "already occupied" check). */
int creep_spawn(CreepRegistry *reg, LivingMapEventLog *log, HexCoord home, int faction_owner, int max_hp);

/* Spawns a real cow: LIVING_MAP_COW_HP, LIVING_MAP_COW_FACTION_OWNER, passive. Same fail/emit/
 * return-value contract as creep_spawn above (LIVING_MAP_EVENT_CREEP_SPAWNED still fires --
 * there's no separate "it's a cow" event kind, a reader distinguishes one by checking
 * LivingMapCreep.passive on the id the spawn event names). */
int creep_spawn_cow(CreepRegistry *reg, LivingMapEventLog *log, HexCoord home);

/* Advances one creep by dt_ms: IDLE scans for the nearest hostile creep within aggro range and
 * starts chasing it (or, if passive, wanders instead of scanning -- see LivingMapCreep.passive's
 * own doc comment); CHASING moves toward (or attacks, once in range) its target, giving up (->
 * RETURNING) if the target dies/deactivates or the creep's own leash range from home is exceeded;
 * RETURNING walks straight home and resets to full HP (-> IDLE) on arrival. No-op if creep_id is
 * inactive, dead, or out of range. */
void creep_tick(CreepRegistry *reg, LivingMapEventLog *log, int creep_id, unsigned int dt_ms);

/* Convenience: ticks every active, alive creep once, in id order. */
void creep_tick_all(CreepRegistry *reg, LivingMapEventLog *log, unsigned int dt_ms);

#endif /* LIVINGMAP_CREEP_H */
