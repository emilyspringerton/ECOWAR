/* town_cap_mod_host.h -- real extern declaration for town_cap_mod.c's own one exported entry
 * point (EMILY/BACKLOG.md SECTION 381, "capping a base should work as a mod"). Same
 * "-include this header before compiling the generated C" pattern every mod host header in this
 * repo already establishes.
 *
 * Pulls in packages/reflux/reflux_mod_host.h (transitively reflux_runtime.h) so the real
 * reflux_dispatch prototype and REFLUX_ACTION_TOWN_CAPPED constant are visible to the generated
 * on_town_captured body without a second -include line -- same pattern
 * bloodflower_mod_host.h/bloodflower_hostile_spawner_mod_host.h already use.
 */
#ifndef TOWN_CAP_MOD_HOST_H
#define TOWN_CAP_MOD_HOST_H

#include "../reflux/reflux_mod_host.h"

extern void on_town_captured(int town_id, int old_faction, int new_faction);

#endif /* TOWN_CAP_MOD_HOST_H */
