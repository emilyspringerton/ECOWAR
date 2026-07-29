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
void sim_step_both(float move_x0, float move_z0, int cast_q0, int cast_w0, int cast_r0,
                    float move_x1, float move_z1, int cast_q1, int cast_w1, int cast_r1,
                    unsigned int dt_ms);
int sim_get_obs(int owner, float *out_obs);
int sim_get_done(void);
int sim_get_winner(void);
void sim_set_hero_position(int owner, float x, float z);

/* Mirrors headless.c's own #define exactly -- see that file's own doc comment for the layout
   (18 scalar fields + one-hot self hero_id + one-hot foe hero_id). */
#define ARENA_TRAINING_OBS_SIZE (18 + 2 * ARENA_HERO_COUNT)

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

/* sim_set_hero_position (2026-07-29, NORTHSTAR §21 follow-up): scripts/rl_env.py's own reset()
 * calls this right after sim_reset to randomize spawn positions across the real map, fixing the
 * coordinate-frame mismatch found while wiring the first trained policy into apps/arena_bot's
 * real networked bots (REDGARDEN Apple #11301) -- the old fixed -6/+6 spawns never taught the
 * policy anything about combat away from map center. */
static void test_sim_set_hero_position_relocates_and_updates_obs(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    sim_set_hero_position(0, 30.0f, -20.0f);
    sim_set_hero_position(1, 40.0f, -12.0f);

    float obs[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs);
    CHECK(fabsf(obs[3] - 30.0f) < 0.001f, "sim_set_hero_position actually moves self x, not just target_x");
    CHECK(fabsf(obs[4] - (-20.0f)) < 0.001f, "and self z");
    CHECK(fabsf(obs[13] - 40.0f) < 0.001f, "and the foe's x, reflected through the same snapshot");
    CHECK(fabsf(obs[14] - (-12.0f)) < 0.001f, "and the foe's z");

    float expect_dx = 40.0f - 30.0f, expect_dz = -12.0f - (-20.0f);
    CHECK(fabsf(obs[16] - expect_dx) < 0.001f, "dx recomputes correctly off the relocated positions, not a stale pre-move value");
    CHECK(fabsf(obs[17] - expect_dz) < 0.001f, "and dz");
}

/* Hero one-hot blocks (2026-07-29, founder: "not just 2 heroes"). See
   ARENA_TRAINING_OBS_SIZE's own doc comment in headless.c for the full "why one-hot" reasoning. */
static void test_hero_onehot_blocks_are_correct_and_disjoint(void) {
    sim_init(ARENA_HERO_GARY, ARENA_HERO_ZAGAN);
    float obs[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs);

    for (int i = 0; i < ARENA_HERO_COUNT; i++) {
        float expect_self = (i == ARENA_HERO_GARY) ? 1.0f : 0.0f;
        float expect_foe = (i == ARENA_HERO_ZAGAN) ? 1.0f : 0.0f;
        if (obs[18 + i] != expect_self) {
            printf("FAIL: self hero one-hot index %d = %f, want %f\n", i, obs[18 + i], expect_self);
            failures++;
        }
        if (obs[18 + ARENA_HERO_COUNT + i] != expect_foe) {
            printf("FAIL: foe hero one-hot index %d = %f, want %f\n", i, obs[18 + ARENA_HERO_COUNT + i], expect_foe);
            failures++;
        }
    }
    printf("PASS: self hero one-hot is 1.0 at ARENA_HERO_GARY and 0.0 everywhere else\n");
    printf("PASS: foe hero one-hot is 1.0 at ARENA_HERO_ZAGAN and 0.0 everywhere else\n");

    /* Flip perspective -- owner 1's own "self" one-hot should be Zagan's, its "foe" one-hot
       should be Gary's, same symmetry test_obs_is_symmetric_between_owner_0_and_1 already
       exercises for the scalar fields. */
    float obs1[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(1, obs1);
    CHECK(obs1[18 + ARENA_HERO_ZAGAN] == 1.0f, "owner 1's own self one-hot is Zagan (the hero it's actually playing)");
    CHECK(obs1[18 + ARENA_HERO_COUNT + ARENA_HERO_GARY] == 1.0f, "owner 1's foe one-hot is Gary");
}

/* sim_step_both (2026-07-29, self-play): real self-play needs BOTH heroes driven by external
   actions, not the stable heuristic sim_step's own internal call drives hero 1 with. */
static void test_sim_step_both_moves_both_heroes_independently(void) {
    sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    float obs0[ARENA_TRAINING_OBS_SIZE], obs1[ARENA_TRAINING_OBS_SIZE];
    sim_get_obs(0, obs0);
    sim_get_obs(1, obs1);
    float hero0_start_x = obs0[3], hero1_start_x = obs1[3];

    /* hero 0 moves right (+x), hero 1 moves further right too (away from hero 0, +x from its
       own +6 starting position) -- two genuinely different, externally-driven targets, neither
       coming from the heuristic. */
    sim_step_both(100.0f, 0.0f, 0, 0, 0, 100.0f, 0.0f, 0, 0, 0, 16);

    sim_get_obs(0, obs0);
    sim_get_obs(1, obs1);
    CHECK(obs0[3] > hero0_start_x, "sim_step_both moves hero 0 toward its own given target");
    CHECK(obs1[3] > hero1_start_x, "sim_step_both ALSO moves hero 1 toward its own given target -- not driven by the stable heuristic");
}

int main(void) {
    test_sim_init_sets_up_two_full_health_heroes();
    test_obs_is_symmetric_between_owner_0_and_1();
    test_sim_step_moves_hero_toward_target();
    test_repeated_steps_produce_real_combat();
    test_sim_reset_restores_full_health_and_clears_winner();
    test_dx_dz_reflect_real_relative_position();
    test_sim_set_hero_position_relocates_and_updates_obs();
    test_hero_onehot_blocks_are_correct_and_disjoint();
    test_sim_step_both_moves_both_heroes_independently();

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
