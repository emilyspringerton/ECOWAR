/* duck_smoke_bomb_mod_host.h -- real extern declaration for the one host-side
 * symbol duck_smoke_bomb_mod.c's #target/inline-c body calls into, plus the
 * mod's own entry point. Same "-include this header before compiling the
 * generated C" pattern tree_passive_mod_host.h/bloodflower_mod_host.h
 * already established -- pure C linking, no cgo layer needed.
 *
 * redgarden_host_duck_smoke_bomb_cast has a real implementation in
 * arena_game.c (arena_toggle_w's own ARENA_HERO_DUCK case calls
 * on_duck_smoke_bomb_cast, which calls back into this).
 */
#ifndef DUCK_SMOKE_BOMB_MOD_HOST_H
#define DUCK_SMOKE_BOMB_MOD_HOST_H

extern void redgarden_host_duck_smoke_bomb_cast(int hero_index);
extern void on_duck_smoke_bomb_cast(int hero_index);

#endif /* DUCK_SMOKE_BOMB_MOD_HOST_H */
