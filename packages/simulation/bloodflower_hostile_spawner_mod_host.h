/* bloodflower_hostile_spawner_mod_host.h -- real extern declarations for
 * bloodflower_hostile_spawner_mod.c's own 2 exported entry points (REFLUX subscriber, EMILY/
 * BACKLOG.md SECTION 380). Same "-include this header before compiling the generated C" pattern
 * every mod host header in this repo already establishes.
 *
 * Pulls in reflux_mod_host.h (transitively reflux_runtime.h) so
 * REFLUX_ACTION_BLOODFLOWER_TRIGGERED is visible to the generated
 * on_bloodflower_hostile_spawner_should_react body without a second -include line.
 */
#ifndef BLOODFLOWER_HOSTILE_SPAWNER_MOD_HOST_H
#define BLOODFLOWER_HOSTILE_SPAWNER_MOD_HOST_H

#include "../reflux/reflux_mod_host.h"

extern int on_bloodflower_hostile_spawner_should_react(int action_type);
extern int on_bloodflower_hostile_spawner_creep_count(void);

#endif /* BLOODFLOWER_HOSTILE_SPAWNER_MOD_HOST_H */
