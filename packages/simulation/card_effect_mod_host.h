/* card_effect_mod_host.h -- real extern declarations for card_effect_mod.c's own exported
 * entry point, plus the internal helper it emits (VS0 doesn't mark unexported top-level defns
 * static, so is_mythic_card_ is technically visible too -- declared here for completeness, not
 * because any host code calls it directly). Same "-include this header before compiling the
 * generated C" pattern every other REDGARDEN/ECOWAR mod host header already established -- pure
 * C linking, no cgo layer needed.
 *
 * on_ecowar_resolve_card_magnitude has no host-side call-INTO-C requirement (unlike every
 * REDGARDEN mod, which calls back into a redgarden_host_* function) -- this mod is pure PARENA
 * logic, called FROM host C (ecowar_resolve_card_effect, arena_game.c), not calling back out.
 */
#ifndef CARD_EFFECT_MOD_HOST_H
#define CARD_EFFECT_MOD_HOST_H

extern int on_ecowar_resolve_card_magnitude(int card_id, int base_magnitude);
/* EMPIRE NORTHSTAR §3.1 (kanban #576): BIG_O faction bridge. `band` is the caster's BIG_O decorum band
 * (bigo_witness_rules.h decorum_band: 3 = CANCELLED, which blocks the cast). */
extern int on_ecowar_resolve_card_faction(int card_id, int base_magnitude, int band);
extern int on_ecowar_card_decorum_action(int card_id);
extern int is_mythic_card_(int card_id);

#endif /* CARD_EFFECT_MOD_HOST_H */
