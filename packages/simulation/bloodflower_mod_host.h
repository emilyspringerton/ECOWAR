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

/* REFLUX (2026-09-11, EMILY/BACKLOG.md SECTION 380): on_moon_zenith's own generated body now
 * also calls reflux_dispatch(REFLUX_ACTION_BLOODFLOWER_TRIGGERED, ...) -- pulling in
 * reflux_mod_host.h here (a plain C #include, not a new -include flag anywhere) is the real,
 * minimal way to make both the real reflux_dispatch prototype (reflux_mod_host.h) and the
 * REFLUX_ACTION_BLOODFLOWER_TRIGGERED constant (reflux_runtime.h, pulled in transitively) visible
 * to bloodflower_mod.c without touching every build script's own hardcoded -include list a
 * second time. Every binary that already links bloodflower_mod.c now also needs reflux_mod.c +
 * reflux_runtime.c in its real source list -- see this repo's own build scripts. */
#include "../reflux/reflux_mod_host.h"

extern void redgarden_host_spawn_bloodflower(int x, int z);
extern void on_moon_zenith(int x, int z);

#endif /* BLOODFLOWER_MOD_HOST_H */
