/* packages/livingmap/town.c -- see town.h for the real design rationale. */
#include "town.h"
#include "frontier_village_mod_host.h"

#include <string.h>

/* Every real town type besides Frontier Village falls back to this until its own real mod
 * lands (see town.h's own header comment) -- effectively unconvertible, not silently easy. */
#define TOWN_PLACEHOLDER_CONVERT_RESISTANCE 100

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
    t->population = 1; /* every town starts with one founding peasant */
    t->militia = 0;
    /* Frontier Village primes its own real spawn timer from the real compiled mod (matching
     * whatever town_tick_frontier_village would compute for a fresh 1-peasant village) rather
     * than starting at 0, so founding a town doesn't grant a free instant spawn regardless of how
     * soon the first tick after it arrives. Every other type has no real tick behavior yet (see
     * town_tick's own switch), so 0 is fine there -- it's never read. */
    t->spawn_timer_ms = (type == TOWN_TYPE_FRONTIER_VILLAGE)
        ? on_frontier_village_spawn_interval_ms(1, 0)
        : 0;
    t->conversion_progress = 0;
    t->converting_faction = 0;

    cell->town_id = id;
    cell->faction_owner = faction_owner;

    reg->town_count = id + 1;

    living_map_emit(log, LIVING_MAP_EVENT_TOWN_FOUNDED, id, (int)type, faction_owner);
    return id;
}

static void town_tick_frontier_village(Town *t, LivingMapEventLog *log, int town_id, unsigned int dt_ms) {
    t->spawn_timer_ms -= (int)dt_ms;
    if (t->spawn_timer_ms > 0) return;

    if (on_frontier_village_should_raise_militia(t->population, t->militia)) {
        t->population -= 1;
        t->militia += 1;
        living_map_emit(log, LIVING_MAP_EVENT_UNIT_SPAWNED, town_id, 1, 0); /* 1 = militia */
    } else {
        t->population += 1;
        living_map_emit(log, LIVING_MAP_EVENT_UNIT_SPAWNED, town_id, 0, 0); /* 0 = peasant */
    }

    t->spawn_timer_ms = on_frontier_village_spawn_interval_ms(t->population, t->militia);
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
        case TOWN_TYPE_JUNGLE_ENCLAVE:
        case TOWN_TYPE_BLIGHTED_SETTLEMENT:
            /* No real behavior yet -- see town.h's own header comment. A deliberate, documented
             * no-op, not a crash or a borrowed Frontier Village default. */
            break;
    }

    living_map_emit(log, LIVING_MAP_EVENT_TOWN_TICK, town_id, t->population, t->militia);
}

static int town_convert_resistance(const Town *t) {
    if (t->type == TOWN_TYPE_FRONTIER_VILLAGE) {
        return on_frontier_village_convert_resistance(t->population, t->militia);
    }
    return TOWN_PLACEHOLDER_CONVERT_RESISTANCE;
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
    return 1;
}
