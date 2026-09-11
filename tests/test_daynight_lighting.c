/* tests/test_daynight_lighting.c -- headless smoke test for arena_daynight_light_dir
 * (BACKLOG.md SECTION 380, founder: "i love the way the lighting shifts from day to night in
 * shankpit"), the real, moving 3D scene light direction ported from SHANKPIT retro_lighting.c's
 * own "sun by day, moon by night" convention. Same "no SDL/GL dependency" reasoning as
 * test_arena_game.c's own header comment -- this only tests the real math, not the actual
 * rendered frame. */
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_light_dir_is_always_above_the_horizon(void) {
    memset(&arena_state, 0, sizeof(arena_state));

    int all_above_horizon = 1;
    for (int ms = 0; ms < 260000; ms += 500) {
        arena_state.time_of_day_sec = (float)ms / 1000.0f;
        float x, y, z;
        arena_daynight_light_dir(&x, &y, &z);
        if (y < 0.0f) all_above_horizon = 0;
    }
    CHECK(all_above_horizon, "the real light source (sun by day, moon by night) is always at or above the horizon -- the scene is never lit from below");
}

static void test_light_dir_actually_changes_over_time(void) {
    memset(&arena_state, 0, sizeof(arena_state));

    arena_state.time_of_day_sec = 0.0f;
    float x0, y0, z0;
    arena_daynight_light_dir(&x0, &y0, &z0);

    arena_state.time_of_day_sec = 60.0f;
    float x1, y1, z1;
    arena_daynight_light_dir(&x1, &y1, &z1);

    CHECK(x0 != x1 || y0 != y1 || z0 != z1, "the real light direction actually moves as time_of_day_sec advances -- not a frozen constant");
}

static void test_light_dir_flips_to_the_moon_at_night(void) {
    memset(&arena_state, 0, sizeof(arena_state));

    /* Analytical zenith (same real math test_bloodflower.c's own EXPECTED_ZENITH_MS derives):
       orbit_t = t * ARENA_DAYNIGHT_ORBIT_SPEED = 1.5*PI is the moon's own real peak, sun_height's
       first local minimum -- deep night, the real half of the cycle sun_y < 0 and the moon
       direction (the negated sun vector) is the real light source. */
    float night_t = (1.5f * (float)M_PI) / ARENA_DAYNIGHT_ORBIT_SPEED;
    arena_state.time_of_day_sec = night_t;

    float sun_x = cosf(night_t * ARENA_DAYNIGHT_ORBIT_SPEED);
    float sun_y = sinf(night_t * ARENA_DAYNIGHT_ORBIT_SPEED) * cosf(ARENA_DAYNIGHT_TILT);
    CHECK(sun_y < 0.0f, "setup: the analytical sun really is below the horizon at this real moment");

    float x, y, z;
    arena_daynight_light_dir(&x, &y, &z);
    CHECK(y > 0.0f, "at deep night, the real light direction is the moon (above horizon), not the sun (which would be below)");
    CHECK(fabsf(x - (-sun_x)) < 0.001f, "the real moon direction is exactly the negated sun direction, matching SHANKPIT's own dynamic-lighting convention");
}

static void test_light_dir_matches_the_sun_at_midday(void) {
    memset(&arena_state, 0, sizeof(arena_state));

    /* orbit_t = PI/2 is the sun's own real zenith (sin=1, maximum sun_height). */
    float midday_t = ((float)M_PI / 2.0f) / ARENA_DAYNIGHT_ORBIT_SPEED;
    arena_state.time_of_day_sec = midday_t;

    float x, y, z;
    arena_daynight_light_dir(&x, &y, &z);
    CHECK(y > 0.9f, "at real midday, the light direction is near the sun's own real peak height");
}

int main(void) {
    test_light_dir_is_always_above_the_horizon();
    test_light_dir_actually_changes_over_time();
    test_light_dir_flips_to_the_moon_at_night();
    test_light_dir_matches_the_sun_at_midday();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
