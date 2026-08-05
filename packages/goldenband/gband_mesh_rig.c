// gband_mesh_rig.c — see gband_mesh_rig.h. Real CPU linear-blend skinning:
// samples the same tyler_idle.gband/tyler_walk.gband clips gband_rig.c
// already uses, runs the same forward-kinematics shape against a skeleton
// loaded from a real .gskel file (not hardcoded, unlike gband_rig.c --
// this module proves the general loader path), then blends the loaded
// .gmesh's real per-vertex weights into a flat, ready-to-draw triangle
// list. Originally proven against a synthetic test mesh
// (synthetic_body.gskel/.gmesh, still in assets/goldenband/ for
// regression-checking); now also drives the real founder-modeled
// tyler_body.gskel/.gmesh, same code path, mesh_name just picks which.
#include "gband_mesh_rig.h"
#include "gband.h"
#include "gskel.h"
#include "gmesh.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Same channel-index convention as gband_rig.c, and for the same reason:
 * tyler_idle.gband/tyler_walk.gband's channel order is fixed at BVH-import
 * time (see the .bvh files under assets/goldenband/src), and synthetic_body.gskel's own
 * joint order (see gen_synthetic_body.py's JOINTS list) matches it exactly
 * -- both hand-authored together. A truly general system would bind
 * channels to joints by name at load time instead of by hardcoded index;
 * deferred until a second, differently-ordered skeleton actually exists to
 * generalize against (same "don't build for a hypothetical" stance the
 * rest of this codebase already takes). */
#define HIPS_TX 0
#define HIPS_TY 1
#define HIPS_TZ 2
#define HIPS_RX 3
#define HIPS_RY 4
#define HIPS_RZ 5
static const int JOINT_ROT_CHAN[5][3] = {
    {HIPS_RX, HIPS_RY, HIPS_RZ},
    { 6,  7,  8}, /* Spine */
    { 9, 10, 11}, /* Head */
    {12, 13, 14}, /* L_Arm */
    {15, 16, 17}, /* R_Arm */
};
#define NUM_CHANNELS 18
#define MAX_JOINTS 5 /* matches GOLDENBAND's own 5-bone armature template; asserted at load time */

static Mat4 gband_mesh_rotate_x(float rad) {
    Mat4 r = mat4_identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
static Mat4 gband_mesh_rotate_z(float rad) {
    Mat4 r = mat4_identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}
#define DEG2RAD(d) ((d) * 3.14159265358979323846f / 180.0f)

/* mat4_transform_point/vector: this renderer has never needed CPU-side
 * point transforms before (every existing draw call hands matrices to the
 * shader and lets the GPU do it) -- skinning is the first CPU-side use, so
 * these are new, self-contained helpers, not additions to the shared
 * mat4.h (same reasoning gband_rig.c's own rotate_x/rotate_z gave). */
static void mat4_transform_point(const Mat4 *m, const float p[3], float out[3]) {
    for (int row = 0; row < 3; row++) {
        out[row] = m->m[0*4+row]*p[0] + m->m[1*4+row]*p[1] + m->m[2*4+row]*p[2] + m->m[3*4+row]*1.0f;
    }
}
static void mat4_transform_vector(const Mat4 *m, const float v[3], float out[3]) {
    for (int row = 0; row < 3; row++) {
        out[row] = m->m[0*4+row]*v[0] + m->m[1*4+row]*v[1] + m->m[2*4+row]*v[2];
    }
}

static GBClip g_idle_clip, g_walk_clip;
static GSkel g_skel;
static GMesh g_mesh;
static int g_ready = 0;

static float *g_out_buf; /* owned, 6 floats per output vertex (pos+normal), 3 verts per triangle */
static int g_out_capacity_tris;

int gband_mesh_rig_init(const char *asset_dir, const char *mesh_name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/tyler_idle.gband", asset_dir);
    if (!gb_init(path, &g_idle_clip)) return 0;
    snprintf(path, sizeof(path), "%s/tyler_walk.gband", asset_dir);
    if (!gb_init(path, &g_walk_clip)) { gb_free(&g_idle_clip); return 0; }
    snprintf(path, sizeof(path), "%s/%s.gskel", asset_dir, mesh_name);
    if (!gskel_init(path, &g_skel)) { gb_free(&g_idle_clip); gb_free(&g_walk_clip); return 0; }
    snprintf(path, sizeof(path), "%s/%s.gmesh", asset_dir, mesh_name);
    if (!gmesh_init(path, &g_mesh)) { gb_free(&g_idle_clip); gb_free(&g_walk_clip); return 0; }

    if (g_skel.joint_count != MAX_JOINTS) {
        /* Hardcoded channel table above only covers this exact rig -- fail
           loudly-but-softly (no draw) rather than skin against the wrong
           joints if a differently-shaped .gskel ever lands here. */
        gb_free(&g_idle_clip); gb_free(&g_walk_clip); gmesh_free(&g_mesh);
        return 0;
    }

    g_out_capacity_tris = (int)(g_mesh.index_count / 3);
    g_out_buf = (float *)malloc((size_t)g_out_capacity_tris * 3 * 6 * sizeof(float));
    if (!g_out_buf) { gb_free(&g_idle_clip); gb_free(&g_walk_clip); gmesh_free(&g_mesh); return 0; }

    g_ready = 1;
    return 1;
}

void gband_mesh_rig_shutdown(void) {
    if (!g_ready) return;
    gb_free(&g_idle_clip);
    gb_free(&g_walk_clip);
    gmesh_free(&g_mesh);
    free(g_out_buf);
    g_out_buf = NULL;
    g_ready = 0;
}

int gband_mesh_rig_ready(void) { return g_ready; }

#define MAX_SLOTS 64
static float g_clock_ticks[MAX_SLOTS];
static float g_prev_x[MAX_SLOTS];
static float g_prev_z[MAX_SLOTS];
static int   g_has_prev[MAX_SLOTS];
#define MOVE_EPSILON 0.02f

void gband_mesh_rig_draw(int hero_slot, float hero_x, float hero_z, float facing_rad, float dt_ms,
                          const Mat4 *vp,
                          void (*draw_skinned)(const float *verts6, int vert_count,
                                                const Mat4 *mvp, const Mat4 *model)) {
    if (!g_ready) return;
    if (hero_slot < 0 || hero_slot >= MAX_SLOTS) return;

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

    Mat4 world[MAX_JOINTS];
    Mat4 skin[MAX_JOINTS];
    for (uint32_t j = 0; j < g_skel.joint_count; j++) {
        const GSkelJoint *joint = &g_skel.joints[j];
        float rx = DEG2RAD(sampled[JOINT_ROT_CHAN[j][0]]);
        float ry = DEG2RAD(sampled[JOINT_ROT_CHAN[j][1]]);
        float rz = DEG2RAD(sampled[JOINT_ROT_CHAN[j][2]]);
        Mat4 rot_x = gband_mesh_rotate_x(rx);
        Mat4 rot_y = mat4_rotate_y(ry);
        Mat4 rot_z = gband_mesh_rotate_z(rz);
        Mat4 rot_zy = mat4_multiply(&rot_z, &rot_y);
        Mat4 rot = mat4_multiply(&rot_zy, &rot_x);

        Mat4 local_t;
        if (joint->parent_index == -1 && j == 0) {
            /* Hips: animated translation, same convention as gband_rig.c
               (root carries real position channels, standard BVH shape). */
            local_t = mat4_translate(sampled[HIPS_TX], sampled[HIPS_TY], sampled[HIPS_TZ]);
        } else {
            local_t = mat4_translate(joint->rest_translation[0], joint->rest_translation[1], joint->rest_translation[2]);
        }
        Mat4 local = mat4_multiply(&local_t, &rot);

        const Mat4 *parent_world = (joint->parent_index == -1) ? &hero_world : &world[joint->parent_index];
        world[j] = mat4_multiply(parent_world, &local);

        Mat4 inv_bind;
        memcpy(inv_bind.m, joint->inverse_bind, sizeof(inv_bind.m));
        skin[j] = mat4_multiply(&world[j], &inv_bind);
    }

    int tri_count = (int)(g_mesh.index_count / 3);
    if (tri_count > g_out_capacity_tris) tri_count = g_out_capacity_tris; /* defensive, shouldn't trigger */

    int out_i = 0;
    for (int t = 0; t < tri_count; t++) {
        for (int c = 0; c < 3; c++) {
            uint32_t vi = g_mesh.indices[t * 3 + c];
            const GMeshVertex *src = &g_mesh.vertices[vi];

            float pos[3] = {0, 0, 0};
            float nrm[3] = {0, 0, 0};
            for (int k = 0; k < 4; k++) {
                float weight = src->bone_weights[k];
                if (weight <= 0.0f) continue;
                uint8_t bi = src->bone_indices[k];
                float p[3], n[3];
                mat4_transform_point(&skin[bi], src->position, p);
                mat4_transform_vector(&skin[bi], src->normal, n);
                pos[0] += weight * p[0]; pos[1] += weight * p[1]; pos[2] += weight * p[2];
                nrm[0] += weight * n[0]; nrm[1] += weight * n[1]; nrm[2] += weight * n[2];
            }
            float nlen = sqrtf(nrm[0]*nrm[0] + nrm[1]*nrm[1] + nrm[2]*nrm[2]);
            if (nlen > 1e-6f) { nrm[0] /= nlen; nrm[1] /= nlen; nrm[2] /= nlen; }

            g_out_buf[out_i++] = pos[0]; g_out_buf[out_i++] = pos[1]; g_out_buf[out_i++] = pos[2];
            g_out_buf[out_i++] = nrm[0]; g_out_buf[out_i++] = nrm[1]; g_out_buf[out_i++] = nrm[2];
        }
    }

    /* Vertices are already skinned into world space (skin[j] already
       includes hero_world), so the "model" matrix for this draw is
       identity -- mvp collapses to just vp. */
    Mat4 identity = mat4_identity();
    Mat4 mvp = mat4_multiply(vp, &identity);
    draw_skinned(g_out_buf, tri_count * 3, &mvp, &identity);
}
