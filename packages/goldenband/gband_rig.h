// gband_rig.h — S144-06: the GOLDENBAND-driven procedural box-rig proof.
//
// Drives a small hardcoded 5-joint skeleton (Hips -> Spine -> {Head, L_Arm,
// R_Arm}) from real .gband clips (tyler_idle.gband / tyler_walk.gband,
// produced by the real, already-shipped `gbtool import --bvh` — see
// assets/goldenband/src/*.bvh) through real forward kinematics, and draws
// one existing unit cube per joint. See
// GoblinFoxDragon/docs2/GOLDENBAND_INTEGRATION_NORTHSTAR.md for the full
// plan this implements (§2 Phase 1 / S144-06).
//
// Deliberately not a general skeleton/mesh system (see the northstar's
// Phase 2 for that, blocked on a real Blender asset) -- this is a fixed,
// hardcoded 5-joint rig, matched exactly to tyler_idle.bvh/tyler_walk.bvh's
// own hierarchy and channel order. Changing the skeleton means changing
// both this file and those two BVH files together.
#ifndef GOLDENBAND_GBAND_RIG_H
#define GOLDENBAND_GBAND_RIG_H

#include "../common/mat4.h"

// gband_rig_init loads tyler_idle.gband / tyler_walk.gband from
// asset_dir (e.g. "assets/goldenband"). Returns 1 on success, 0 on failure
// (missing/corrupt files) -- callers should fall back to the plain box
// silhouette on failure, never crash the client over a missing test asset.
int gband_rig_init(const char *asset_dir);

void gband_rig_shutdown(void);

// gband_rig_ready reports whether gband_rig_init succeeded -- callers should
// fall back to their normal (non-animated) rendering when this is false,
// never render nothing.
int gband_rig_ready(void);

// gband_rig_draw animates and draws the rig for one hero slot (0..ARENA_MAX_HEROES-1,
// used only to track that slot's own animation clock/previous-position state
// independently of every other slot -- e.g. Tyler and a Tyler clone animate
// out of phase with each other, not in lockstep).
//
// hero_x/hero_z/facing_rad match draw_hero_model's own hero-placement
// convention exactly. dt_ms is the real frame delta time (same `dt` main.c
// already threads into squish_age_ms).
//
// cube_mesh is passed through opaquely (this file never dereferences it --
// only main.c's own Mesh definition does) specifically so this header never
// has to redeclare main.c's `Mesh` struct and risk a conflicting-type error
// across translation units. set_mvp_model/draw_mesh_fn are main.c's own
// uniform-set and draw_mesh, passed in as callbacks so this file never needs
// to link against main.c's dynamically-loaded OpenGL function pointers
// directly.
void gband_rig_draw(int hero_slot, float hero_x, float hero_z, float facing_rad, float dt_ms,
                     const Mat4 *vp, void (*set_mvp_model)(const Mat4 *mvp, const Mat4 *model),
                     void (*draw_mesh_fn)(const void *m), const void *cube_mesh);

#endif // GOLDENBAND_GBAND_RIG_H
