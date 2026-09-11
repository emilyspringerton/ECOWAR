/* packages/simulation/card_deck.c -- see card_deck.h for the real design rationale. */
#include "card_deck.h"

static unsigned int xorshift32(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void card_deck_init(CardDeck *deck, unsigned int shuffle_seed) {
    for (int i = 0; i < CARD_DECK_SIZE; i++) deck->card_ids[i] = i;

    unsigned int rng_state = shuffle_seed != 0 ? shuffle_seed : 1;
    for (int i = CARD_DECK_SIZE - 1; i > 0; i--) {
        int j = (int)(xorshift32(&rng_state) % (unsigned int)(i + 1));
        int tmp = deck->card_ids[i];
        deck->card_ids[i] = deck->card_ids[j];
        deck->card_ids[j] = tmp;
    }
    deck->draw_cursor = 0;
}

int card_deck_draw_next(CardDeck *deck) {
    int card_id = deck->card_ids[deck->draw_cursor];
    deck->draw_cursor = (deck->draw_cursor + 1) % CARD_DECK_SIZE;
    return card_id;
}

void card_hand_init(CardHand *hand, CardDeck *deck) {
    for (int i = 0; i < CARD_HAND_SIZE; i++) {
        hand->slots[i].card_id = card_deck_draw_next(deck);
        hand->slots[i].redraw_ms_remaining = 0;
    }
}

void card_hand_tick(CardHand *hand, CardDeck *deck, unsigned int dt_ms) {
    for (int i = 0; i < CARD_HAND_SIZE; i++) {
        CardHandSlot *slot = &hand->slots[i];
        if (slot->card_id != -1) continue;

        slot->redraw_ms_remaining -= (int)dt_ms;
        if (slot->redraw_ms_remaining <= 0) {
            slot->card_id = card_deck_draw_next(deck);
            slot->redraw_ms_remaining = 0;
        }
    }
}

int card_hand_play(CardHand *hand, int slot_index) {
    if (slot_index < 0 || slot_index >= CARD_HAND_SIZE) return -1;

    CardHandSlot *slot = &hand->slots[slot_index];
    if (slot->card_id == -1) return -1;

    int played = slot->card_id;
    slot->card_id = -1;
    slot->redraw_ms_remaining = CARD_HAND_REDRAW_MS;
    return played;
}
