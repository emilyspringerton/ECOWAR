/* build_template_mod_host.h -- real extern declaration for the one host-side symbol
 * build_template_mod.c's #target/inline-c body calls into, plus the mod's own entry point. Same
 * "-include this header before compiling the generated C" pattern bloodflower_mod_host.h and
 * tree_passive_mod_host.h already established.
 *
 * redgarden_host_buy_build_item has a real implementation in arena_game.c
 * (arena_hero_apply_build_template's own per-item loop calls on_apply_build_template_item, which
 * calls back into this).
 */
#ifndef BUILD_TEMPLATE_MOD_HOST_H
#define BUILD_TEMPLATE_MOD_HOST_H

extern int redgarden_host_buy_build_item(int hero_index, int item_id);
extern int on_apply_build_template_item(int hero_index, int item_id);

#endif /* BUILD_TEMPLATE_MOD_HOST_H */
