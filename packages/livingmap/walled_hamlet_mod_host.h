/* walled_hamlet_mod_host.h -- real extern declarations for walled_hamlet_mod.c's own 5 exported
 * entry points. Same "-include this header before compiling the generated C" pattern every
 * REDGARDEN/ECOWAR mod host header already established.
 *
 * All 5 functions are pure PARENA decision logic, called FROM host C (town.c's
 * town_tick/town_attempt_convert/town_tick_with_creeps), not calling back out to any
 * redgarden_host_ or ecowar_host_ function -- same shape frontier_village_mod_host.h already has.
 */
#ifndef WALLED_HAMLET_MOD_HOST_H
#define WALLED_HAMLET_MOD_HOST_H

extern int on_walled_hamlet_spawn_interval_ms(int population, int militia);
extern int on_walled_hamlet_should_raise_militia(int population, int militia);
extern int on_walled_hamlet_convert_resistance(int population, int militia);
extern int on_walled_hamlet_defense_range(int militia);
extern int on_walled_hamlet_defense_damage(int militia);

#endif /* WALLED_HAMLET_MOD_HOST_H */
