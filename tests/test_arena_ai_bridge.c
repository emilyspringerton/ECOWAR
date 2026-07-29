/* tests/test_arena_ai_bridge.c — headless test for packages/simulation/
 * arena_ai_bridge.c (NORTHSTAR §12 Phase E, Milestone-6 equivalent,
 * EMILY/BACKLOG.md S170-36). No SDL/GL dependency, same reasoning as the
 * other arena test files. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"
#include "../packages/simulation/arena_ai_bridge.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_hero_name_covers_full_roster(void) {
    CHECK(strcmp(arena_hero_name(ARENA_HERO_UNICORN), "unicorn") == 0, "Unicorn's name token");
    CHECK(strcmp(arena_hero_name(ARENA_HERO_DUCK), "duck") == 0, "Duck's name token");
    CHECK(strcmp(arena_hero_name(ARENA_HERO_GHOST), "ghost") == 0, "Ghost's name token");
    CHECK(strcmp(arena_hero_name(ARENA_HERO_FROG), "frog") == 0, "Frog's name token");
    CHECK(strcmp(arena_hero_name((ArenaHeroID)99), "unknown") == 0,
          "an out-of-range hero_id returns \"unknown\", not garbage or a crash");
}

static void test_serialize_is_stable_for_a_fixed_state(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    arena_state.heroes[0].x = -6.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].hp = 80;
    arena_state.heroes[1].x = 4.0f; arena_state.heroes[1].z = 2.0f;
    arena_state.heroes[1].hp = 60;

    char a[512], b[512];
    arena_serialize_state(0, 1234, a, sizeof(a));
    arena_serialize_state(0, 1234, b, sizeof(b));

    CHECK(strcmp(a, b) == 0, "serializing the same fixed state twice produces an identical string");
    CHECK(strstr(a, "tick:1234") != NULL, "serialized state includes the tick");
    CHECK(strstr(a, "self hero:unicorn") != NULL, "self section names the correct hero");
    CHECK(strstr(a, "hp:80") != NULL, "self section includes current HP");
    CHECK(strstr(a, "foe hero:duck") != NULL, "foe section names the correct hero");
    CHECK(strstr(a, "hp:60") != NULL, "foe section includes current HP");
}

static void test_serialize_self_and_foe_swap_by_owner(void) {
    arena_init_with_heroes(ARENA_HERO_GHOST, ARENA_HERO_FROG);
    char from_owner0[512], from_owner1[512];
    arena_serialize_state(0, 0, from_owner0, sizeof(from_owner0));
    arena_serialize_state(1, 0, from_owner1, sizeof(from_owner1));

    CHECK(strstr(from_owner0, "self hero:ghost") != NULL, "owner 0's view: self is Ghost");
    CHECK(strstr(from_owner0, "foe hero:frog") != NULL, "owner 0's view: foe is Frog");
    CHECK(strstr(from_owner1, "self hero:frog") != NULL, "owner 1's view: self is Frog");
    CHECK(strstr(from_owner1, "foe hero:ghost") != NULL, "owner 1's view: foe is Ghost");
}

static void test_serialize_invalid_owner_writes_empty_string(void) {
    arena_init();
    char buf[64] = "not empty to start";
    arena_serialize_state(5, 0, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "an out-of-range owner writes an empty string, not garbage");
}

/* S170-194, NORTHSTAR §18 ("do the work to prepare for unsupervised learning"). */

static void test_serialize_works_for_team_mode_owners_beyond_1v1(void) {
    /* Real bug this closes: arena_serialize_state used to hardcode `owner > 1` out of range
       and `heroes[1-owner]` as "the foe" -- correct only for the 1v1 local demo, silently
       unusable for every team-mode match (owners 2..ARENA_MAX_HEROES-1), which is almost the
       entire real replay corpus NORTHSTAR §18.4 names. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_FROG;

    char buf[512];
    arena_serialize_state(0, 0, buf, sizeof(buf));

    CHECK(strstr(buf, "self hero:gary") != NULL, "team-mode owner 0 (previously always in range) still serializes correctly");
    CHECK(strstr(buf, "foe hero:frog") != NULL, "and finds the real team-mode opponent via arena_nearest_enemy, not a hardcoded slot");
}

static void test_serialize_no_living_enemy_writes_foe_none(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    char buf[512];
    arena_serialize_state(0, 0, buf, sizeof(buf));

    CHECK(strstr(buf, "self hero:") != NULL, "self section still writes with nobody else on the map");
    CHECK(strstr(buf, "foe none") != NULL, "no living enemy writes a real, parseable \"foe none\" instead of garbage or crashing");
}

static void test_serialize_includes_hero_tags(void) {
    arena_init_with_heroes(ARENA_HERO_GARY, ARENA_HERO_FROG);
    char buf[512];
    arena_serialize_state(0, 0, buf, sizeof(buf));

    CHECK(strstr(buf, "self hero:gary tags:ranged has_homing_attack") != NULL,
          "Gary's own tags (ranged, the one hero with a homing basic attack) appear in the self section");
    CHECK(strstr(buf, "foe hero:frog tags:melee has_stealth") != NULL,
          "Frog's tags (melee, has R vanish stealth) appear in the foe section");
}

static void test_hero_tags_string_covers_a_few_real_kits(void) {
    char buf[96];
    arena_hero_tags_string(ARENA_HERO_GARY, buf, sizeof(buf));
    CHECK(strcmp(buf, "ranged has_homing_attack") == 0, "Gary: ranged + the one homing-attack tag in the roster");

    arena_hero_tags_string(ARENA_HERO_MNM, buf, sizeof(buf));
    CHECK(strcmp(buf, "melee has_stealth") == 0, "MnM: melee (Q explicitly \"melee root+damage\" per docs/HEROES_VS0.md) + has_stealth (S170-208: W Burrow grants untargetability)");

    arena_hero_tags_string(ARENA_HERO_DUCK, buf, sizeof(buf));
    CHECK(strcmp(buf, "melee has_knockback") == 0, "Duck: melee + knockback (Q pulls the foe, a forced displacement)");

    arena_hero_tags_string(ARENA_HERO_ZAGAN, buf, sizeof(buf));
    CHECK(strcmp(buf, "ranged") == 0, "Zagan: ranged (Q is a real 5.0-range poke) with no other tag true -- no heal/knockback/dash/stealth anywhere in the kit");

    arena_hero_tags_string((ArenaHeroID)99, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "an out-of-range hero_id writes an empty tag string, not garbage");
}

static void test_decode_full_action_string(void) {
    ArenaAction act;
    int ok = arena_decode_action("move:4.20,1.00 cast_q:1 cast_w:0 cast_r:1", &act);
    CHECK(ok == 1, "a well-formed action string decodes successfully");
    CHECK(act.has_move == 1, "has_move is set when a move: token is present");
    CHECK(act.move_x > 4.19f && act.move_x < 4.21f, "move_x parsed correctly");
    CHECK(act.move_z > 0.99f && act.move_z < 1.01f, "move_z parsed correctly");
    CHECK(act.cast_q == 1, "cast_q parsed correctly");
    CHECK(act.cast_w == 0, "cast_w parsed correctly");
    CHECK(act.cast_r == 1, "cast_r parsed correctly");
}

static void test_decode_partial_action_defaults_safely(void) {
    ArenaAction act;
    int ok = arena_decode_action("cast_q:1", &act);
    CHECK(ok == 1, "a partial action string (only cast_q) still decodes as found");
    CHECK(act.has_move == 0, "no move: token means has_move stays 0, not garbage coordinates");
    CHECK(act.cast_q == 1, "the one token that was present still parses correctly");
    CHECK(act.cast_w == 0 && act.cast_r == 0, "missing cast tokens default to 0, a safe no-op");
}

static void test_decode_garbage_fails_closed(void) {
    ArenaAction act;
    CHECK(arena_decode_action("this is not a valid action string at all", &act) == 0,
          "a string with none of the recognized tokens returns 0 (do nothing), not a crash");
    CHECK(arena_decode_action(NULL, &act) == 0, "a NULL action string returns 0, not a crash");
    CHECK(arena_decode_action("", &act) == 0, "an empty action string returns 0");
}

int main(void) {
    printf("RED GARDEN arena_ai_bridge headless smoke test\n\n");
    test_hero_name_covers_full_roster();
    test_serialize_is_stable_for_a_fixed_state();
    test_serialize_self_and_foe_swap_by_owner();
    test_serialize_invalid_owner_writes_empty_string();
    test_serialize_works_for_team_mode_owners_beyond_1v1();
    test_serialize_no_living_enemy_writes_foe_none();
    test_serialize_includes_hero_tags();
    test_hero_tags_string_covers_a_few_real_kits();
    test_decode_full_action_string();
    test_decode_partial_action_defaults_safely();
    test_decode_garbage_fails_closed();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
