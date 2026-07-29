/* tests/test_gpt2_infer.c -- headless smoke test for packages/common/gpt2_infer.c (S170-220).
 * No trained checkpoint exists to test against yet (that requires a real Colab run) -- this
 * checks the ported engine on a tiny SYNTHETIC model instead: deterministic weights written by
 * this file itself (not scripts/export_weights_to_c.py), loaded through the real
 * gpt2_model_load_weights binary-file path, then run through a real forward pass and a real
 * gpt2_generate call. What this proves: the file loading, memory layout, and forward-pass math
 * all run to completion without crashing or producing NaN/Inf, on a config shaped like what
 * scripts/colab_train.py actually trains (tiny, not GPT-2-small scale). What this does NOT
 * prove: that a real trained checkpoint round-trips correctly through
 * scripts/export_weights_to_c.py's own write order -- that needs a real Colab run to verify,
 * flagged here rather than claimed. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "../packages/common/gpt2_infer.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* A tiny config, same shape as scripts/colab_train.py's own small-model defaults (see that
 * script's own doc comment) -- not GPT-2-small scale, deliberately, since the whole point of
 * shrinking the model was to make it embeddable/fast, and a test using the real 124M-param scale
 * would take unreasonably long just to allocate. */
#define TEST_VOCAB 37
#define TEST_CTX   16
#define TEST_EMBD  8
#define TEST_LAYER 2
#define TEST_HEAD  2

/* write_synthetic_weights_file: writes a deterministic (not random -- reproducible across runs)
 * float32 blob matching gpt2_model_load_weights' own expected layout exactly (wte, wpe, then
 * per-layer ln_1/c_attn/c_proj/ln_2/mlp_fc/mlp_proj, then ln_f), scaled small so the forward
 * pass doesn't blow up into Inf/NaN purely from having no real trained structure. */
static void write_f32_block(FILE *f, int n, float scale, float offset) {
    for (int i = 0; i < n; i++) {
        float v = scale * (float)((i * 37 + 11) % 23 - 11) / 11.0f + offset;
        fwrite(&v, sizeof(float), 1, f);
    }
}

static void write_synthetic_weights_file(const char *path) {
    FILE *f = fopen(path, "wb");
    int D = TEST_EMBD, D4 = D * 4;

    write_f32_block(f, TEST_VOCAB * D, 0.02f, 0.0f); /* wte */
    write_f32_block(f, TEST_CTX * D,   0.02f, 0.0f); /* wpe */

    for (int l = 0; l < TEST_LAYER; l++) {
        write_f32_block(f, D, 0.0f, 1.0f);       /* ln_1_gamma (identity-ish scale) */
        write_f32_block(f, D, 0.0f, 0.0f);       /* ln_1_beta */
        write_f32_block(f, D * 3 * D, 0.02f, 0.0f); /* c_attn_w */
        write_f32_block(f, 3 * D, 0.0f, 0.0f);   /* c_attn_b */
        write_f32_block(f, D * D, 0.02f, 0.0f);  /* c_proj_w */
        write_f32_block(f, D, 0.0f, 0.0f);       /* c_proj_b */
        write_f32_block(f, D, 0.0f, 1.0f);       /* ln_2_gamma */
        write_f32_block(f, D, 0.0f, 0.0f);       /* ln_2_beta */
        write_f32_block(f, D * D4, 0.02f, 0.0f); /* mlp_fc_w */
        write_f32_block(f, D4, 0.0f, 0.0f);      /* mlp_fc_b */
        write_f32_block(f, D4 * D, 0.02f, 0.0f); /* mlp_proj_w */
        write_f32_block(f, D, 0.0f, 0.0f);       /* mlp_proj_b */
    }

    write_f32_block(f, D, 0.0f, 1.0f); /* ln_f_gamma */
    write_f32_block(f, D, 0.0f, 0.0f); /* ln_f_beta */

    fclose(f);
}

static void test_load_weights_from_file_succeeds(void) {
    const char *path = "/tmp/test_gpt2_infer_synthetic.bin";
    write_synthetic_weights_file(path);

    gpt2_model *m = gpt2_model_new(TEST_VOCAB, TEST_CTX, TEST_EMBD, TEST_LAYER, TEST_HEAD);
    int rc = gpt2_model_load_weights(m, path);
    CHECK(rc == 0, "gpt2_model_load_weights succeeds reading a correctly-sized synthetic weights file");

    gpt2_model_free(m);
    remove(path);
}

static void test_load_weights_missing_file_fails_cleanly(void) {
    gpt2_model *m = gpt2_model_new(TEST_VOCAB, TEST_CTX, TEST_EMBD, TEST_LAYER, TEST_HEAD);
    int rc = gpt2_model_load_weights(m, "/tmp/this_file_does_not_exist_gpt2_infer_test.bin");
    CHECK(rc == -1, "gpt2_model_load_weights returns -1, not a crash, for a missing file");
    gpt2_model_free(m);
}

static void test_forward_pass_produces_finite_logits(void) {
    const char *path = "/tmp/test_gpt2_infer_synthetic2.bin";
    write_synthetic_weights_file(path);

    gpt2_model *m = gpt2_model_new(TEST_VOCAB, TEST_CTX, TEST_EMBD, TEST_LAYER, TEST_HEAD);
    CHECK(gpt2_model_load_weights(m, path) == 0, "setup: synthetic weights load for the forward-pass test");

    int tokens[4] = { 1, 5, 9, 2 };
    float *logits = (float *)malloc(sizeof(float) * TEST_VOCAB);
    int rc = gpt2_model_forward(m, tokens, 4, logits);
    CHECK(rc == 0, "gpt2_model_forward returns 0 (success) for a valid token sequence");

    int all_finite = 1;
    for (int i = 0; i < TEST_VOCAB; i++) {
        if (!isfinite(logits[i])) all_finite = 0;
    }
    CHECK(all_finite, "every output logit is finite (no NaN/Inf) -- the ported attention/layernorm/GELU math runs cleanly end to end");

    free(logits);
    gpt2_model_free(m);
    remove(path);
}

static void test_forward_pass_rejects_out_of_range_context(void) {
    const char *path = "/tmp/test_gpt2_infer_synthetic3.bin";
    write_synthetic_weights_file(path);
    gpt2_model *m = gpt2_model_new(TEST_VOCAB, TEST_CTX, TEST_EMBD, TEST_LAYER, TEST_HEAD);
    gpt2_model_load_weights(m, path);

    float *logits = (float *)malloc(sizeof(float) * TEST_VOCAB);
    int rc = gpt2_model_forward(m, (int[]){0}, TEST_CTX + 1, logits);
    CHECK(rc == -1, "gpt2_model_forward rejects a token count beyond n_ctx instead of reading out of bounds");

    free(logits);
    gpt2_model_free(m);
    remove(path);
}

static void test_generate_produces_requested_token_count(void) {
    const char *path = "/tmp/test_gpt2_infer_synthetic4.bin";
    write_synthetic_weights_file(path);
    gpt2_model *m = gpt2_model_new(TEST_VOCAB, TEST_CTX, TEST_EMBD, TEST_LAYER, TEST_HEAD);
    gpt2_model_load_weights(m, path);

    int context[2] = { 3, 7 };
    int out_tokens[5];
    float entropy_buf[5];
    gpt2_entropy ent = { entropy_buf, 0 };
    int n = gpt2_generate(m, context, 2, 5, out_tokens, &ent);

    CHECK(n == 5, "gpt2_generate produces exactly the requested number of new tokens");
    CHECK(ent.n == 5, "per-token entropy is filled for every generated token");
    int tokens_in_range = 1;
    for (int i = 0; i < n; i++) {
        if (out_tokens[i] < 0 || out_tokens[i] >= TEST_VOCAB) tokens_in_range = 0;
    }
    CHECK(tokens_in_range, "every generated token id is a valid vocab index");

    gpt2_model_free(m);
    remove(path);
}

int main(void) {
    test_load_weights_from_file_succeeds();
    test_load_weights_missing_file_fails_cleanly();
    test_forward_pass_produces_finite_logits();
    test_forward_pass_rejects_out_of_range_context();
    test_generate_produces_requested_token_count();

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
