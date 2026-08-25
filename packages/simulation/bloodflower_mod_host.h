/* bloodflower_mod_host.h -- real extern declaration for the one host-side
 * symbol bloodflower_mod.c's #target/inline-c body calls into, plus the
 * mod's own entry point. Same "-include this header before compiling the
 * generated C" pattern PARENA/examples/BUILD.bazel established for
 * editor_plugin_host_stubs.h and PITVIPER's scrollmod_host.h -- here it's
 * a plain #include in arena_game.c since REDGARDEN's PARENA integration
 * is pure C linking a compiled C object, no cgo layer needed (unlike
 * PITVIPER's Go host).
 *
 * redgarden_host_spawn_bloodflower has a real implementation in
 * arena_game.c (arena_tick_daynight's own moon-zenith edge-trigger calls
 * on_moon_zenith, which calls back into this).
 */
#ifndef BLOODFLOWER_MOD_HOST_H
#define BLOODFLOWER_MOD_HOST_H

extern void redgarden_host_spawn_bloodflower(int x, int z);
extern void on_moon_zenith(int x, int z);

#endif /* BLOODFLOWER_MOD_HOST_H */
