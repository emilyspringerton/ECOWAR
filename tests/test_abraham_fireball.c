/* tests/test_abraham_fireball.c -- headless smoke test for Abraham's Fireball (W)
 * (packages/simulation/arena_game.c, S202-34, 2026-08-26). Same "no SDL/GL dependency"
 * reasoning as test_arena_game.c's own header comment, same file-per-mod precedent
 * test_bloodflower.c/test_tree_passive.c/test_duck_smoke_bomb.c already set.
 *
 * Founder real-time: "give abraham a real targetable slow moving projectile fireball that
 * moves a long distance and damages enemies it passes through, replace his dumbest ability" ->
 * "usually w" -> "there is no real range limit just have it go whatever direction is the
 * click" -> "the targeter is green when you are ready to cast."
 *
 * Same "live round-trip tested, not just compile-checked" bar test_duck_smoke_bomb.c already
 * set for a PARENA mod: this actually casts through arena_toggle_w + tick_hero_kit's real
 * completion path and asserts the shot lands through the real compiled mod
 * (on_abraham_fireball_cast -> redgarden_host_abraham_fireball_cast), not a direct call to the
 * host function. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_abraham_w_requires_a_ground_target(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    abe->mp = 999;

    arena_toggle_w(0); /* no arena_set_ground_target call first -- has_ground_target[0] stays 0 */

    CHECK(abe->casting_slot == 0, "W with no ground target on the cast packet is a no-op -- no windup begins");
    CHECK(abe->w_cooldown_ms == 0, "no cooldown spent on the no-op case");
}

static void test_abraham_w_begins_a_real_windup(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    abe->x = 10.0f; abe->z = 10.0f;
    abe->mp = 999;

    arena_set_ground_target(0, 1, 50.0f, 10.0f); /* due "east," same z */
    arena_toggle_w(0);

    CHECK(abe->casting_slot == 2, "casting a ground-targeted W begins a real windup (slot 2)");
    CHECK(abe->cast_total_ms == ARENA_ABRAHAM_FIREBALL_WINDUP_MS, "windup duration matches the real constant");
    CHECK(abe->cast_time_remaining_ms == ARENA_ABRAHAM_FIREBALL_WINDUP_MS, "windup starts at full duration");
    CHECK(abe->cast_target_x == 50.0f && abe->cast_target_z == 10.0f,
          "the real click point is locked in at cast start");
    CHECK(abe->w_cooldown_ms == ARENA_ABRAHAM_FIREBALL_COOLDOWN_MS, "cooldown is spent on cast");
    CHECK(arena_state.projectiles[0].active == 0,
          "no projectile exists yet -- the shot only fires once the windup completes");
}

static void test_abraham_w_gated_by_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    abe->mp = 999;
    abe->w_cooldown_ms = 5000;

    arena_set_ground_target(0, 1, 50.0f, 10.0f);
    arena_toggle_w(0);

    CHECK(abe->casting_slot == 0, "a cast blocked by cooldown never begins a windup");
}

static void test_abraham_w_gated_by_mana(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    abe->mp = 0;

    arena_set_ground_target(0, 1, 50.0f, 10.0f);
    arena_toggle_w(0);

    CHECK(abe->casting_slot == 0, "a cast blocked by insufficient mana never begins a windup");
}

/* The actual new mechanic: a real piercing shot that damages MULTIPLE enemies standing along
 * its path in one pass, not just the first one it touches (the pre-existing ArenaProjectile
 * behavior every other skill-shot in this file still uses). */
static void test_fireball_completion_spawns_a_real_piercing_shot(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaHero *abe = &arena_state.heroes[0];
    abe->hero_id = ARENA_HERO_ABRAHAM;
    abe->x = 0.0f; abe->z = 0.0f;
    abe->mp = 999;

    arena_set_ground_target(0, 1, 100.0f, 0.0f); /* due "east," effectively along the whole map */
    arena_toggle_w(0);
    CHECK(abe->casting_slot == 2, "setup: windup began");

    /* Drive the windup to completion through the REAL tick function, not a direct field write. */
    arena_update_teams(ARENA_ABRAHAM_FIREBALL_WINDUP_MS + 10);

    CHECK(abe->casting_slot == 0, "windup ends after its own duration");
    ArenaProjectile *shot = NULL;
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        if (arena_state.projectiles[i].active && arena_state.projectiles[i].hero_id == ARENA_HERO_ABRAHAM) {
            shot = &arena_state.projectiles[i];
            break;
        }
    }
    CHECK(shot != NULL, "the fireball actually fires on windup completion, through the real PARENA mod");
    if (shot) {
        CHECK(shot->pierce == 1, "the fireball is a real piercing shot, not a single-hit skill-shot");
        CHECK(shot->vx > 0.0f && fabsf(shot->vz) < 0.01f, "travels toward the real click direction (due east)");
        CHECK(shot->max_range == ARENA_ABRAHAM_FIREBALL_MAX_RANGE, "\"no real range limit\" -- uses the real long-range constant");
    }
}

static void test_piercing_shot_damages_multiple_enemies_in_one_pass(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaHero *abe = &arena_state.heroes[0];
    abe->hero_id = ARENA_HERO_ABRAHAM;
    abe->x = 0.0f; abe->z = 0.0f;
    abe->mp = 999;
    /* Two enemies standing in a line along the shot's own path -- both should take damage
       from the SAME shot in the SAME pass, the real behavior this rework adds. */
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE + 1].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE + 1].hp = arena_state.heroes[ARENA_TEAM_SIZE + 1].max_hp;
    arena_state.heroes[ARENA_TEAM_SIZE + 1].x = 8.0f; arena_state.heroes[ARENA_TEAM_SIZE + 1].z = 0.0f;
    int foe1_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;
    int foe2_hp_before = arena_state.heroes[ARENA_TEAM_SIZE + 1].hp;

    arena_set_ground_target(0, 1, 100.0f, 0.0f);
    arena_toggle_w(0);
    arena_update_teams(ARENA_ABRAHAM_FIREBALL_WINDUP_MS + 10); /* fires the real shot */
    /* Slow speed means the shot needs several ticks to physically reach both foes --
       run enough real ticks for it to traverse the whole 0->8 distance at
       ARENA_ABRAHAM_FIREBALL_SPEED units/sec. */
    for (int i = 0; i < 50; i++) arena_update_teams(200);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe1_hp_before,
          "the first enemy in the shot's path takes real damage");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE + 1].hp < foe2_hp_before,
          "the second enemy further down the same path ALSO takes real damage from the same shot");
}

static void test_abraham_w_only_fires_for_abraham(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_ABRAHAM);
    ArenaHero *unicorn = &arena_state.heroes[0];
    unicorn->mp = 999;

    arena_set_ground_target(0, 1, 50.0f, 0.0f);
    arena_toggle_w(0); /* Unicorn's own W is a free toggle (w_active), not the fireball */

    CHECK(unicorn->casting_slot == 0, "a non-Abraham hero's W never begins a fireball windup");
    CHECK(unicorn->w_active == 1, "Unicorn's own W toggle still works normally, untouched by this rework");
}

int main(void) {
    test_abraham_w_requires_a_ground_target();
    test_abraham_w_begins_a_real_windup();
    test_abraham_w_gated_by_cooldown();
    test_abraham_w_gated_by_mana();
    test_fireball_completion_spawns_a_real_piercing_shot();
    test_piercing_shot_damages_multiple_enemies_in_one_pass();
    test_abraham_w_only_fires_for_abraham();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
