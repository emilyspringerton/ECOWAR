/* packages/livingmap/town.c -- see town.h for the real design rationale. */
#include "town.h"
#include "frontier_village_mod_host.h"
#include "walled_hamlet_mod_host.h"
#include "town_cap_mod_host.h"

#include <string.h>

/* Every real town type besides Frontier Village/Walled Hamlet falls back to this until its own
 * real mod lands (see town.h's own header comment) -- effectively unconvertible, not silently
 * easy. */
#define TOWN_PLACEHOLDER_CONVERT_RESISTANCE 100

/* Walled Hamlet's own "shoots hostile creeps" cooldown -- a real, tunable pace distinct from
 * LIVING_MAP_CREEP_ATTACK_COOLDOWN_MS (creep.h), since a town's defensive fire and a creep's own
 * melee attack are two different real systems with no reason to share a cadence. */
#define WALLED_HAMLET_DEFENSE_COOLDOWN_MS 1500

void town_registry_init(TownRegistry *reg) {
    memset(reg, 0, sizeof(*reg));
    for (int i = 0; i < TOWN_MAX_COUNT; i++) {
        reg->towns[i].active = 0;
    }
    reg->town_count = 0;
}

int town_found(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log,
               TownType type, HexCoord coord, int faction_owner) {
    HexCell *cell = hex_grid_cell_at(grid, coord);
    if (!cell) return -1;
    if (cell->town_id != -1) return -1;
    if (reg->town_count >= TOWN_MAX_COUNT) return -1;

    int id = reg->town_count;
    Town *t = &reg->towns[id];
    t->active = 1;
    t->type = type;
    t->coord = coord;
    t->faction_owner = faction_owner;
    t->population = 1; /* every town starts with one founding peasant/townsfolk */
    t->militia = 0;
    /* Frontier Village and Walled Hamlet each prime their own real spawn timer from their own
     * real compiled mod (matching whatever their own town_tick_* would compute for a fresh
     * 1-population town) rather than starting at 0, so founding a town doesn't grant a free
     * instant spawn regardless of how soon the first tick after it arrives. The remaining 2 types
     * have no real tick behavior yet (see town_tick's own switch), so 0 is fine there -- it's
     * never read. */
    if (type == TOWN_TYPE_FRONTIER_VILLAGE) {
        t->spawn_timer_ms = on_frontier_village_spawn_interval_ms(1, 0);
    } else if (type == TOWN_TYPE_WALLED_HAMLET) {
        t->spawn_timer_ms = on_walled_hamlet_spawn_interval_ms(1, 0);
    } else {
        t->spawn_timer_ms = 0;
    }
    t->conversion_progress = 0;
    t->converting_faction = 0;
    t->defense_cooldown_ms = 0;
    t->militia_bonus = 0;

    cell->town_id = id;
    cell->faction_owner = faction_owner;

    reg->town_count = id + 1;

    living_map_emit(log, LIVING_MAP_EVENT_TOWN_FOUNDED, id, (int)type, faction_owner);
    return id;
}

/* raise_count is how many militia/garrison this spawn tick actually raises -- 1 plus the town's
 * own real militia_bonus (town_apply_militia_boost), clamped so it never raises more than the
 * population actually available to convert (a huge, unrealistic bonus can't drive population
 * negative). Shared by both real town types below since both use the identical "some peasants
 * become defenders" shape, just with different cadence/threshold formulas. */
static int town_raise_count(const Town *t) {
    int raise_count = 1 + t->militia_bonus;
    if (raise_count > t->population) raise_count = t->population;
    if (raise_count < 1) raise_count = 1;
    return raise_count;
}

static void town_tick_frontier_village(Town *t, LivingMapEventLog *log, int town_id, unsigned int dt_ms) {
    t->spawn_timer_ms -= (int)dt_ms;
    if (t->spawn_timer_ms > 0) return;

    if (on_frontier_village_should_raise_militia(t->population, t->militia)) {
        int raised = town_raise_count(t);
        t->population -= raised;
        t->militia += raised;
        living_map_emit(log, LIVING_MAP_EVENT_UNIT_SPAWNED, town_id, 1, raised); /* 1 = militia */
    } else {
        t->population += 1;
        living_map_emit(log, LIVING_MAP_EVENT_UNIT_SPAWNED, town_id, 0, 1); /* 0 = peasant */
    }

    t->spawn_timer_ms = on_frontier_village_spawn_interval_ms(t->population, t->militia);
}

static void town_tick_walled_hamlet(Town *t, LivingMapEventLog *log, int town_id, unsigned int dt_ms) {
    t->spawn_timer_ms -= (int)dt_ms;
    if (t->spawn_timer_ms > 0) return;

    if (on_walled_hamlet_should_raise_militia(t->population, t->militia)) {
        int raised = town_raise_count(t);
        t->population -= raised;
        t->militia += raised;
        living_map_emit(log, LIVING_MAP_EVENT_UNIT_SPAWNED, town_id, 1, raised); /* 1 = garrison */
    } else {
        t->population += 1;
        living_map_emit(log, LIVING_MAP_EVENT_UNIT_SPAWNED, town_id, 0, 1); /* 0 = townsfolk */
    }

    t->spawn_timer_ms = on_walled_hamlet_spawn_interval_ms(t->population, t->militia);
}

void town_tick(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log, int town_id, unsigned int dt_ms) {
    (void)grid;
    if (town_id < 0 || town_id >= reg->town_count) return;
    Town *t = &reg->towns[town_id];
    if (!t->active) return;

    switch (t->type) {
        case TOWN_TYPE_FRONTIER_VILLAGE:
            town_tick_frontier_village(t, log, town_id, dt_ms);
            break;
        case TOWN_TYPE_WALLED_HAMLET:
            town_tick_walled_hamlet(t, log, town_id, dt_ms);
            break;
        case TOWN_TYPE_JUNGLE_ENCLAVE:
        case TOWN_TYPE_BLIGHTED_SETTLEMENT:
            /* No real behavior yet -- see town.h's own header comment. A deliberate, documented
             * no-op, not a crash or a borrowed default. */
            break;
    }

    living_map_emit(log, LIVING_MAP_EVENT_TOWN_TICK, town_id, t->population, t->militia);
}

static int town_convert_resistance(const Town *t) {
    if (t->type == TOWN_TYPE_FRONTIER_VILLAGE) {
        return on_frontier_village_convert_resistance(t->population, t->militia);
    }
    if (t->type == TOWN_TYPE_WALLED_HAMLET) {
        return on_walled_hamlet_convert_resistance(t->population, t->militia);
    }
    return TOWN_PLACEHOLDER_CONVERT_RESISTANCE;
}

void town_apply_militia_boost(TownRegistry *reg, int town_id) {
    if (town_id < 0 || town_id >= reg->town_count) return;
    Town *t = &reg->towns[town_id];
    if (!t->active) return;
    if (t->militia_bonus < TOWN_MILITIA_BONUS_CAP) t->militia_bonus += 1;
}

int town_registry_faction_has_full_control(const TownRegistry *reg, int faction_owner) {
    if (faction_owner == 0) return 0; /* neutral can't "win" -- see town.h's own header comment */

    int saw_any_active_town = 0;
    for (int i = 0; i < reg->town_count; i++) {
        const Town *t = &reg->towns[i];
        if (!t->active) continue;
        saw_any_active_town = 1;
        if (t->faction_owner != faction_owner) return 0;
    }
    return saw_any_active_town;
}

int town_registry_owned_count(const TownRegistry *reg, int faction_owner) {
    int count = 0;
    for (int i = 0; i < reg->town_count; i++) {
        const Town *t = &reg->towns[i];
        if (t->active && t->faction_owner == faction_owner) count++;
    }
    return count;
}

int town_registry_active_count(const TownRegistry *reg) {
    int count = 0;
    for (int i = 0; i < reg->town_count; i++) {
        if (reg->towns[i].active) count++;
    }
    return count;
}

int town_attempt_convert(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log,
                          int town_id, int attacking_faction, int attempt_strength) {
    if (town_id < 0 || town_id >= reg->town_count) return 0;
    Town *t = &reg->towns[town_id];
    if (!t->active) return 0;

    living_map_emit(log, LIVING_MAP_EVENT_CONVERT_ATTEMPT, town_id, attacking_faction, attempt_strength);

    if (attacking_faction == t->faction_owner) return 0; /* already owned by the attacker -- nothing to convert */

    if (t->converting_faction != attacking_faction) {
        /* A different faction is attacking than last time -- no progress carries over. */
        t->converting_faction = attacking_faction;
        t->conversion_progress = 0;
    }

    int resistance = town_convert_resistance(t);
    int gain = attempt_strength - resistance;
    if (gain < 0) gain = 0;
    t->conversion_progress += gain;

    if (t->conversion_progress < 100) return 0;

    int old_owner = t->faction_owner;
    t->faction_owner = attacking_faction;
    t->conversion_progress = 0;
    t->converting_faction = 0;

    HexCell *cell = hex_grid_cell_at(grid, t->coord);
    if (cell) cell->faction_owner = attacking_faction;

    living_map_emit(log, LIVING_MAP_EVENT_TOWN_CONVERTED, town_id, old_owner, attacking_faction);
    /* CAP mod (EMILY/BACKLOG.md SECTION 381, "capping a base should work as a mod"): the
     * LIVING_MAP_EVENT_TOWN_CONVERTED emit just above is this package's own internal event log
     * (living_map_events.h) -- a separate, real system from REFLUX. on_town_captured is the real
     * CROSS-SYSTEM announcement: ecowar/town_cap_mod.prn dispatches REFLUX_ACTION_TOWN_CAPPED,
     * which arena_game.c's own ecowar_tick_allcap_win_check (a REAL SUBSCRIBER this package has
     * never heard of) polls for -- see that function's own doc comment for the real win-condition
     * chain this feeds. */
    on_town_captured(town_id, old_owner, attacking_faction);
    return 1;
}

/* Walled Hamlet's own "shoots hostile creeps": stationary, no chasing (see creep.h's own header
 * comment -- this is a TOWN shooting, not a creep). Targets the NEAREST hostile creep in range,
 * ties broken by lowest creep id -- same deterministic-tiebreak convention creep.c's own
 * creep_find_aggro_target already uses. A whiffed cooldown (nothing in range) spends no
 * cooldown, matching every other "no free swing, but no wasted resource either" convention in
 * this codebase. */
static void town_walled_hamlet_defend(Town *t, int town_id, CreepRegistry *creeps, LivingMapEventLog *log, unsigned int dt_ms) {
    t->defense_cooldown_ms -= (int)dt_ms;
    if (t->defense_cooldown_ms > 0) return;

    int range = on_walled_hamlet_defense_range(t->militia);
    int damage = on_walled_hamlet_defense_damage(t->militia);

    int target = -1;
    int best_dist = range + 1;
    for (int i = 0; i < creeps->creep_count; i++) {
        LivingMapCreep *c = &creeps->creeps[i];
        if (!c->active || !c->alive) continue;
        if (c->faction_owner == t->faction_owner) continue;
        int d = hex_distance(t->coord, c->pos);
        if (d <= range && d < best_dist) {
            best_dist = d;
            target = i;
        }
    }
    if (target == -1) return;

    LivingMapCreep *c = &creeps->creeps[target];
    c->hp -= damage;
    living_map_emit(log, LIVING_MAP_EVENT_TOWN_DEFENSE_FIRE, town_id, target, damage);
    t->defense_cooldown_ms = WALLED_HAMLET_DEFENSE_COOLDOWN_MS;

    if (c->hp <= 0) {
        c->alive = 0;
        /* a = -1 distinguishes "killed by a town's defense fire" from "killed by another creep"
         * (creep.c's own CREEP_KILLED emit passes the killer creep's own id there instead). */
        living_map_emit(log, LIVING_MAP_EVENT_CREEP_KILLED, target, -1, 0);
    }
}

void town_tick_with_creeps(TownRegistry *reg, HexGrid *grid, CreepRegistry *creeps,
                            LivingMapEventLog *log, int town_id, unsigned int dt_ms) {
    town_tick(reg, grid, log, town_id, dt_ms);

    if (town_id < 0 || town_id >= reg->town_count) return;
    Town *t = &reg->towns[town_id];
    if (!t->active || t->type != TOWN_TYPE_WALLED_HAMLET) return;

    town_walled_hamlet_defend(t, town_id, creeps, log, dt_ms);
}
