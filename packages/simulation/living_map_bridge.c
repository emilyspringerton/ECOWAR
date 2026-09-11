/* packages/simulation/living_map_bridge.c -- see living_map_bridge.h for the real design
 * rationale. */
#include "living_map_bridge.h"

/* LIVING_MAP_BRIDGE_HEX_SIZE: the one real number tying the Living Map's own hex-radius-12 grid
 * to this arena's real ARENA_HALF_EXTENT play area -- picked so the grid's own maximum world
 * extent (HEX_MAP_RADIUS hexes out, each hex_axial_to_world x-step is 1.5*hex_size) lands right
 * at ARENA_HALF_EXTENT, i.e. the hex grid exactly covers the same square the arena's own map
 * already occupies. docs/NORTHSTAR_LIVING_MAP.md's own Phase 1 section named this exact number
 * ("hex_size is left caller-tunable... no real system ties the two together yet") as the real,
 * deliberately-deferred decision this file now makes. */
#define LIVING_MAP_BRIDGE_HEX_SIZE (ARENA_HALF_EXTENT / ((float)HEX_MAP_RADIUS * 1.5f))

static HexGrid g_living_map_grid;
static TownRegistry g_living_map_towns;
static CreepRegistry g_living_map_creeps;
static LivingMapEventLog g_living_map_log;

static void found_town(TownType type, HexCoord coord, int faction_owner) {
    town_found(&g_living_map_towns, &g_living_map_grid, &g_living_map_log, type, coord, faction_owner);
}

/* living_map_bridge_init_match -- the real starting layout. Tunable, NOT founder-specified
 * (the founder asked to make sure the existing systems show up in the client/server at all, not
 * for a particular map design) -- named honestly, not presented as a final balance decision:
 * 2 Frontier Villages pre-owned by faction 1 (owner/team 0's own side of the map), 2 by faction 2
 * (owner/team 1's side), and 2 neutral, contestable towns (a Frontier Village + a Walled Hamlet)
 * roughly at midfield -- matching the founder's own "hard to cap all of the nodes at once" win-
 * condition intent (SECTION 377 Phase 2): both sides start with real, defensible territory, and
 * winning requires actually taking the other side's towns AND the neutral ones. Coordinates are
 * hex-grid axial, well within the real HEX_MAP_RADIUS (12) boundary on both the q and r axes so
 * every one of them is guaranteed real, in-range map cells. Cow homes (founder: "add cows") are
 * placed at real, empty cells not already claimed by a town. */
void living_map_bridge_init_match(void) {
    hex_grid_init(&g_living_map_grid);
    town_registry_init(&g_living_map_towns);
    creep_registry_init(&g_living_map_creeps);
    living_map_event_log_reset(&g_living_map_log);

    found_town(TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){-8, 2}, 1);
    found_town(TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){-8, -2}, 1);
    found_town(TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){8, 2}, 2);
    found_town(TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){8, -2}, 2);
    found_town(TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 4}, 0);
    found_town(TOWN_TYPE_WALLED_HAMLET, (HexCoord){0, -4}, 0);

    creep_spawn_cow(&g_living_map_creeps, &g_living_map_log, (HexCoord){-3, 1});
    creep_spawn_cow(&g_living_map_creeps, &g_living_map_log, (HexCoord){3, -1});
    creep_spawn_cow(&g_living_map_creeps, &g_living_map_log, (HexCoord){0, 0});
}

void living_map_bridge_tick(unsigned int dt_ms) {
    for (int i = 0; i < g_living_map_towns.town_count; i++) {
        if (!g_living_map_towns.towns[i].active) continue;
        town_tick_with_creeps(&g_living_map_towns, &g_living_map_grid, &g_living_map_creeps,
                               &g_living_map_log, i, dt_ms);
    }
    creep_tick_all(&g_living_map_creeps, &g_living_map_log, dt_ms);
}

int living_map_bridge_full_control_faction(void) {
    for (int faction = 1; faction <= 3; faction++) {
        if (town_registry_faction_has_full_control(&g_living_map_towns, faction)) return faction;
    }
    return 0;
}

int living_map_bridge_faction_to_owner(int faction) {
    if (faction == 1) return 0;
    if (faction == 2) return 1;
    return -1; /* Corruption (3) -- no real player maps to it in a 2-sided match today */
}

int living_map_bridge_town_count(void) {
    int n = g_living_map_towns.town_count;
    return n > LIVING_MAP_BRIDGE_MAX_SYNC_TOWNS ? LIVING_MAP_BRIDGE_MAX_SYNC_TOWNS : n;
}

void living_map_bridge_town_world_pos(int index, float *out_x, float *out_z) {
    hex_axial_to_world(g_living_map_towns.towns[index].coord, LIVING_MAP_BRIDGE_HEX_SIZE, out_x, out_z);
}

int living_map_bridge_town_type(int index) {
    return (int)g_living_map_towns.towns[index].type;
}

int living_map_bridge_town_faction_owner(int index) {
    return g_living_map_towns.towns[index].faction_owner;
}

int living_map_bridge_town_population(int index) {
    return g_living_map_towns.towns[index].population;
}

int living_map_bridge_town_militia(int index) {
    return g_living_map_towns.towns[index].militia;
}

int living_map_bridge_creep_count(void) {
    int n = g_living_map_creeps.creep_count;
    return n > LIVING_MAP_BRIDGE_MAX_SYNC_CREEPS ? LIVING_MAP_BRIDGE_MAX_SYNC_CREEPS : n;
}

void living_map_bridge_creep_world_pos(int index, float *out_x, float *out_z) {
    hex_axial_to_world(g_living_map_creeps.creeps[index].pos, LIVING_MAP_BRIDGE_HEX_SIZE, out_x, out_z);
}

int living_map_bridge_creep_faction_owner(int index) {
    return g_living_map_creeps.creeps[index].faction_owner;
}

int living_map_bridge_creep_alive(int index) {
    return g_living_map_creeps.creeps[index].alive;
}

/* Bloodflower's own real hostile creeps (SECTION 380): stronger than a cow (10 hp, passive) but
 * not a boss -- a real, deliberate, tunable middle value. Living Map faction 3 (Corruption)'s
 * first real, live use anywhere in this codebase. */
#define LIVING_MAP_BLOODFLOWER_HOSTILE_HP 40
#define LIVING_MAP_BLOODFLOWER_HOSTILE_FACTION 3

int living_map_bridge_spawn_hostile_creep_at_map_center(void) {
    HexCoord center = {0, 0};
    return living_map_creep_spawn(&g_living_map_creeps, &g_living_map_log, center,
                                   LIVING_MAP_BLOODFLOWER_HOSTILE_FACTION,
                                   LIVING_MAP_BLOODFLOWER_HOSTILE_HP);
}
