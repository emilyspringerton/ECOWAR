/* frontier_village_mod_host.h -- real extern declarations for frontier_village_mod.c's own 3
 * exported entry points. Same "-include this header before compiling the generated C" pattern
 * every REDGARDEN/ECOWAR mod host header already established -- pure C linking, no cgo layer.
 *
 * All 3 functions are pure PARENA decision logic, called FROM host C (town.c's town_tick/
 * town_attempt_convert), not calling back out to any redgarden_host_ or ecowar_host_ function --
 * same shape ecowar/card_effect_mod_host.h already has for that exact reason.
 */
#ifndef FRONTIER_VILLAGE_MOD_HOST_H
#define FRONTIER_VILLAGE_MOD_HOST_H

extern int on_frontier_village_spawn_interval_ms(int population, int militia);
extern int on_frontier_village_should_raise_militia(int population, int militia);
extern int on_frontier_village_convert_resistance(int population, int militia);

#endif /* FRONTIER_VILLAGE_MOD_HOST_H */
