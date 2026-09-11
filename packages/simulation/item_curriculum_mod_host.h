/* item_curriculum_mod_host.h -- real extern declaration for the one host-side symbol
 * item_curriculum_mod.c's #target/inline-c body calls into, plus the mod's own entry point.
 * Same "-include this header before compiling the generated C" pattern tree_passive_mod_host.h
 * already established -- pure C linking, no cgo layer needed. (build_template_mod_host.h was
 * this same pattern's other real example when this comment was written; removed S371-02.)
 *
 * redgarden_host_item_curriculum_generate_counter_item has a real implementation in
 * arena_game.c (blends two ARENA_ITEMS catalog entries into a runtime-mutable curriculum
 * slot -- see arena_game.h's "Item curriculum" section doc comment for the full design).
 */
#ifndef ITEM_CURRICULUM_MOD_HOST_H
#define ITEM_CURRICULUM_MOD_HOST_H

extern int redgarden_host_item_curriculum_generate_counter_item(int base_item_a, int base_item_b, int slot_index);
extern int on_generate_counter_item(int base_item_a, int base_item_b, int slot_index);

#endif /* ITEM_CURRICULUM_MOD_HOST_H */
