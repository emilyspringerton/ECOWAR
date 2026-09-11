/* packages/simulation/card_deck.h -- Deck/Hand mechanics for ECOWAR's card-battler experiment
 * (BACKLOG.md SECTION 377/S378, founder: "lets experiment with making the hero cards based and
 * if it ends up being unfun we bring back classic RTS/MOBA hero affordances in order to increase
 * player agency").
 *
 * Real-time, continuous draw (Clash Royale's own model, not Hearthstone's turn-based full-hand
 * draw) -- matches ECOWAR's own existing real-time Flow/mana economy and the founder's own
 * "Hearthstone/Clash Royale-style" card-UI framing. A deliberately simple, "narrowest real slice"
 * deck model for a genuine experiment, not a final design: one copy of every real card id, no
 * curated deck-building, no draw-pile/discard-pile distinction -- a real, endless, recycling
 * sequence instead. If the experiment sticks, deck-building (variable copy counts, a smaller
 * curated deck, omitted cards) is real, separate, later work.
 */
#ifndef CARD_DECK_H
#define CARD_DECK_H

#include "arena_game.h"

/* Card ids [0, ECOWAR_CARD_COUNT) are the existing 16 generic ECOWAR_CARDS (arena_game.h).
 * [ECOWAR_CARD_COUNT, ECOWAR_CARD_COUNT+3) are THIS HERO's own Q/W/R -- "the hero abilities cards
 * shuffled into your deck" (founder's own words). card_battler.h's own card_battler_play_slot
 * resolves these by calling arena_cast_q/arena_toggle_w/arena_cast_r directly, not through
 * ecowar_resolve_card_effect -- a real ability cast, not a generic card effect, reusing every
 * hero's own existing kit implementation unchanged. */
#define CARD_ID_HERO_Q (ECOWAR_CARD_COUNT + 0)
#define CARD_ID_HERO_W (ECOWAR_CARD_COUNT + 1)
#define CARD_ID_HERO_R (ECOWAR_CARD_COUNT + 2)
#define CARD_DECK_CARD_COUNT (ECOWAR_CARD_COUNT + 3)

/* One copy of every real card id -- see this file's own header comment for why not a smaller,
 * curated deck yet. */
#define CARD_DECK_SIZE CARD_DECK_CARD_COUNT
#define CARD_HAND_SIZE 4
#define CARD_HAND_REDRAW_MS 3000

typedef struct {
    int card_ids[CARD_DECK_SIZE]; /* a shuffled permutation of 0..CARD_DECK_SIZE-1 */
    int draw_cursor;              /* index of the next card to draw */
} CardDeck;

typedef struct {
    int card_id;              /* -1 = empty slot, currently redrawing */
    int redraw_ms_remaining;  /* only meaningful while card_id == -1 */
} CardHandSlot;

typedef struct {
    CardHandSlot slots[CARD_HAND_SIZE];
} CardHand;

/* Fisher-Yates shuffle via an isolated xorshift32 PRNG (deliberately not libc rand() -- same
 * "isolated, seed-reproducible" reasoning arena_game.c's own Mandelbrot jungle generation already
 * established), seeded by shuffle_seed. shuffle_seed == 0 is coerced to 1 -- xorshift32 can't be
 * seeded with 0 (it would output 0 forever). */
void card_deck_init(CardDeck *deck, unsigned int shuffle_seed);

/* Draws the deck's own next card, wrapping back to the start once exhausted -- a real, endless,
 * recycling deck (see this file's own header comment), not a draw-pile/discard-pile/"out of
 * cards" model. */
int card_deck_draw_next(CardDeck *deck);

/* Fills every hand slot via card_deck_draw_next -- call once, right after card_deck_init. */
void card_hand_init(CardHand *hand, CardDeck *deck);

/* Advances any currently-redrawing (empty) slot's timer, drawing a fresh card from deck once it
 * elapses. */
void card_hand_tick(CardHand *hand, CardDeck *deck, unsigned int dt_ms);

/* Plays whichever card is in slot_index: returns its real card_id and starts that slot
 * redrawing (CARD_HAND_REDRAW_MS). Returns -1 (a real no-op -- nothing played, no redraw
 * started) if slot_index is out of range or that slot is currently empty/redrawing, same "no
 * free swing, but no wasted resource either" convention every other whiffed action in this
 * codebase already holds itself to. */
int card_hand_play(CardHand *hand, int slot_index);

#endif /* CARD_DECK_H */
