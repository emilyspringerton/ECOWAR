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

/* Auto-target redesign (2026-08-26, founder real-time: "why does gary work but abraham doesnt"
   -> "fuck it have the fireball go infinitely across the map" -> "have it fire at the nearest
   enemy no matter how far away"): W no longer needs a manual ground-target click at all --
   arena_set_ground_target/has_ground_target are dead for this ability now (see
   arena_toggle_w's own ARENA_HERO_ABRAHAM case doc comment). It auto-targets the nearest
   living enemy instead, so the real "no-op" case is now "no living enemy exists anywhere,"
   not "no ground target was set." */
static void test_abraham_w_requires_a_living_enemy(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    abe->mp = 999;
    arena_state.heroes[1].alive = 0; /* the only other hero, dead -- no valid target anywhere */

    arena_toggle_w(0);

    CHECK(abe->casting_slot == 0, "W with no living enemy anywhere is a no-op -- no windup begins");
    CHECK(abe->w_cooldown_ms == 0, "no cooldown spent on the no-op case");
}

static void test_abraham_w_begins_a_real_windup(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    abe->x = 10.0f; abe->z = 10.0f;
    abe->mp = 999;
    foe->x = 50.0f; foe->z = 10.0f; /* due "east," same z -- arena_init_with_heroes already puts them on opposing teams */

    arena_toggle_w(0); /* no arena_set_ground_target call -- auto-targets the nearest enemy (foe) */

    CHECK(abe->casting_slot == 2, "casting W auto-targets the nearest enemy and begins a real windup (slot 2)");
    CHECK(abe->cast_total_ms == ARENA_ABRAHAM_FIREBALL_WINDUP_MS, "windup duration matches the real constant");
    CHECK(abe->cast_time_remaining_ms == ARENA_ABRAHAM_FIREBALL_WINDUP_MS, "windup starts at full duration");
    CHECK(abe->cast_target_x == 50.0f && abe->cast_target_z == 10.0f,
          "the nearest enemy's own position is locked in as the cast target at cast start");
    CHECK(abe->w_cooldown_ms == ARENA_ABRAHAM_FIREBALL_COOLDOWN_MS, "cooldown is spent on cast");
    CHECK(arena_state.projectiles[0].active == 0,
          "no projectile exists yet -- the shot only fires once the windup completes");
}

static void test_abraham_w_gated_by_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    abe->mp = 999;
    abe->w_cooldown_ms = 5000;

    arena_toggle_w(0);

    CHECK(abe->casting_slot == 0, "a cast blocked by cooldown never begins a windup");
}

static void test_abraham_w_gated_by_mana(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    ArenaHero *abe = &arena_state.heroes[0];
    abe->mp = 0;

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
    ArenaHero *foe = &arena_state.heroes[1];
    abe->hero_id = ARENA_HERO_ABRAHAM;
    abe->x = 0.0f; abe->z = 0.0f;
    abe->mp = 999;
    /* arena_init_teams() puts indices 0..9 on team 0 -- heroes[1] needs to be a real enemy
       (team 1) for the auto-target redesign to find it at all, not just "the other active
       hero." Positioned due east so the directional assertions below stay meaningful. */
    foe->team = 1;
    foe->x = 100.0f; foe->z = 0.0f;

    arena_toggle_w(0); /* auto-targets foe, the only living enemy */
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

/* Ignite (2026-08-26, founder: "make it so that the fireball ignites the enemies it touches
   making them have burning too"): the fireball's own on_hit_burn_ms/on_hit_burn_dps fields
   (set in redgarden_host_abraham_fireball_cast, the same host function the real compiled
   PARENA mod already calls into -- no bypass) should apply a real burn DoT on landing, same
   generic mechanic Pizza/Flute Debt/Tyler's own Q abilities already use. */
static void test_fireball_ignites_the_foe_it_hits(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    arena_bot_enabled = 0; /* isolate the fireball's own effect from internal bot combat noise */
    ArenaHero *abe = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = abe->x + 5.0f; foe->z = abe->z;
    abe->mp = 999;

    arena_toggle_w(0); /* auto-targets foe */
    for (int t = 0; t < 300 && foe->burning_ms == 0; t++) arena_update(16);

    CHECK(foe->burning_ms > 0, "the fireball applies a real burn DoT on landing");
    CHECK(foe->burn_dps == ARENA_ABRAHAM_FIREBALL_BURN_DPS, "burn damage matches the real constant");
}

/* Cast-freeze (2026-08-26, founder: "when you hit w on abraham and you are moving dont have it
   blow the cooldown and do nothing have it freeze the player for the length of the cast for
   that ability"): before this, tick_hero_kit's own generic "movement interrupts an in-progress
   cast" rule (S170-203, real and still correct for Gary's own Aimed Shot) silently ate the
   fireball's mana/cooldown with no fireball if the caster was moving -- same drift-from-
   cast_anchor check every casting_slot user shares. Fixed for Abraham specifically (see
   update_hero_motion's own doc comment for why Gary is untouched): pressing W while already
   walking now freezes him in place for the windup instead, so the cast always actually lands. */
static void test_abraham_w_freezes_movement_and_still_fires_while_moving(void) {
    arena_init_with_heroes(ARENA_HERO_ABRAHAM, ARENA_HERO_UNICORN);
    arena_bot_enabled = 0;
    ArenaHero *abe = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = abe->x + 30.0f; foe->z = abe->z; /* far enough not to interfere */
    abe->mp = 999;

    arena_set_move_target(0, abe->x + 20.0f, abe->z);
    for (int t = 0; t < 5; t++) arena_update(16); /* real movement in progress before the cast */
    float x_before_cast = abe->x;

    arena_toggle_w(0); /* press W while already moving */
    CHECK(abe->casting_slot == 2, "W still begins a real windup even while already moving");

    int stayed_frozen = 1;
    int ticks = 0;
    while (abe->casting_slot != 0 && ticks < 100) {
        arena_update(16);
        if (abe->x != x_before_cast) stayed_frozen = 0;
        ticks++;
    }
    CHECK(stayed_frozen, "position stays frozen every tick of the windup, not just the first");
    CHECK(abe->casting_slot == 0, "the windup completes on its own, uninterrupted by the earlier movement");
    int fired = 0;
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        if (arena_state.projectiles[i].active && arena_state.projectiles[i].hero_id == ARENA_HERO_ABRAHAM) fired = 1;
    }
    CHECK(fired, "the fireball actually fires -- the cooldown/mana already spent at cast start wasn't wasted");
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
    test_abraham_w_requires_a_living_enemy();
    test_abraham_w_begins_a_real_windup();
    test_abraham_w_gated_by_cooldown();
    test_abraham_w_gated_by_mana();
    test_fireball_completion_spawns_a_real_piercing_shot();
    test_piercing_shot_damages_multiple_enemies_in_one_pass();
    test_fireball_ignites_the_foe_it_hits();
    test_abraham_w_freezes_movement_and_still_fires_while_moving();
    test_abraham_w_only_fires_for_abraham();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
