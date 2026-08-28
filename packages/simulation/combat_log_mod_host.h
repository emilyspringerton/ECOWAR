/* combat_log_mod_host.h -- real extern declarations for combat_log_mod.c's five exported entry
 * points, plus the redgarden_host_log_* functions each calls back into. Same "-include this
 * header before compiling the generated C" pattern every other REDGARDEN/ECOWAR mod host header
 * already established -- pure C linking, no cgo layer needed.
 *
 * All five redgarden_host_log_* functions have real implementations in arena_game.c (see
 * ArenaCombatLogEntry's own doc comment there for the real call sites) -- this mod calls back
 * into host C the same shape bloodflower_mod/tree_passive_mod/etc already established, not the
 * "pure logic, no callback" shape card_effect_mod/bacon_puck_intangible_speed_mod use.
 */
#ifndef COMBAT_LOG_MOD_HOST_H
#define COMBAT_LOG_MOD_HOST_H

extern void redgarden_host_log_kill(int victim, int killer);
extern void redgarden_host_log_purchase(int buyer, int item_id, int cost);
extern void redgarden_host_log_node_capture(int node_id, int team);
extern void redgarden_host_log_node_uncapture(int node_id, int team);
extern void redgarden_host_log_king_spawn(int camp_id);

extern void on_hero_kill(int victim, int killer);
extern void on_item_purchase(int buyer, int item_id, int cost);
extern void on_node_capture(int node_id, int team);
extern void on_node_uncapture(int node_id, int team);
extern void on_king_spawn(int camp_id);

#endif /* COMBAT_LOG_MOD_HOST_H */
