#ifndef GPT2_INFER_H
#define GPT2_INFER_H

#include <stddef.h>
#include <stdint.h>

/* gpt2_infer.h/.c (S170-220, founder: "we want to embed the weights right into the c code...
 * we can do it all with colab scripts running python to do it all"): a direct, verbatim port of
 * the sibling gpt2-alpine-c repo's own src/gpt2.c/gpt2.h -- same "check what already exists
 * first, port rather than reinvent" convention scripts/colab_train.py's own doc comment already
 * used for the training driver itself. Every dimension (n_vocab/n_ctx/n_embd/n_layer/n_head) is
 * a runtime parameter, not a hardcoded constant, so this ONE file serves both repos: gpt2-alpine-c
 * ships it at real GPT-2-small scale (12 layers, 768 dim, 50257 vocab, ~497MB of weights), while
 * REDGARDEN's own scripts/colab_train.py trains and exports a much smaller custom-sized config
 * (see that script's own doc comment for the exact numbers and why) -- small enough to actually
 * commit the resulting weight blob to git and run inference at real-time speed in a game loop,
 * neither of which GPT-2-small itself could do here. Not yet wired into the live bot AI decision
 * loop (arena_game.c's bot_cast_kit_if_ready or an eventual generalization of it) -- that's
 * separate, future work, flagged here rather than half-built and left looking finished. */

/*
 * Full GPT-2-shaped weight struct (parameterized -- see this header's own doc comment above for
 * why the same struct serves both a full-size and a REDGARDEN-scaled model).
 *
 * Weight layout per layer (matching gpt2-alpine-c's own convert_checkpoint.py write order,
 * mirrored here by scripts/export_weights_to_c.py):
 *   ln_1_gamma, ln_1_beta          (D each)
 *   c_attn_w (D × 3D), c_attn_b (3D)
 *   c_proj_w (D × D),  c_proj_b (D)
 *   ln_2_gamma, ln_2_beta          (D each)
 *   mlp_fc_w (D × 4D), mlp_fc_b (4D)
 *   mlp_proj_w (4D × D), mlp_proj_b (D)
 *
 * Global:
 *   wte (V × D), wpe (T × D)
 *   ln_f_gamma, ln_f_beta          (D each)
 */

typedef struct {
    /* layer norms */
    float *ln_1_gamma;   /* D */
    float *ln_1_beta;    /* D */
    float *ln_2_gamma;   /* D */
    float *ln_2_beta;    /* D */
    /* attention */
    float *c_attn_w;     /* D × 3D  (row-major: out=3D, in=D) */
    float *c_attn_b;     /* 3D */
    float *c_proj_w;     /* D × D   (out=D, in=D) */
    float *c_proj_b;     /* D */
    /* MLP */
    float *mlp_fc_w;     /* D × 4D  (out=4D, in=D) */
    float *mlp_fc_b;     /* 4D */
    float *mlp_proj_w;   /* 4D × D  (out=D, in=4D) */
    float *mlp_proj_b;   /* D */
} GPT2Layer;

typedef struct {
    int n_vocab;   /* V */
    int n_ctx;     /* T */
    int n_embd;    /* D */
    int n_layer;   /* L */
    int n_head;    /* H */
    /* global embeddings */
    float *wte;    /* V × D */
    float *wpe;    /* T × D */
    /* per-layer */
    GPT2Layer *layers; /* n_layer */
    /* final layer norm */
    float *ln_f_gamma; /* D */
    float *ln_f_beta;  /* D */
} gpt2_model;

/*
 * gpt2_entropy holds per-token entropy from the last generate call
 * (allocated by caller: n_tokens floats).
 */
typedef struct {
    float *per_token; /* entropy in nats for each generated token */
    int    n;
} gpt2_entropy;

gpt2_model *gpt2_model_new(int n_vocab, int n_ctx, int n_embd, int n_layer, int n_head);
void        gpt2_model_free(gpt2_model *m);

/*
 * Load from binary produced by scripts/export_weights_to_c.py.
 * Format: all weights written contiguously in float32, in the order:
 *   wte, wpe,
 *   [for each layer]: ln_1_gamma, ln_1_beta,
 *                     c_attn_w, c_attn_b, c_proj_w, c_proj_b,
 *                     ln_2_gamma, ln_2_beta,
 *                     mlp_fc_w, mlp_fc_b, mlp_proj_w, mlp_proj_b,
 *   ln_f_gamma, ln_f_beta
 * Returns 0 on success, -1 on error.
 */
int gpt2_model_load_weights(gpt2_model *m, const char *filename);

/*
 * Forward pass: compute logits for next token given context tokens[0..t-1].
 * out_logits must be preallocated to n_vocab floats.
 * Returns 0 on success.
 */
int gpt2_model_forward(gpt2_model *m, const int *tokens, int t, float *out_logits);

/*
 * Autoregressive generation. Greedy (argmax) by default.
 * If entropy != NULL, fills per-token entropy (caller allocates max_new_tokens floats).
 * Returns number of tokens generated, or -1 on error.
 */
int gpt2_generate(gpt2_model *m, int *context_tokens, int context_len,
                  int max_new_tokens, int *out_tokens, gpt2_entropy *entropy);

#endif /* GPT2_INFER_H */
