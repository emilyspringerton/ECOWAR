/* tests/test_reflux.c -- headless smoke test for REFLUX, the real cross-mod pub/sub layer
 * (EMILY/BACKLOG.md SECTION 380), and its first real subscriber/dispatcher pair: bloodflower_mod
 * (dispatcher) + bloodflower_hostile_spawner_mod (subscriber), connected only through the shared
 * REFLUX action log, never a direct call between the two. Same "no SDL/GL dependency" reasoning
 * as test_arena_game.c's own header comment. */
#include <stdio.h>

#include "../packages/reflux/reflux_mod_host.h"
#include "../packages/simulation/bloodflower_hostile_spawner_mod_host.h"
#include "../packages/simulation/living_map_bridge.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_reflux_dispatch_and_poll_round_trip(void) {
    /* Fresh state: arena_init_with_heroes resets everything living_map_bridge owns, but REFLUX's
       own log is a separate global -- reset it directly for a clean, isolated test. */
    reflux_host_reset();

    CHECK(reflux_log_size() == 0, "a freshly reset REFLUX log starts empty");

    reflux_dispatch(REFLUX_ACTION_BLOODFLOWER_TRIGGERED, 7, -3, 0);
    CHECK(reflux_log_size() == 1, "dispatching a real action grows the real log");
    CHECK(reflux_action_type_at(0) == REFLUX_ACTION_BLOODFLOWER_TRIGGERED, "the dispatched action's real type round-trips");
    CHECK(reflux_action_a_at(0) == 7 && reflux_action_b_at(0) == -3, "the dispatched action's real payload round-trips");

    reflux_dispatch(999, 1, 2, 3);
    CHECK(reflux_log_size() == 2, "a second real dispatch grows the log again");
    CHECK(reflux_action_type_at(1) == 999, "an unrelated action type is preserved as-is, not filtered by the log itself");
}

static void test_out_of_range_poll_is_a_real_noop(void) {
    reflux_host_reset();
    CHECK(reflux_action_type_at(0) == -1, "polling an empty log at index 0 returns -1, not garbage");
    CHECK(reflux_action_a_at(5) == 0, "polling an out-of-range index for a payload field returns a real, safe 0");
}

/* The real point of REFLUX: bloodflower_mod.prn (the dispatcher) and bloodflower_hostile_
 * spawner_mod.prn (the subscriber) never call each other directly -- this test proves the
 * subscriber's own real decision logic correctly recognizes the dispatcher's own real action
 * type, with zero shared code between them beyond the one, real, cross-referenced constant. */
static void test_hostile_spawner_recognizes_the_real_bloodflower_action(void) {
    CHECK(on_bloodflower_hostile_spawner_should_react(REFLUX_ACTION_BLOODFLOWER_TRIGGERED) == 1,
          "the real subscriber mod recognizes the real bloodflower-triggered action type");
    CHECK(on_bloodflower_hostile_spawner_should_react(999) == 0,
          "the real subscriber mod ignores an unrelated action type");
    CHECK(on_bloodflower_hostile_spawner_creep_count() == 5,
          "the real subscriber mod names a real, fixed 'a bunch' count");
}

/* Real, live, end-to-end round trip: driving a real match through a full day/night cycle
 * (arena_update, exactly the real server's own per-tick call) makes the real Bloodflower pickup
 * appear (existing behavior, untouched) AND makes real hostile Living Map creeps appear at the
 * real map midpoint -- through REFLUX alone, no direct call from bloodflower_mod to the spawner. */
static void test_moon_zenith_spawns_real_hostile_creeps_via_reflux(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    /* Real bot AI (arena_bot_enabled, default 1) would otherwise walk hero 1 into hero 0 and win
     * the match within a few real seconds (hero 0 never moves or fights back in this test) --
     * arena_update returns instantly once arena_state.winner is set, which would silently starve
     * arena_tick_daynight of the ~190 real simulated seconds it needs to ever reach zenith. Same
     * "disable the practice bot for a controlled test" convention apps/arena_training/src/
     * headless.c's own sim_init already uses. */
    arena_bot_enabled = 0;
    reflux_host_reset(); /* arena_init doesn't touch REFLUX's own separate global -- reset explicitly for a clean run */

    int creeps_before = living_map_bridge_creep_count(); /* the 3 real starting cows */

    int found_hostile = 0;
    for (int i = 0; i < 250000 && !found_hostile; i += 500) {
        arena_update(500); /* same real dt granularity test_bloodflower.c's own moon-zenith test already uses */
        for (int c = 0; c < living_map_bridge_creep_count(); c++) {
            if (living_map_bridge_creep_faction_owner(c) == 3) { found_hostile = 1; break; }
        }
    }

    CHECK(found_hostile, "driving a real match through a full day/night cycle spawns a real Living Map creep owned by faction 3 (Corruption) -- Bloodflower's own real hostile spawn, delivered purely through REFLUX");
    CHECK(living_map_bridge_creep_count() >= creeps_before + 5,
          "at least 5 new real creeps appeared (on-bloodflower-hostile-spawner-creep-count's own real answer), not just one");
    CHECK(arena_state.bloodflower_active, "the original Bloodflower pickup still spawns too -- REFLUX is additive, not a replacement for the existing behavior");

    arena_bot_enabled = 1; /* restore the real default -- this is process-global state a later test binary run could otherwise inherit */
}

int main(void) {
    test_reflux_dispatch_and_poll_round_trip();
    test_out_of_range_poll_is_a_real_noop();
    test_hostile_spawner_recognizes_the_real_bloodflower_action();
    test_moon_zenith_spawns_real_hostile_creeps_via_reflux();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
