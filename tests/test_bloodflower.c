/* tests/test_bloodflower.c -- headless smoke test for the day/night cycle +
 * moon-zenith Bloodflower event (packages/simulation/arena_game.c,
 * 2026-08-25). Same "no SDL/GL dependency" reasoning as test_arena_game.c's
 * own header comment. Deliberately a separate file rather than appended
 * into that already-large file -- this is a self-contained new subsystem
 * with its own real round-trip (arena_tick_daynight -> the compiled PARENA
 * mod on_moon_zenith, stdlib/redgarden/bloodflower_mod.prn ->
 * redgarden_host_spawn_bloodflower), not an incremental addition to
 * existing hero/creep/tower mechanics.
 *
 * This is the "live round-trip tested, not just compile-checked" bar
 * PITVIPER's S192-01 (BACKLOG.md SECTION 192) already set for a PARENA
 * mod: it actually calls arena_tick_daynight in a real fast-forward loop
 * and asserts the event fires through the real compiled mod, not a direct
 * call to redgarden_host_spawn_bloodflower. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* Analytical zenith: orbit_t = time_sec * ARENA_DAYNIGHT_ORBIT_SPEED reaches 1.5*PI (moon
 * height's first local maximum, sun height's first local minimum) at t = 1.5*PI / 0.025 ~=
 * 188.5s -- same math arena_tick_daynight itself runs, computed independently here so this test
 * isn't just checking the implementation against itself with different variable names. */
#define EXPECTED_ZENITH_MS 188500

static void test_moon_zenith_fires_bloodflower_via_real_parena_mod(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    CHECK(!arena_state.bloodflower_active, "bloodflower_active starts at 0");

    int fired_at_ms = -1;
    for (int ms = 0; ms < 220000; ms += 500) {
        arena_tick_daynight(500);
        if (arena_state.bloodflower_active) {
            fired_at_ms = ms;
            break; /* stop the instant it fires so the 20s claim window below is still open */
        }
    }
    CHECK(fired_at_ms >= 0, "moon-zenith event fires a real Bloodflower within one full cycle");

    float delta = (float)fired_at_ms - (float)EXPECTED_ZENITH_MS;
    if (delta < 0) delta = -delta;
    CHECK(delta <= 3000.0f, "fires within 3s of the independently-computed analytical zenith (500ms tick granularity)");
    CHECK(arena_state.bloodflower_x == 0.0f && arena_state.bloodflower_z == 0.0f,
          "spawns at real map center (0,0), same convention as arena_fountain_position/arena_camp_position");
}

static void test_bloodflower_claim_grants_flow_and_despawns(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    for (int ms = 0; ms < 220000; ms += 500) {
        arena_tick_daynight(500);
        if (arena_state.bloodflower_active) break;
    }
    CHECK(arena_state.bloodflower_active, "setup: bloodflower is active before the claim check");

    arena_state.heroes[0].active = 1;
    arena_state.heroes[0].alive = 1;
    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].flow = 0;
    arena_hero_claim_bloodflower();

    CHECK(!arena_state.bloodflower_active, "claiming despawns the Bloodflower");
    CHECK(arena_state.heroes[0].flow == ARENA_BLOODFLOWER_CLAIM_FLOW, "claiming hero gains the real ARENA_BLOODFLOWER_CLAIM_FLOW amount");
}

static void test_bloodflower_out_of_radius_grants_nothing(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    for (int ms = 0; ms < 220000; ms += 500) {
        arena_tick_daynight(500);
        if (arena_state.bloodflower_active) break;
    }
    arena_state.heroes[0].active = 1;
    arena_state.heroes[0].alive = 1;
    arena_state.heroes[0].x = ARENA_BLOODFLOWER_CLAIM_RADIUS * 10.0f; /* far outside the claim radius */
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].flow = 0;
    arena_hero_claim_bloodflower();

    CHECK(arena_state.bloodflower_active, "a hero outside the claim radius does not claim it");
    CHECK(arena_state.heroes[0].flow == 0, "and gains no Flow");
}

static void test_bloodflower_despawns_unclaimed_after_lifetime(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    for (int ms = 0; ms < 220000; ms += 500) {
        arena_tick_daynight(500);
        if (arena_state.bloodflower_active) break;
    }
    CHECK(arena_state.bloodflower_active, "setup: bloodflower active before the timeout window");
    /* No hero claims it -- just let it run out. */
    for (int ms = 0; ms < ARENA_BLOODFLOWER_LIFETIME_MS + 1000; ms += 500) {
        arena_tick_daynight(500);
    }
    CHECK(!arena_state.bloodflower_active, "an unclaimed Bloodflower despawns after ARENA_BLOODFLOWER_LIFETIME_MS");
}

static void test_zenith_event_does_not_refire_within_same_cycle(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    int fire_count = 0;
    for (int ms = 0; ms < 220000; ms += 500) {
        int was_active = arena_state.bloodflower_active;
        arena_tick_daynight(500);
        if (arena_state.bloodflower_active && !was_active) fire_count++;
    }
    /* This loop covers slightly less than one full ~251s cycle, so a single fire is the correct
     * expectation -- the edge-trigger guard's job is verified by that single fire count, not a
     * second cycle boundary. */
    CHECK(fire_count == 1, "fires exactly once within one cycle -- edge-trigger guard doesn't double-fire near the peak");
}

int main(void) {
    test_moon_zenith_fires_bloodflower_via_real_parena_mod();
    test_bloodflower_claim_grants_flow_and_despawns();
    test_bloodflower_out_of_radius_grants_nothing();
    test_bloodflower_despawns_unclaimed_after_lifetime();
    test_zenith_event_does_not_refire_within_same_cycle();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
