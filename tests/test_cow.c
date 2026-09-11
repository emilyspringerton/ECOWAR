/* tests/test_cow.c -- headless smoke test for ECOWAR Living Map cows (BACKLOG.md SECTION 377,
 * founder: "add cows"): passive, wandering, harmless neutral wildlife built on top of the real
 * creep aggro/chase/leash/reset system (creep.h). Same "no SDL/GL dependency" reasoning as every
 * other Living Map test. */
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

static void test_cow_spawns_neutral_passive_and_harmless(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int id = creep_spawn_cow(&reg, &log, (HexCoord){3, -1});

    CHECK(id == 0, "the first spawned cow gets id 0");
    CHECK(reg.creeps[id].faction_owner == LIVING_MAP_COW_FACTION_OWNER, "a cow is neutral (faction 0)");
    CHECK(reg.creeps[id].passive == 1, "a cow is real, flagged passive");
    CHECK(reg.creeps[id].hp == LIVING_MAP_COW_HP && reg.creeps[id].max_hp == LIVING_MAP_COW_HP, "a cow has real, low hp");
    CHECK(reg.creeps[id].state == LIVING_MAP_CREEP_IDLE, "a cow starts IDLE, same as any other fresh creep");
}

static void test_cow_never_initiates_aggro_even_with_a_hostile_creep_adjacent(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int cow = creep_spawn_cow(&reg, &log, (HexCoord){0, 0});
    living_map_creep_spawn(&reg, &log, (HexCoord){1, 0}, 1, 50); /* a real, aggressive, hostile-faction creep right next to it */

    for (int i = 0; i < 10; i++) creep_tick(&reg, &log, cow, 100);

    CHECK(reg.creeps[cow].state == LIVING_MAP_CREEP_IDLE, "a cow never transitions to CHASING, no matter how close a hostile creep stands");
    CHECK(reg.creeps[cow].chase_target == -1, "a cow never acquires a chase target");
}

static void test_a_hostile_creep_can_still_aggro_and_kill_a_cow(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int cow = creep_spawn_cow(&reg, &log, (HexCoord){0, 0});
    int hunter = living_map_creep_spawn(&reg, &log, (HexCoord){1, 0}, 1, 50);

    /* Drive the hunter (not the cow) forward -- a cow's own faction_owner (0) is a real, ordinary
       faction value, not immunity, so the hunter's own aggro scan should find it. */
    for (int i = 0; i < 5 && reg.creeps[cow].alive; i++) {
        creep_tick(&reg, &log, hunter, LIVING_MAP_CREEP_ATTACK_COOLDOWN_MS);
    }

    CHECK(reg.creeps[hunter].state != LIVING_MAP_CREEP_IDLE || !reg.creeps[cow].alive,
          "a real hostile-faction creep can aggro a neutral cow -- it's not immune to being targeted, just never fights back");
    CHECK(!reg.creeps[cow].alive, "enough real hits from a hunting creep kill the cow (LIVING_MAP_COW_HP is real and low)");
}

static void test_cow_wanders_but_never_past_its_own_real_radius(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    HexCoord home = {0, 0};
    int cow = creep_spawn_cow(&reg, &log, home);

    int moved_at_least_once = 0;
    for (int i = 0; i < 30; i++) {
        HexCoord before = reg.creeps[cow].pos;
        creep_tick(&reg, &log, cow, LIVING_MAP_COW_WANDER_INTERVAL_MS);
        if (!hex_coord_equal(before, reg.creeps[cow].pos)) moved_at_least_once = 1;
        CHECK(hex_distance(home, reg.creeps[cow].pos) <= LIVING_MAP_COW_WANDER_RADIUS,
              "the cow never wanders past its own real LIVING_MAP_COW_WANDER_RADIUS from home");
    }
    CHECK(moved_at_least_once, "driving real ticks forward, the cow actually moves at least once -- it's not just standing still");
}

static void test_cow_wander_emits_real_events_only_when_it_actually_moves(void) {
    CreepRegistry reg; LivingMapEventLog log;
    setup(&reg, &log);

    int cow = creep_spawn_cow(&reg, &log, (HexCoord){0, 0});
    /* Founding emits CREEP_SPAWNED (1 event) -- confirm every subsequent WANDERED event lines up
       with a real position change, not just fired every tick regardless. */
    int wandered_events = 0;
    HexCoord last_pos = reg.creeps[cow].pos;
    for (int i = 0; i < 10; i++) {
        creep_tick(&reg, &log, cow, LIVING_MAP_COW_WANDER_INTERVAL_MS);
        if (!hex_coord_equal(last_pos, reg.creeps[cow].pos)) {
            wandered_events++;
            last_pos = reg.creeps[cow].pos;
        }
    }

    int real_wandered_events_in_log = 0;
    for (int i = 0; i < living_map_event_log_size(&log); i++) {
        if (living_map_event_log_at(&log, i)->kind == LIVING_MAP_EVENT_CREEP_WANDERED) real_wandered_events_in_log++;
    }
    CHECK(real_wandered_events_in_log == wandered_events, "exactly one real LIVING_MAP_EVENT_CREEP_WANDERED fires per tick that actually moved the cow, no more no less");
}

static void test_cow_wander_is_deterministic_given_the_same_tick_sequence(void) {
    CreepRegistry a, b; LivingMapEventLog log_a, log_b;
    setup(&a, &log_a);
    setup(&b, &log_b);
    creep_spawn_cow(&a, &log_a, (HexCoord){2, -2});
    creep_spawn_cow(&b, &log_b, (HexCoord){2, -2});

    for (int i = 0; i < 20; i++) {
        creep_tick(&a, &log_a, 0, LIVING_MAP_COW_WANDER_INTERVAL_MS);
        creep_tick(&b, &log_b, 0, LIVING_MAP_COW_WANDER_INTERVAL_MS);
    }

    CHECK(hex_coord_equal(a.creeps[0].pos, b.creeps[0].pos),
          "two cows given the identical real tick sequence wander to the identical real position -- deterministic, no PRNG needed");
}

int main(void) {
    test_cow_spawns_neutral_passive_and_harmless();
    test_cow_never_initiates_aggro_even_with_a_hostile_creep_adjacent();
    test_a_hostile_creep_can_still_aggro_and_kill_a_cow();
    test_cow_wanders_but_never_past_its_own_real_radius();
    test_cow_wander_emits_real_events_only_when_it_actually_moves();
    test_cow_wander_is_deterministic_given_the_same_tick_sequence();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
