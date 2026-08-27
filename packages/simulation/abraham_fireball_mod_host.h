/* abraham_fireball_mod_host.h -- real extern declaration for the one
 * host-side symbol abraham_fireball_mod.c's #target/inline-c body calls
 * into, plus the mod's own entry point. Same "-include this header before
 * compiling the generated C" pattern tree_passive_mod_host.h/
 * bloodflower_mod_host.h/duck_smoke_bomb_mod_host.h already established --
 * pure C linking, no cgo layer needed.
 *
 * redgarden_host_abraham_fireball_cast has a real implementation in
 * arena_game.c (arena_toggle_w's own ARENA_HERO_ABRAHAM case calls
 * on_abraham_fireball_cast, which calls back into this). target_x/target_z
 * are I32 (rounded click-point world coordinates) -- VS0 doesn't support
 * F32 mod parameters yet, same reason bloodflower_mod_host.h's
 * on_moon_zenith uses I32 x/z.
 */
#ifndef ABRAHAM_FIREBALL_MOD_HOST_H
#define ABRAHAM_FIREBALL_MOD_HOST_H

extern void redgarden_host_abraham_fireball_cast(int hero_index, int target_x, int target_z);
extern void on_abraham_fireball_cast(int hero_index, int target_x, int target_z);

#endif /* ABRAHAM_FIREBALL_MOD_HOST_H */
