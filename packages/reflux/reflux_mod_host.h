/* reflux_mod_host.h -- real extern declarations for reflux_mod.c's own 6 exported entry points.
 * Same "-include this header before compiling the generated C" pattern every mod host header in
 * this repo already establishes.
 *
 * All 6 are pure, thin wrappers calling straight into reflux_runtime.c's own real host functions
 * (reflux_host_*) -- this mod IS the interface, reflux_runtime.c does the real work, same split
 * every other real mod in this repo already uses.
 */
#ifndef REFLUX_MOD_HOST_H
#define REFLUX_MOD_HOST_H

/* Pulls in RefluxLog/RefluxAction and the real REFLUX_ACTION_* constants too -- any file that
 * -includes this header (or #includes it directly, like bloodflower_mod_host.h now does) gets
 * both the real reflux_dispatch/etc. prototypes below AND the shared action-type constants for
 * free, without a second include line at every call site. */
#include "reflux_runtime.h"

extern void reflux_dispatch(int action_type, int a, int b, int c);
extern int reflux_log_size(void);
extern int reflux_action_type_at(int index);
extern int reflux_action_a_at(int index);
extern int reflux_action_b_at(int index);
extern int reflux_action_c_at(int index);

#endif /* REFLUX_MOD_HOST_H */
