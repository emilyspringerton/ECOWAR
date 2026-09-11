/* tests/test_creep.c -- headless smoke test for ECOWAR's Living Map creep system: real
 * classic-RTS aggro/chase/leash/reset (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377
 * Phase 2). Same "no SDL/GL dependency" reasoning as every other Living Map test. */
#include <stdio.h>

#include "../packages/livingmap/creep.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void setup(CreepRegistry *reg, LivingMapEventLog *log) {
    creep_registry_init(reg);
    living_map_event_log_reset(log);
}

static void test_spawn_places_creep_at_home_full_hp(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int id = living_map_creep_spawn(&reg, &log, (HexCoord){2, -1}, 3, 50);
    CHECK(id == 0, "the first spawned creep gets id 0");
    CHECK(reg.creeps[id].hp == 50 && reg.creeps[id].max_hp == 50, "a fresh creep starts at full hp");
    CHECK(hex_coord_equal(reg.creeps[id].pos, reg.creeps[id].home), "a fresh creep starts exactly at its own home");
    CHECK(reg.creeps[id].state == LIVING_MAP_CREEP_IDLE, "a fresh creep starts IDLE");

    CHECK(living_map_event_log_size(&log) == 1, "spawning emits exactly one event");
    const LivingMapEvent *e = living_map_event_log_at(&log, 0);
    CHECK(e->kind == LIVING_MAP_EVENT_CREEP_SPAWNED && e->a == 3 && e->b == 50, "the spawn event carries the real faction_owner and max_hp");
}

static void test_same_faction_creeps_never_aggro_each_other(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int a = living_map_creep_spawn(&reg, &log, (HexCoord){0, 0}, 1, 50);
    living_map_creep_spawn(&reg, &log, (HexCoord){1, 0}, 1, 50); /* same faction, adjacent -- well within aggro range */

    for (int i = 0; i < 10; i++) creep_tick_all(&reg, &log, 100);

    CHECK(reg.creeps[a].state == LIVING_MAP_CREEP_IDLE, "same-faction creeps standing right next to each other never aggro");
}

static void test_idle_creep_aggros_a_nearby_hostile_creep(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int a = living_map_creep_spawn(&reg, &log, (HexCoord){0, 0}, 1, 50);
    int b = living_map_creep_spawn(&reg, &log, (HexCoord){2, 0}, 2, 50); /* distance 2, within LIVING_MAP_CREEP_AGGRO_RANGE (3) */

    creep_tick(&reg, &log, a, 0);

    CHECK(reg.creeps[a].state == LIVING_MAP_CREEP_CHASING, "a hostile creep within aggro range triggers a real chase");
    CHECK(reg.creeps[a].chase_target == b, "the chase target is the real nearby hostile creep");

    int found_aggro_event = 0;
    for (int i = 0; i < living_map_event_log_size(&log); i++) {
        const LivingMapEvent *e = living_map_event_log_at(&log, i);
        if (e->kind == LIVING_MAP_EVENT_CREEP_AGGRO && e->subject_id == a && e->a == b) found_aggro_event = 1;
    }
    CHECK(found_aggro_event, "a real LIVING_MAP_EVENT_CREEP_AGGRO event was emitted");
}

static void test_a_far_away_hostile_creep_is_not_aggroed(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int a = living_map_creep_spawn(&reg, &log, (HexCoord){0, 0}, 1, 50);
    living_map_creep_spawn(&reg, &log, (HexCoord){10, 0}, 2, 50); /* far outside aggro range */

    creep_tick(&reg, &log, a, 0);

    CHECK(reg.creeps[a].state == LIVING_MAP_CREEP_IDLE, "a hostile creep far outside aggro range is never noticed");
}

static void test_chasing_creep_closes_distance_and_attacks(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int a = living_map_creep_spawn(&reg, &log, (HexCoord){0, 0}, 1, 50);
    int b = living_map_creep_spawn(&reg, &log, (HexCoord){2, 0}, 2, 50);
    creep_tick(&reg, &log, a, 0); /* aggro */
    CHECK(reg.creeps[a].state == LIVING_MAP_CREEP_CHASING, "setup: chasing");

    /* One real move step (500ms) should close from distance 2 to distance 1 -- still not in
     * attack range (1 hex, since ATTACK_RANGE is 1 and they'd need to be adjacent/same cell). */
    creep_tick(&reg, &log, a, LIVING_MAP_CREEP_MOVE_INTERVAL_MS);
    int dist_after_one_step = hex_distance(reg.creeps[a].pos, reg.creeps[b].pos);
    CHECK(dist_after_one_step == 1, "one real move step closes the gap from 2 to 1 -- a real chase, not a teleport");

    /* Now in attack range -- the next tick should land a real hit. */
    int hp_before = reg.creeps[b].hp;
    creep_tick(&reg, &log, a, LIVING_MAP_CREEP_ATTACK_COOLDOWN_MS);
    CHECK(reg.creeps[b].hp == hp_before - LIVING_MAP_CREEP_ATTACK_DAMAGE, "a real attack lands real damage once in range");

    int found_attack_event = 0;
    for (int i = 0; i < living_map_event_log_size(&log); i++) {
        const LivingMapEvent *e = living_map_event_log_at(&log, i);
        if (e->kind == LIVING_MAP_EVENT_CREEP_ATTACK && e->subject_id == a && e->a == b) found_attack_event = 1;
    }
    CHECK(found_attack_event, "a real LIVING_MAP_EVENT_CREEP_ATTACK event was emitted");
}

static void test_killing_the_target_ends_the_chase(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int a = living_map_creep_spawn(&reg, &log, (HexCoord){0, 0}, 1, 100);
    int b = living_map_creep_spawn(&reg, &log, (HexCoord){1, 0}, 2, 5); /* low hp -- one hit kills */
    creep_tick(&reg, &log, a, 0); /* aggro, already adjacent */
    creep_tick(&reg, &log, a, 0); /* attack -- cooldown starts at 0, so this lands immediately */

    CHECK(!reg.creeps[b].alive, "the low-hp target dies to a single real attack");
    CHECK(reg.creeps[a].state == LIVING_MAP_CREEP_RETURNING, "killing the target ends the chase -- the attacker starts returning home");

    int found_killed_event = 0;
    for (int i = 0; i < living_map_event_log_size(&log); i++) {
        const LivingMapEvent *e = living_map_event_log_at(&log, i);
        if (e->kind == LIVING_MAP_EVENT_CREEP_KILLED && e->subject_id == b && e->a == a) found_killed_event = 1;
    }
    CHECK(found_killed_event, "a real LIVING_MAP_EVENT_CREEP_KILLED event names the real killer creep");
}

static void test_creep_gives_up_beyond_leash_range_and_resets_at_home(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int a = living_map_creep_spawn(&reg, &log, (HexCoord){0, 0}, 1, 50);
    int b = living_map_creep_spawn(&reg, &log, (HexCoord){LIVING_MAP_CREEP_LEASH_RANGE + 2, 0}, 2, 50);
    /* Real leash is checked against the CREEP's own distance from ITS OWN home (never the
     * target's), so simulate "already chased this far" by hand-placing a's own current position
     * past its own real leash range -- a real, direct way to isolate this one rule without first
     * driving dozens of real move ticks just to get there. */
    reg.creeps[a].state = LIVING_MAP_CREEP_CHASING;
    reg.creeps[a].chase_target = b;
    reg.creeps[a].pos = (HexCoord){LIVING_MAP_CREEP_LEASH_RANGE + 1, 0};

    creep_tick(&reg, &log, a, 0);

    CHECK(reg.creeps[a].state == LIVING_MAP_CREEP_RETURNING, "a creep already beyond its own real leash range from home gives up the instant it's checked");

    /* Drive real ticks until it's actually home and reset. */
    int reset = 0;
    for (int i = 0; i < 50 && !reset; i++) {
        creep_tick(&reg, &log, a, LIVING_MAP_CREEP_MOVE_INTERVAL_MS);
        if (reg.creeps[a].state == LIVING_MAP_CREEP_IDLE) reset = 1;
    }
    CHECK(reset, "driving real ticks forward eventually gets the creep home and back to IDLE");
    CHECK(hex_coord_equal(reg.creeps[a].pos, reg.creeps[a].home), "the creep is really back at its own home coord");
    CHECK(reg.creeps[a].hp == reg.creeps[a].max_hp, "the creep resets to full hp only once actually home, same real precedent as ArenaCampMinion");
}

static void test_a_dead_target_also_ends_the_chase(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int a = living_map_creep_spawn(&reg, &log, (HexCoord){0, 0}, 1, 50);
    int b = living_map_creep_spawn(&reg, &log, (HexCoord){1, 0}, 2, 50);
    reg.creeps[a].state = LIVING_MAP_CREEP_CHASING;
    reg.creeps[a].chase_target = b;
    reg.creeps[b].alive = 0; /* died some other way (e.g. a town's own defense fire) */

    creep_tick(&reg, &log, a, 0);

    CHECK(reg.creeps[a].state == LIVING_MAP_CREEP_RETURNING, "a target that died from something else also ends the chase, not just a kill by this creep");
}

int main(void) {
    test_spawn_places_creep_at_home_full_hp();
    test_same_faction_creeps_never_aggro_each_other();
    test_idle_creep_aggros_a_nearby_hostile_creep();
    test_a_far_away_hostile_creep_is_not_aggroed();
    test_chasing_creep_closes_distance_and_attacks();
    test_killing_the_target_ends_the_chase();
    test_creep_gives_up_beyond_leash_range_and_resets_at_home();
    test_a_dead_target_also_ends_the_chase();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
