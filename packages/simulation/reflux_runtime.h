/* packages/simulation/reflux_runtime.h -- REFLUX, the real cross-mod pub/sub layer (BACKLOG.md
 * SECTION 377/380, founder: "have the spawning in hostiles be a mod too with cross mod pub sub
 * communications (THINK REDUX HOWEVER REDUX WORKS) call the mod itself REFLUX a mod that presents
 * a new layer of mod interface for cross mod communication - give it its own stdlib in PARENA").
 *
 * Real, checked-first constraint this design works within: VS0 (PARENA's current compiler) has no
 * function pointers or closures (see docs/NORTHSTAR_LIVING_MAP.md's own "Mod event model,
 * honestly" section for the full accounting) -- a true Redux-style callback/subscriber-list
 * dispatch table isn't buildable at the language level today. What IS real and buildable, and
 * what this file is: a single, real, shared, append-only action log ANY mod can DISPATCH into and
 * ANY OTHER mod can POLL from (tracking its own last-seen cursor), the exact same "shared log,
 * not push callbacks" shape `packages/livingmap/living_map_events.h` already proved out for the
 * Living Map specifically -- REFLUX is that same idea generalized into a real, standalone,
 * cross-mod, cross-SYSTEM primitive (not tied to towns/creeps at all). This is genuine, real
 * decoupling: a dispatching mod (e.g. bloodflower_mod.prn) never needs to know who, if anyone, is
 * listening; a polling mod (e.g. bloodflower_hostile_spawner_mod.prn) never needs the host to
 * hand-wire a bespoke call site for it the way every OTHER mod in this repo needs today.
 *
 * Real, honest, arena-scoped for now: this runtime lives in packages/simulation (one global log
 * per match, alongside arena_state) because ECOWAR is REFLUX's only real consumer today. If a
 * second game ever wants it, extracting this into its own standalone package (no arena_game.h
 * dependency at all -- this file already has none) is a real, cheap, later move, not a redesign.
 */
#ifndef REFLUX_RUNTIME_H
#define REFLUX_RUNTIME_H

/* Real, named action types every dispatcher/subscriber pair agrees on by convention (the same
 * "both sides hardcode the same literal, documented cross-reference" pattern protocol.h/
 * living_map_bridge.h already use for their own duplicated wire constants -- REFLUX has no
 * exported-constant mechanism of its own, PARENA's (export ...) covers functions, not values). */
#define REFLUX_ACTION_BLOODFLOWER_TRIGGERED 1

#define REFLUX_LOG_CAPACITY 256

typedef struct {
    int action_type;
    int a, b, c; /* real, action-specific scalar payload -- matching VS0's own scalar-only ABI, same shape LivingMapEvent already uses */
} RefluxAction;

typedef struct {
    RefluxAction actions[REFLUX_LOG_CAPACITY];
    int total_dispatched; /* every dispatch ever, even past capacity -- same "distinguish full from empty" convention living_map_events.h already uses */
} RefluxLog;

void reflux_log_reset(RefluxLog *log);
void reflux_log_dispatch(RefluxLog *log, int action_type, int a, int b, int c);

/* Number of actions currently retained (min(total_dispatched, CAPACITY)). */
int reflux_log_length(const RefluxLog *log);

/* index 0 = oldest still-retained action, reflux_log_length()-1 = most recent. NULL if index is out
 * of that range. */
const RefluxAction *reflux_log_at(const RefluxLog *log, int index);

/* ---- The one, real, per-match REFLUX log + its PARENA-callable host wrappers ------------------
 * reflux_host_* are the real functions stdlib/reflux/reflux.prn's own #target inline-c bodies
 * call into by name -- same call-by-name FFI convention every mod in this repo already uses,
 * applied to a shared log instead of a bespoke per-mod host function. */
void reflux_host_reset(void);
void reflux_host_dispatch(int action_type, int a, int b, int c);
int reflux_host_log_size(void);
int reflux_host_action_type_at(int index); /* -1 if index out of range */
int reflux_host_action_a_at(int index);    /* 0 if index out of range */
int reflux_host_action_b_at(int index);    /* 0 if index out of range */
int reflux_host_action_c_at(int index);    /* 0 if index out of range */

#endif /* REFLUX_RUNTIME_H */
