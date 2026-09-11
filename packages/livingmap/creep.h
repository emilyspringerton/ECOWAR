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

typedef enum {
    LIVING_MAP_CREEP_IDLE = 0,   /* at or near home, scanning for a hostile target every tick */
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

/* Advances one creep by dt_ms: IDLE scans for the nearest hostile creep within aggro range and
 * starts chasing it; CHASING moves toward (or attacks, once in range) its target, giving up (->
 * RETURNING) if the target dies/deactivates or the creep's own leash range from home is exceeded;
 * RETURNING walks straight home and resets to full HP (-> IDLE) on arrival. No-op if creep_id is
 * inactive, dead, or out of range. */
void creep_tick(CreepRegistry *reg, LivingMapEventLog *log, int creep_id, unsigned int dt_ms);

/* Convenience: ticks every active, alive creep once, in id order. */
void creep_tick_all(CreepRegistry *reg, LivingMapEventLog *log, unsigned int dt_ms);

#endif /* LIVINGMAP_CREEP_H */
