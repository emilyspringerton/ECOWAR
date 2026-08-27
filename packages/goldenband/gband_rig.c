// gband_rig.c — see gband_rig.h. S144-06: a hardcoded 5-joint skeleton,
// animated by real GOLDENBAND .gband clips, drawn as one cube per joint.
//
// The 5 joints (indices below) and the 18 channel indices are matched
// exactly to assets/goldenband/src/tyler_idle.bvh and tyler_walk.bvh's own
// HIERARCHY block -- gbtool preserves BVH declaration order 1:1 into the
// binary's channel_data columns (confirmed by reading the manifest's
// "channels" array after import), so these indices are not guessed, they're
// read off the real generated tyler_walk.gband.json.
#include "gband_rig.h"
#include "gband.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

#define HIPS   0
#define SPINE  1
#define HEAD   2
#define L_ARM  3
#define R_ARM  4
#define NUM_JOINTS 5

static const int JOINT_PARENT[NUM_JOINTS] = { -1, HIPS, SPINE, SPINE, SPINE };

/* Rest-pose local translation from parent (BVH OFFSET, hand-duplicated here
 * since gbtool discards OFFSET on import -- see northstar §1). Unused for
 * HIPS, whose local translation comes from animated Xposition/Yposition/
 * Zposition channels instead (standard BVH convention: only the root
 * carries translation channels). */
static const float JOINT_REST_OFFSET[NUM_JOINTS][3] = {
    { 0.0f,  0.0f,  0.0f}, /* HIPS (unused) */
    { 0.0f,  0.35f, 0.0f}, /* SPINE, from HIPS */
    { 0.0f,  0.30f, 0.0f}, /* HEAD, from SPINE */
    {-0.45f, 0.0f,  0.0f}, /* L_ARM, from SPINE */
    { 0.45f, 0.0f,  0.0f}, /* R_ARM, from SPINE */
};

/* Channel indices into the 18-float sampled/blended channel array. */
#define HIPS_TX 0
#define HIPS_TY 1
#define HIPS_TZ 2
#define HIPS_RX 3
#define HIPS_RY 4
#define HIPS_RZ 5
static const int JOINT_ROT_CHAN[NUM_JOINTS][3] = {
    {HIPS_RX, HIPS_RY, HIPS_RZ},
    { 6,  7,  8}, /* SPINE */
    { 9, 10, 11}, /* HEAD */
    {12, 13, 14}, /* L_ARM */
    {15, 16, 17}, /* R_ARM */
};
#define NUM_CHANNELS 18

/* Box render params: local offset (applied at draw time only, never
 * propagated to children -- a joint's own world transform stays the real
 * pivot for its children regardless of how its box is drawn) + scale.
 * Arms hang from the shoulder pivot rather than being centered on it. */
static const float JOINT_BOX_OFFSET[NUM_JOINTS][3] = {
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, -0.25f, 0}, {0, -0.25f, 0},
};
static const float JOINT_BOX_SCALE[NUM_JOINTS][3] = {
    {0.5f, 0.4f, 0.4f},   /* HIPS  */
    {0.5f, 0.5f, 0.35f},  /* SPINE */
    {0.35f,0.35f,0.35f},  /* HEAD  */
    {0.18f,0.5f, 0.18f},  /* L_ARM */
    {0.18f,0.5f, 0.18f},  /* R_ARM */
};

/* Self-contained rotation helpers -- deliberately NOT added to the shared
 * packages/common/mat4.h (which only has mat4_rotate_y, the one rotation
 * every other caller in this codebase has ever needed). Same column-major/
 * right-handed convention as mat4_rotate_y (see that function's own doc
 * comment), derived the same way, kept local to avoid any risk to existing
 * callers of the shared header. */
static Mat4 gband_rotate_x(float rad) {
    Mat4 r = mat4_identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
static Mat4 gband_rotate_z(float rad) {
    Mat4 r = mat4_identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}
#define DEG2RAD(d) ((d) * 3.14159265358979323846f / 180.0f)

static GBClip g_idle_clip, g_walk_clip;
static int g_ready = 0;

int gband_rig_init(const char *asset_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/tyler_idle.gband", asset_dir);
    if (!gb_init(path, &g_idle_clip)) return 0;
    snprintf(path, sizeof(path), "%s/tyler_walk.gband", asset_dir);
    if (!gb_init(path, &g_walk_clip)) {
        gb_free(&g_idle_clip);
        return 0;
    }
    g_ready = 1;
    return 1;
}

void gband_rig_shutdown(void) {
    if (!g_ready) return;
    gb_free(&g_idle_clip);
    gb_free(&g_walk_clip);
    g_ready = 0;
}

int gband_rig_ready(void) { return g_ready; }

/* Per-hero-slot animation state, independent per slot so e.g. a Tyler clone
 * doesn't animate in lockstep with the real Tyler. */
#define GBAND_RIG_MAX_SLOTS 64
static float g_clock_ticks[GBAND_RIG_MAX_SLOTS];
static float g_prev_x[GBAND_RIG_MAX_SLOTS];
static float g_prev_z[GBAND_RIG_MAX_SLOTS];
static int   g_has_prev[GBAND_RIG_MAX_SLOTS];

#define MOVE_EPSILON 0.02f

void gband_rig_draw(int hero_slot, float hero_x, float hero_z, float facing_rad, float dt_ms,
                     const Mat4 *vp, void (*set_mvp_model)(const Mat4 *mvp, const Mat4 *model),
                     void (*draw_mesh_fn)(const void *m), const void *cube_mesh) {
    if (!g_ready) return;
    if (hero_slot < 0 || hero_slot >= GBAND_RIG_MAX_SLOTS) return;

    /* moved-this-frame -> walk vs idle. Deliberately no smoothing/hysteresis
     * (a known MVP simplification, see northstar): picks per-frame from raw
     * position delta, same signal update_facing_from_motion already uses
     * for facing, just re-derived locally so this file stays decoupled from
     * main.c's own facing-tracking arrays. */
    int walking = 0;
    if (g_has_prev[hero_slot]) {
        float mdx = hero_x - g_prev_x[hero_slot];
        float mdz = hero_z - g_prev_z[hero_slot];
        walking = (mdx * mdx + mdz * mdz) > (MOVE_EPSILON * MOVE_EPSILON);
    }
    g_prev_x[hero_slot] = hero_x;
    g_prev_z[hero_slot] = hero_z;
    g_has_prev[hero_slot] = 1;

    const GBClip *clip = walking ? &g_walk_clip : &g_idle_clip;

    g_clock_ticks[hero_slot] += (dt_ms / 1000.0f) * (float)clip->tick_rate;
    /* wrap into [0, duration_ticks) -- fmodf can return a value with the sign
     * of clock_ticks, but clock_ticks only ever increases here, so it's
     * always non-negative and this is safe. */
    g_clock_ticks[hero_slot] = fmodf(g_clock_ticks[hero_slot], (float)clip->duration_ticks);

    float whole = floorf(g_clock_ticks[hero_slot]);
    float w = g_clock_ticks[hero_slot] - whole;
    uint32_t tick0 = (uint32_t)whole;
    uint32_t tick1 = (tick0 + 1) % clip->duration_ticks;

    float sampled[NUM_CHANNELS];
    gb_blend(clip, tick0, tick1, w, sampled);

    Mat4 hero_world_t = mat4_translate(hero_x, 0.0f, hero_z);
    Mat4 hero_rot = mat4_rotate_y(facing_rad);
    Mat4 hero_world = mat4_multiply(&hero_world_t, &hero_rot);

    Mat4 world[NUM_JOINTS];
    for (int j = 0; j < NUM_JOINTS; j++) {
        float rx = DEG2RAD(sampled[JOINT_ROT_CHAN[j][0]]);
        float ry = DEG2RAD(sampled[JOINT_ROT_CHAN[j][1]]);
        float rz = DEG2RAD(sampled[JOINT_ROT_CHAN[j][2]]);
        Mat4 rot_x = gband_rotate_x(rx);
        Mat4 rot_y = mat4_rotate_y(ry);
        Mat4 rot_z = gband_rotate_z(rz);
        Mat4 rot_zy = mat4_multiply(&rot_z, &rot_y);
        Mat4 rot = mat4_multiply(&rot_zy, &rot_x); /* apply X, then Y, then Z */

        Mat4 local_t;
        if (j == HIPS) {
            local_t = mat4_translate(sampled[HIPS_TX], sampled[HIPS_TY], sampled[HIPS_TZ]);
        } else {
            local_t = mat4_translate(JOINT_REST_OFFSET[j][0], JOINT_REST_OFFSET[j][1], JOINT_REST_OFFSET[j][2]);
        }
        Mat4 local = mat4_multiply(&local_t, &rot);

        const Mat4 *parent_world = (JOINT_PARENT[j] == -1) ? &hero_world : &world[JOINT_PARENT[j]];
        world[j] = mat4_multiply(parent_world, &local);
    }

    for (int j = 0; j < NUM_JOINTS; j++) {
        Mat4 box_t = mat4_translate(JOINT_BOX_OFFSET[j][0], JOINT_BOX_OFFSET[j][1], JOINT_BOX_OFFSET[j][2]);
        Mat4 box_s = mat4_scale(JOINT_BOX_SCALE[j][0], JOINT_BOX_SCALE[j][1], JOINT_BOX_SCALE[j][2]);
        Mat4 box_local = mat4_multiply(&box_t, &box_s);
        Mat4 model = mat4_multiply(&world[j], &box_local);
        Mat4 mvp = mat4_multiply(vp, &model);
        set_mvp_model(&mvp, &model);
        draw_mesh_fn(cube_mesh);
    }
}
