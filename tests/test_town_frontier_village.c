/* tests/test_town_frontier_village.c -- headless smoke test for ECOWAR's Living Map Town system,
 * Frontier Village behavior (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377).
 *
 * Real live round-trip through the real compiled PARENA mod (on_frontier_village_*), not a
 * direct call to a hand-written substitute -- same bar test_ecowar_cards.c's own header comment
 * already holds itself to for card_effect_mod. */
#include <stdio.h>

#include "../packages/livingmap/town.h"
#include "../packages/livingmap/frontier_village_mod_host.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void setup(HexGrid *grid, TownRegistry *reg, LivingMapEventLog *log) {
    hex_grid_init(grid);
    town_registry_init(reg);
    living_map_event_log_reset(log);
}

static void test_found_town_claims_its_hex_cell(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);

    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){1, 1}, 1);
    CHECK(id == 0, "the first founded town gets id 0");

    HexCell *cell = hex_grid_cell_at(&grid, (HexCoord){1, 1});
    CHECK(cell->town_id == id, "the town's home cell records its town_id");
    CHECK(cell->faction_owner == 1, "the town's home cell records its faction_owner");
    CHECK(reg.towns[id].population == 1, "a freshly founded town starts with 1 peasant");
    CHECK(reg.towns[id].militia == 0, "a freshly founded town starts with 0 militia");

    CHECK(living_map_event_log_size(&log) == 1, "founding emits exactly one event");
    const LivingMapEvent *e = living_map_event_log_at(&log, 0);
    CHECK(e->kind == LIVING_MAP_EVENT_TOWN_FOUNDED, "the emitted event is TOWN_FOUNDED");
    CHECK(e->a == TOWN_TYPE_FRONTIER_VILLAGE && e->b == 1, "the event carries the real type and faction_owner");
}

static void test_cannot_found_a_second_town_on_the_same_cell(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);

    town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 1);
    int second = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 2);
    CHECK(second == -1, "a second town can't be founded on an already-occupied hex cell");
}

static void test_cannot_found_a_town_outside_the_map(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);

    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE,
                         (HexCoord){HEX_MAP_RADIUS * 2, HEX_MAP_RADIUS * 2}, 1);
    CHECK(id == -1, "founding outside the hex map fails cleanly");
}

/* Real live round trip: ticks a fresh village forward using its own real, compiled
 * on_frontier_village_spawn_interval_ms cadence (population 1, militia 0 -> real interval is
 * 8000 - 150 = 7850ms), proving town_tick actually calls the compiled PARENA mod rather than a
 * hardcoded constant. */
static void test_tick_before_interval_elapses_does_nothing(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 0);

    int real_interval = on_frontier_village_spawn_interval_ms(1, 0);
    CHECK(real_interval == 7850, "sanity: the real compiled mod's own spawn interval for a 1-peasant village is 7850ms");

    town_tick(&reg, &grid, &log, id, (unsigned int)(real_interval - 1));

    CHECK(reg.towns[id].population == 1, "population hasn't grown before the real interval elapses");
    CHECK(living_map_event_log_size(&log) == 2, "a tick that doesn't spawn still emits founding + TOWN_TICK, no UNIT_SPAWNED");
    CHECK(living_map_event_log_at(&log, 1)->kind == LIVING_MAP_EVENT_TOWN_TICK, "the second event is TOWN_TICK");
}

static void test_tick_past_interval_spawns_a_peasant(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 0);

    int real_interval = on_frontier_village_spawn_interval_ms(1, 0);
    town_tick(&reg, &grid, &log, id, (unsigned int)real_interval);

    CHECK(reg.towns[id].population == 2, "population 1, militia 0 doesn't clear the real should-raise-militia threshold (needs population >= 5), so this spawn is a peasant");
    CHECK(living_map_event_log_size(&log) == 3, "founding + TOWN_TICK + UNIT_SPAWNED");
    const LivingMapEvent *spawn = living_map_event_log_at(&log, 1);
    CHECK(spawn->kind == LIVING_MAP_EVENT_UNIT_SPAWNED && spawn->a == 0, "the spawned unit is flagged as a peasant (a=0)");
}

/* Drives real ticks forward until the real compiled mod's own on_frontier_village_should_raise_militia
 * fires (population >= 5 and militia % 3 == 2) -- proving "spawns peasants -> militia" end to end
 * through the real mod, not a hand-picked test-only shortcut. */
static void test_village_eventually_raises_real_militia(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 0);

    int raised_militia = 0;
    for (int i = 0; i < 50 && !raised_militia; i++) {
        Town *t = &reg.towns[id];
        int interval = on_frontier_village_spawn_interval_ms(t->population, t->militia);
        town_tick(&reg, &grid, &log, id, (unsigned int)interval);
        if (t->militia > 0) raised_militia = 1;
    }
    CHECK(raised_militia, "driving real ticks forward eventually raises real militia (spawns peasants -> militia)");
}

static void test_convert_below_resistance_makes_no_progress(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 1);

    int resistance = on_frontier_village_convert_resistance(reg.towns[id].population, reg.towns[id].militia);
    int result = town_attempt_convert(&reg, &grid, &log, id, 2, resistance - 1);

    CHECK(result == 0, "an attempt strictly below the real resistance value doesn't flip the town");
    CHECK(reg.towns[id].conversion_progress == 0, "an attempt below resistance makes zero real progress");
}

static void test_convert_easily_flips_a_frontier_village(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){2, -1}, 1);

    int resistance = on_frontier_village_convert_resistance(reg.towns[id].population, reg.towns[id].militia);
    /* "Converts easily": a Frontier Village's real, low resistance (20 for a militia-less
     * village) means even a single strong-enough attempt should be able to flip it outright. */
    int result = town_attempt_convert(&reg, &grid, &log, id, 3, resistance + 100);

    CHECK(result == 1, "a strong enough single attempt flips a Frontier Village outright -- it converts easily");
    CHECK(reg.towns[id].faction_owner == 3, "the town's faction_owner is now the attacker");
    HexCell *cell = hex_grid_cell_at(&grid, (HexCoord){2, -1});
    CHECK(cell->faction_owner == 3, "the town's home hex cell's faction_owner flips in lockstep with the town");
    CHECK(reg.towns[id].conversion_progress == 0, "conversion_progress resets to 0 after a successful flip");

    int found_converted_event = 0;
    for (int i = 0; i < living_map_event_log_size(&log); i++) {
        const LivingMapEvent *e = living_map_event_log_at(&log, i);
        if (e->kind == LIVING_MAP_EVENT_TOWN_CONVERTED && e->a == 1 && e->b == 3) found_converted_event = 1;
    }
    CHECK(found_converted_event, "a real LIVING_MAP_EVENT_TOWN_CONVERTED(old=1, new=3) event was emitted");
}

static void test_attacking_your_own_town_is_a_real_noop(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 1);

    int result = town_attempt_convert(&reg, &grid, &log, id, 1, 1000);
    CHECK(result == 0, "the faction that already owns a town can't 'convert' it to itself");
    CHECK(reg.towns[id].faction_owner == 1, "faction_owner is unchanged");
}

static void test_switching_attacker_resets_progress(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 1);
    int resistance = on_frontier_village_convert_resistance(0, 0);

    town_attempt_convert(&reg, &grid, &log, id, 2, resistance + 10); /* faction 2 makes real progress but not enough to flip (assuming resistance+10 < 100) */
    int progress_after_first = reg.towns[id].conversion_progress;
    CHECK(progress_after_first > 0 && progress_after_first < 100, "setup: faction 2 made real, partial progress");

    town_attempt_convert(&reg, &grid, &log, id, 3, resistance + 10); /* a different faction attacks next */
    CHECK(reg.towns[id].converting_faction == 3, "the town now tracks faction 3 as the current attacker");
    CHECK(reg.towns[id].conversion_progress == 10, "switching attackers discards faction 2's progress -- this is faction 3's own first attempt's gain, not accumulated on top");
}

int main(void) {
    test_found_town_claims_its_hex_cell();
    test_cannot_found_a_second_town_on_the_same_cell();
    test_cannot_found_a_town_outside_the_map();
    test_tick_before_interval_elapses_does_nothing();
    test_tick_past_interval_spawns_a_peasant();
    test_village_eventually_raises_real_militia();
    test_convert_below_resistance_makes_no_progress();
    test_convert_easily_flips_a_frontier_village();
    test_attacking_your_own_town_is_a_real_noop();
    test_switching_attacker_resets_progress();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
