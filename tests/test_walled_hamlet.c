/* tests/test_walled_hamlet.c -- headless smoke test for ECOWAR's Living Map Walled Hamlet town
 * type (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377 Phase 2). Real live round-trip
 * through the real compiled PARENA mod (on_walled_hamlet_*), same bar test_town_frontier_village.c
 * already holds itself to. Also covers town_apply_militia_boost (shared by both real town types)
 * and the full-control win condition. */
#include <stdio.h>

#include "../packages/livingmap/town.h"
#include "../packages/livingmap/walled_hamlet_mod_host.h"

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

static void test_walled_hamlet_resistance_is_much_higher_than_frontier(void) {
    int hamlet_resistance = on_walled_hamlet_convert_resistance(1, 0);
    int frontier_baseline = 20; /* frontier_village_mod.prn's own real baseline, see test_town_frontier_village.c */
    CHECK(hamlet_resistance > frontier_baseline, "a fresh Walled Hamlet's real resistance is higher than a fresh Frontier Village's -- 'slow to flip'");
}

static void test_walled_hamlet_is_genuinely_slow_to_flip(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_WALLED_HAMLET, (HexCoord){0, 0}, 1);

    int resistance = on_walled_hamlet_convert_resistance(reg.towns[id].population, reg.towns[id].militia);
    /* The exact same attempt strength that outright flips a Frontier Village in
     * test_town_frontier_village.c's own test_convert_easily_flips_a_frontier_village (frontier
     * resistance + 100) should NOT be enough here, since Walled Hamlet's own baseline resistance
     * (60) already eats most of that headroom. */
    int result = town_attempt_convert(&reg, &grid, &log, id, 3, 20 + 100);

    CHECK(result == 0, "an attempt strong enough to outright flip a Frontier Village does NOT flip a Walled Hamlet -- it's genuinely slow to flip");
    CHECK(reg.towns[id].faction_owner == 1, "the hamlet is still owned by its original faction");
    (void)resistance;
}

static void test_walled_hamlet_raises_garrison_faster_than_frontier(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_WALLED_HAMLET, (HexCoord){0, 0}, 0);

    int raised_garrison = 0;
    int ticks = 0;
    for (int i = 0; i < 20 && !raised_garrison; i++) {
        Town *t = &reg.towns[id];
        int interval = on_walled_hamlet_spawn_interval_ms(t->population, t->militia);
        town_tick(&reg, &grid, &log, id, (unsigned int)interval);
        ticks++;
        if (t->militia > 0) raised_garrison = 1;
    }
    CHECK(raised_garrison, "driving real ticks forward eventually raises real garrison (townsfolk -> garrison)");
    CHECK(ticks <= 3, "a Walled Hamlet raises its first garrison soldier within 3 real ticks (population >= 3, faster threshold than Frontier Village's >= 5)");
}

static void test_militia_boost_stacks_the_raise_count(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 0);

    /* Drive the village up to the real should-raise-militia threshold once, unboosted, to confirm
     * a bare raise really is 1 (matching test_town_frontier_village.c's own baseline). */
    Town *t = &reg.towns[id];
    while (t->militia == 0) {
        int interval = on_frontier_village_spawn_interval_ms(t->population, t->militia);
        town_tick(&reg, &grid, &log, id, (unsigned int)interval);
    }
    CHECK(t->militia == 1, "setup: an unboosted Frontier Village raises exactly 1 militia at a time");

    /* "so when you play it twice villages start putting up three militia out at a time" --
     * playing the card twice should mean the NEXT raise brings 1 (base) + 2 (bonus) = 3. */
    town_apply_militia_boost(&reg, id);
    town_apply_militia_boost(&reg, id);
    CHECK(t->militia_bonus == 2, "two real card plays stack to a +2 militia_bonus");

    int militia_before = t->militia;
    while (t->militia == militia_before) {
        int interval = on_frontier_village_spawn_interval_ms(t->population, t->militia);
        town_tick(&reg, &grid, &log, id, (unsigned int)interval);
    }
    CHECK(t->militia == militia_before + 3, "after two real militia-boost plays, the next raise brings 3 militia at once, exactly as specified");
}

static void test_militia_boost_is_capped(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);
    int id = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 0);

    for (int i = 0; i < TOWN_MILITIA_BONUS_CAP + 10; i++) {
        town_apply_militia_boost(&reg, id);
    }
    CHECK(reg.towns[id].militia_bonus == TOWN_MILITIA_BONUS_CAP, "militia_bonus is capped at TOWN_MILITIA_BONUS_CAP, not unbounded");
}

static void test_walled_hamlet_shoots_a_hostile_creep_in_range(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log; CreepRegistry creeps;
    setup(&grid, &reg, &log);
    creep_registry_init(&creeps);

    int town_id = town_found(&reg, &grid, &log, TOWN_TYPE_WALLED_HAMLET, (HexCoord){0, 0}, 1);
    int creep_id = creep_spawn(&creeps, &log, (HexCoord){1, 0}, 2, 50); /* hostile (faction 2), within the real base defense range (1, zero garrison) */

    int range = on_walled_hamlet_defense_range(0);
    int damage = on_walled_hamlet_defense_damage(0);
    CHECK(range >= 1, "sanity: even a garrison-less hamlet has a real, nonzero defense range");

    town_tick_with_creeps(&reg, &grid, &creeps, &log, town_id, 0);

    CHECK(creeps.creeps[creep_id].hp == 50 - damage, "the hamlet's real stationary defense fire damages a hostile creep in range, on the very first opportunity (cooldown starts at 0)");

    int found_defense_event = 0;
    for (int i = 0; i < living_map_event_log_size(&log); i++) {
        const LivingMapEvent *e = living_map_event_log_at(&log, i);
        if (e->kind == LIVING_MAP_EVENT_TOWN_DEFENSE_FIRE && e->subject_id == town_id && e->a == creep_id) found_defense_event = 1;
    }
    CHECK(found_defense_event, "a real LIVING_MAP_EVENT_TOWN_DEFENSE_FIRE event names the real shooting town and its real target");
}

static void test_walled_hamlet_never_shoots_its_own_faction(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log; CreepRegistry creeps;
    setup(&grid, &reg, &log);
    creep_registry_init(&creeps);

    int town_id = town_found(&reg, &grid, &log, TOWN_TYPE_WALLED_HAMLET, (HexCoord){0, 0}, 1);
    int creep_id = creep_spawn(&creeps, &log, (HexCoord){1, 0}, 1, 50); /* SAME faction as the hamlet */

    town_tick_with_creeps(&reg, &grid, &creeps, &log, town_id, 0);

    CHECK(creeps.creeps[creep_id].hp == 50, "a hamlet never fires on a creep of its own faction");
}

static void test_walled_hamlet_defense_respects_its_own_cooldown(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log; CreepRegistry creeps;
    setup(&grid, &reg, &log);
    creep_registry_init(&creeps);

    int town_id = town_found(&reg, &grid, &log, TOWN_TYPE_WALLED_HAMLET, (HexCoord){0, 0}, 1);
    int creep_id = creep_spawn(&creeps, &log, (HexCoord){1, 0}, 2, 1000); /* high hp -- survives many real shots */

    town_tick_with_creeps(&reg, &grid, &creeps, &log, town_id, 0); /* first shot lands (cooldown starts at 0) */
    int hp_after_first_shot = creeps.creeps[creep_id].hp;

    town_tick_with_creeps(&reg, &grid, &creeps, &log, town_id, 1); /* 1ms later -- real cooldown not elapsed */

    CHECK(creeps.creeps[creep_id].hp == hp_after_first_shot, "the hamlet's real defense cooldown prevents a second shot 1ms after the first");
}

static void test_full_control_win_condition(void) {
    HexGrid grid; TownRegistry reg; LivingMapEventLog log;
    setup(&grid, &reg, &log);

    CHECK(town_registry_faction_has_full_control(&reg, 1) == 0, "an empty board is not a win for anyone");

    int a = town_found(&reg, &grid, &log, TOWN_TYPE_FRONTIER_VILLAGE, (HexCoord){0, 0}, 1);
    int b = town_found(&reg, &grid, &log, TOWN_TYPE_WALLED_HAMLET, (HexCoord){1, 0}, 1);
    CHECK(town_registry_faction_has_full_control(&reg, 1) == 1, "faction 1 owns every active town -- a real win");
    CHECK(town_registry_faction_has_full_control(&reg, 2) == 0, "faction 2 owns nothing -- not a win for faction 2");
    CHECK(town_registry_faction_has_full_control(&reg, 0) == 0, "neutral (0) can never win the full-control condition, even if it somehow owned everything");

    town_attempt_convert(&reg, &grid, &log, b, 2, 60 + 8 * 0 + 100); /* flip the hamlet to faction 2 */
    CHECK(reg.towns[b].faction_owner == 2, "setup: the hamlet really did flip");
    CHECK(town_registry_faction_has_full_control(&reg, 1) == 0, "faction 1 no longer has full control once it loses even one town");
    (void)a;
}

int main(void) {
    test_walled_hamlet_resistance_is_much_higher_than_frontier();
    test_walled_hamlet_is_genuinely_slow_to_flip();
    test_walled_hamlet_raises_garrison_faster_than_frontier();
    test_militia_boost_stacks_the_raise_count();
    test_militia_boost_is_capped();
    test_walled_hamlet_shoots_a_hostile_creep_in_range();
    test_walled_hamlet_never_shoots_its_own_faction();
    test_walled_hamlet_defense_respects_its_own_cooldown();
    test_full_control_win_condition();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
