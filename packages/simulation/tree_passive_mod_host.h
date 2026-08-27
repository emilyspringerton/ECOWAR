/* tree_passive_mod_host.h -- real extern declaration for the one host-side
 * symbol tree_passive_mod.c's #target/inline-c body calls into, plus the
 * mod's own entry point. Same "-include this header before compiling the
 * generated C" pattern bloodflower_mod_host.h already established -- pure
 * C linking, no cgo layer needed.
 *
 * redgarden_host_tree_passive_strike has a real implementation in
 * arena_game.c (arena_hero_tree_passive's own range/cooldown/hero-id gate
 * calls on_tree_passive_strike, which calls back into this).
 */
#ifndef TREE_PASSIVE_MOD_HOST_H
#define TREE_PASSIVE_MOD_HOST_H

extern void redgarden_host_tree_passive_strike(int hero_index, int obstacle_index);
extern void on_tree_passive_strike(int hero_index, int obstacle_index);

#endif /* TREE_PASSIVE_MOD_HOST_H */
