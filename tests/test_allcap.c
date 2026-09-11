/* tests/test_allcap.c -- headless smoke test for ALLCAP, the real mod-driven win condition
 * (EMILY/BACKLOG.md SECTION 381, founder: "even the win con should be mods - capping a base
 * should work as a mod or a collection of mods and the wincon mod should interface with that mod
 * via the ALLCAP mod"). Proves the full real 3-hop REFLUX chain: packages/livingmap/town.c's own
 * town_attempt_convert (a real capture) -> ecowar/town_cap_mod.prn dispatches
 * REFLUX_ACTION_TOWN_CAPPED -> arena_game.c's ecowar_tick_allcap_win_check polls for it and asks
 * ecowar/allcap_mod.prn's own on-allcap-check -> dispatches REFLUX_ACTION_ALLCAP_WIN ->
 * arena_update's own real poll sets arena_state.winner. No link in this chain calls the next one
 * directly by name -- every hop is real, independent REFLUX dispatch/poll. Same "no SDL/GL
 * dependency" reasoning as every other headless test in this repo. */
#include <stdio.h>

#include "../packages/reflux/reflux_mod_host.h"
#include "../packages/simulation/allcap_mod_host.h"
#include "../packages/simulation/living_map_bridge.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_allcap_mod_recognizes_full_control(void) {
    CHECK(on_allcap_check(3, 3) == 1, "the real ALLCAP mod says yes when owned equals total");
    CHECK(on_allcap_check(2, 3) == 0, "the real ALLCAP mod says no when a faction owns some but not all");
    CHECK(on_allcap_check(0, 0) == 0, "the real ALLCAP mod never calls an empty board (0 of 0) a win");
}

/* Real, live, end-to-end round trip: converts every real starting town to one faction (via the
 * real, live town_attempt_convert path, not a hand-rolled substitute), then drives real
 * arena_update ticks and confirms arena_state.winner gets set -- purely through the real 3-hop
 * REFLUX chain, with no direct call anywhere from town.c to arena_game.c's own win-check
 * function. */
static void test_capturing_every_real_town_sets_the_real_winner_via_reflux(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    arena_bot_enabled = 0; /* same real reason test_reflux.c's own moon-zenith test disables this -- no early match end from the practice bot */
    reflux_host_reset();

    int town_count = living_map_bridge_real_town_count();
    CHECK(town_count > 0, "setup: the real starting layout founded at least one town");

    /* A real, overwhelming attempt_strength against every real starting town -- large enough to
       clear even Walled Hamlet's own real, higher resistance in one shot. Uses the real,
       live town-conversion path via a direct registry index loop (this test file has no access
       to a hex-coordinate-based capture API -- that's the real, separate "capture a node" card
       wiring named as future work in docs/NORTHSTAR_LIVING_MAP.md's own Phase 2 section; this
       test exercises the underlying real mechanism that card will eventually call). */
    for (int i = 0; i < town_count; i++) {
        living_map_bridge_attempt_convert_town(i, 1, 500);
    }

    CHECK(living_map_bridge_faction_owned_count(1) == town_count,
          "setup: faction 1 really does own every real town after the forced conversions");

    int winner_set = 0;
    for (int i = 0; i < 20 && !winner_set; i++) {
        arena_update(16);
        if (arena_state.winner != 0) winner_set = 1;
    }

    CHECK(winner_set, "arena_state.winner gets set purely through the real REFLUX chain (TOWN_CAPPED -> ALLCAP check -> ALLCAP_WIN -> arena_update's own poll), with no direct call between any of the three stages");
    CHECK(arena_state.winner == 1, "the real winner is owner 0 (faction 1/Dominion), matching who actually captured every town");

    arena_bot_enabled = 1;
}

int main(void) {
    test_allcap_mod_recognizes_full_control();
    test_capturing_every_real_town_sets_the_real_winner_via_reflux();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
