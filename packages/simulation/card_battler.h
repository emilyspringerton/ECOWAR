/* packages/simulation/card_battler.h -- per-owner Deck/Hand registry + play dispatch for
 * ECOWAR's card-battler experiment (BACKLOG.md SECTION 377/S378).
 *
 * Module-level state (like arena_bot_enabled), indexed by ArenaHero owner -- deliberately kept
 * out of ArenaState/ArenaHero itself while this remains an unwired experiment (nothing in any
 * live match calls into this file yet; see docs/NORTHSTAR_LIVING_MAP.md's own "hero-as-NPC/
 * card-battler" section for the full reasoning and the founder's own explicit, reversible
 * framing: "if it ends up being unfun we bring back classic RTS/MOBA hero affordances").
 */
#ifndef CARD_BATTLER_H
#define CARD_BATTLER_H

#include "card_deck.h"

/* Sets up owner's own deck (shuffled from shuffle_seed) and draws its starting hand. Call once,
 * e.g. right after a hero is picked for a card-battler-mode match. */
void card_battler_init_hero(int owner, unsigned int shuffle_seed);

/* Advances owner's own hand redraw timers. Call once per real game tick, alongside every other
 * per-hero per-tick update. No-op for an owner that was never card_battler_init_hero'd (an
 * all-zero CardHand just has every slot already "empty," so this stays a real no-op rather than
 * ever crashing on uninitialized state). */
void card_battler_tick(int owner, unsigned int dt_ms);

/* Plays owner's own hand slot slot_index: a generic card id (0..ECOWAR_CARD_COUNT-1) resolves via
 * arena_ecowar_play_card (the existing, real in-match card path, unchanged); CARD_ID_HERO_Q/_W/_R
 * instead call that hero's own arena_cast_q/arena_toggle_w/arena_cast_r directly -- a real
 * ability cast, reusing every hero's existing kit implementation unchanged, not a new effect.
 * Returns 1 if a real card was played, 0 if the slot was empty/out of range (a real no-op).
 * hover_target is forwarded only to the generic-card path -- hero ability casts triggered this
 * way take no hover target, same as PACKET_ARENA_CAST's own real Q/W/R dispatch. */
int card_battler_play_slot(int owner, int slot_index, int hover_target);

/* Read-only accessor for a test/tool/future-HUD to inspect an owner's current hand without
 * exposing the module-level array directly. NULL for an out-of-range owner. */
const CardHand *card_battler_hand(int owner);

#endif /* CARD_BATTLER_H */
