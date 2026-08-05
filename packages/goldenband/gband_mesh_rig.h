// gband_mesh_rig.h — S144-07: real vertex-weighted skinned mesh, driven by
// the same GOLDENBAND .gband clips S144-06's box-rig already uses, now
// deforming a continuous mesh (loaded from .gskel/.gmesh) instead of one
// rigid cube per joint. See
// GoblinFoxDragon/docs2/GOLDENBAND_INTEGRATION_NORTHSTAR.md §2 Phase 2.
//
// Deliberately a separate module from gband_rig.c (the box-rig) -- that
// code is proven and live; this is new and unproven until a real Blender
// asset exists, so it stays fully independent rather than risking any
// change to what's already working.
//
// CPU skinning, deliberately (see the northstar doc for why): this module
// computes fully-skinned, flattened (non-indexed) triangle-list vertex data
// each frame -- pos.xyz + normal.xyz per vertex, exactly matching
// CUBE_VERTS's own interleaved layout -- and hands it to the caller via a
// callback, the same "never touch main.c's GL function pointers directly"
// boundary gband_rig.h already established.
#ifndef GOLDENBAND_GBAND_MESH_RIG_H
#define GOLDENBAND_GBAND_MESH_RIG_H

#include "../common/mat4.h"

// gband_mesh_rig_init loads <asset_dir>/<mesh_name>.gskel + .gmesh, plus
// tyler_idle.gband/tyler_walk.gband from asset_dir (same clips S144-06's
// box-rig uses -- they bind by joint order, Hips/Spine/Head/L_Arm/R_Arm,
// which any 5-joint humanoid rig built on GOLDENBAND's own armature
// template shares, real character or synthetic test alike). Returns 1 on
// success, 0 on failure -- callers should fall back to gband_rig.c's
// box-rig (or the plain box beneath that) on failure, never draw nothing.
int gband_mesh_rig_init(const char *asset_dir, const char *mesh_name);

void gband_mesh_rig_shutdown(void);

int gband_mesh_rig_ready(void);

// gband_mesh_rig_draw animates, skins, and draws the mesh for one hero slot
// (independent animation clock per slot, same convention as gband_rig.c).
// draw_skinned receives this frame's flattened pos+normal vertex data
// (vert_count vertices, 6 floats each) plus the already-computed mvp/model
// matrices -- the caller uploads it to a dynamic VBO and draws, the same
// division of responsibility gband_rig.h's draw_mesh_fn callback has.
void gband_mesh_rig_draw(int hero_slot, float hero_x, float hero_z, float facing_rad, float dt_ms,
                          const Mat4 *vp,
                          void (*draw_skinned)(const float *verts6, int vert_count,
                                                const Mat4 *mvp, const Mat4 *model));

#endif // GOLDENBAND_GBAND_MESH_RIG_H
