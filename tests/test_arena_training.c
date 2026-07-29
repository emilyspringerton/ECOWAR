/* tests/test_arena_training.c -- headless test for apps/arena_training/src/headless.c (S170-224,
 * NORTHSTAR §21). Calls sim_init/sim_step/sim_get_obs/sim_get_done/sim_get_winner/sim_reset
 * directly (same C functions ctypes calls from Python, just exercised from C here so this repo's
 * own test suite catches a regression without needing Python/ctypes installed). A live ctypes
 * round-trip against the compiled .so was also run manually to confirm the ABI itself works --
 * see this pass's own commit message -- this test covers the C-level logic/contract, not the
 * ctypes marshaling itself. */
#include <stdio.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

/* Declarations mirror apps/arena_training/src/headless.c's own public API -- that file has no
 * header of its own (it's a ctypes target, not something other C files #include), so this test
 * declares the functions itself, same as any other extern-linkage C consumer would. */
void sim_init(int hero0_id, int hero1_id);
void sim_reset(int hero0_id, int hero1_id);
void sim_step(float move_x, float move_z, int cast_q, int cast_w, int cast_r, unsigned int dt_ms);
int sim_get_obs(int owner, float *out_obs);
int sim_get_done(void);
int sim_get_winner(void);

#define ARENA_TRAINING_OBS_SIZE 18

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_sim_init_sets_up_two_full_health_heroes(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    float obs[ARENA_TRAINING_OBS_SIZE];
    int n = sim_get_obs(0, obs);
    CHECK(n == ARENA_TRAINING_OBS_SIZE, "sim_get_obs returns the documented fixed size");
    CHECK(obs[0] == obs[1], "self hp starts equal to self max_hp");
    CHECK(obs[10] == 1.0f, "self alive flag is 1.0 at match start");
    CHECK(obs[15] == 1.0f, "foe alive flag is 1.0 at match start");
    CHECK(sim_get_done() == 0, "a freshly-initialized match is not done");
    CHECK(sim_get_winner() == 0, "a freshly-initialized match has no winner yet");
}

static void test_obs_is_symmetric_between_owner_0_and_1(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    float obs0[ARENA_TRAINING_OBS_SIZE], obs1[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs0);
    sim_get_obs(1, obs1);
    /* owner 0's "self" fields should equal owner 1's "foe" fields, and vice versa --
       sim_get_obs is a genuine point-of-view flip, not owner-0-only. */
    CHECK(fabsf(obs0[0] - obs1[11]) < 0.01f, "owner 0's self hp matches owner 1's foe hp");
    CHECK(fabsf(obs0[3] - obs1[13]) < 0.01f, "owner 0's self x matches owner 1's foe x");
    CHECK(fabsf(obs1[0] - obs0[11]) < 0.01f, "owner 1's self hp matches owner 0's foe hp");
}

static void test_sim_step_moves_hero_toward_target(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    float obs[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs);
    float start_x = obs[3];

    sim_step(100.0f, 0.0f, 0, 0, 0, 16); /* move far to the right, one 16ms tick, no casts */

    sim_get_obs(0, obs);
    CHECK(obs[3] > start_x, "sim_step's move_x/move_z genuinely moves hero 0 toward the target");
}

static void test_repeated_steps_produce_real_combat(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    float obs[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs);
    float self_hp_before = obs[0], foe_hp_before = obs[11];

    /* Walk hero 0 toward hero 1 and keep attempting Q -- same shape a real RL rollout would
       drive, just with a fixed policy (always move right, always try Q) instead of a trained
       one. Over enough ticks the flat melee auto-attack loop (unconditional, not gated on this
       test's own Q attempts landing) should produce real damage on both sides. */
    int done = 0;
    for (int i = 0; i < 400 && !done; i++) {
        sim_step(4.0f, 0.0f, 1, 0, 0, 16);
        done = sim_get_done();
    }

    sim_get_obs(0, obs);
    CHECK(obs[0] < self_hp_before || obs[11] < foe_hp_before,
          "400 ticks of closing distance and fighting produces real HP loss on at least one side");
}

static void test_sim_reset_restores_full_health_and_clears_winner(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    for (int i = 0; i < 400 && !sim_get_done(); i++) {
        sim_step(4.0f, 0.0f, 1, 0, 0, 16);
    }

    sim_reset(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);

    float obs[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs);
    CHECK(obs[0] == obs[1], "sim_reset restores hero 0 to full HP");
    CHECK(sim_get_done() == 0, "sim_reset clears the done/winner state for a fresh episode");
    CHECK(sim_get_winner() == 0, "sim_get_winner reads 0 again after reset");
}

static void test_dx_dz_reflect_real_relative_position(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    float obs[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs);
    float expect_dx = obs[13] - obs[3]; /* foe_x - self_x */
    float expect_dz = obs[14] - obs[4];
    CHECK(fabsf(obs[16] - expect_dx) < 0.001f, "dx (index 16) is genuinely foe_x - self_x, not a stale/wrong field");
    CHECK(fabsf(obs[17] - expect_dz) < 0.001f, "dz (index 17) is genuinely foe_z - self_z");
}

int main(void) {
    test_sim_init_sets_up_two_full_health_heroes();
    test_obs_is_symmetric_between_owner_0_and_1();
    test_sim_step_moves_hero_toward_target();
    test_repeated_steps_produce_real_combat();
    test_sim_reset_restores_full_health_and_clears_winner();
    test_dx_dz_reflect_real_relative_position();

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
