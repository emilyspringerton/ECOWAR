/* packages/simulation/card_battler.c -- see card_battler.h for the real design rationale. */
#include "card_battler.h"

#include <stddef.h>

static CardDeck g_card_decks[ARENA_MAX_HEROES];
static CardHand g_card_hands[ARENA_MAX_HEROES];

void card_battler_init_hero(int owner, unsigned int shuffle_seed) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    card_deck_init(&g_card_decks[owner], shuffle_seed);
    card_hand_init(&g_card_hands[owner], &g_card_decks[owner]);
}

void card_battler_tick(int owner, unsigned int dt_ms) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    card_hand_tick(&g_card_hands[owner], &g_card_decks[owner], dt_ms);
}

int card_battler_play_slot(int owner, int slot_index, int hover_target) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return 0;

    int card_id = card_hand_play(&g_card_hands[owner], slot_index);
    if (card_id == -1) return 0;

    if (card_id == CARD_ID_HERO_Q) arena_cast_q(owner);
    else if (card_id == CARD_ID_HERO_W) arena_toggle_w(owner);
    else if (card_id == CARD_ID_HERO_R) arena_cast_r(owner);
    else arena_ecowar_play_card(owner, card_id, hover_target);

    return 1;
}

const CardHand *card_battler_hand(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return NULL;
    return &g_card_hands[owner];
}
