/* gpt2_infer.c -- see gpt2_infer.h's own doc comment for provenance (a verbatim port of the
 * sibling gpt2-alpine-c repo's src/gpt2.c). */
#include "gpt2_infer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── allocation helpers ──────────────────────────────────────────────────── */

static float *fmalloc(size_t n)
{
    float *p = (float *)malloc(sizeof(float) * n);
    if (!p) { fprintf(stderr, "gpt2: malloc(%zu floats) failed\n", n); exit(1); }
    return p;
}

static float *fcalloc(size_t n)
{
    float *p = (float *)calloc(n, sizeof(float));
    if (!p) { fprintf(stderr, "gpt2: calloc(%zu floats) failed\n", n); exit(1); }
    return p;
}

/* ── model lifecycle ─────────────────────────────────────────────────────── */

gpt2_model *gpt2_model_new(int n_vocab, int n_ctx, int n_embd, int n_layer, int n_head)
{
    gpt2_model *m = (gpt2_model *)calloc(1, sizeof(gpt2_model));
    m->n_vocab = n_vocab; m->n_ctx = n_ctx; m->n_embd = n_embd;
    m->n_layer = n_layer; m->n_head  = n_head;

    m->wte = fmalloc((size_t)n_vocab * n_embd);
    m->wpe = fmalloc((size_t)n_ctx   * n_embd);

    m->layers = (GPT2Layer *)calloc((size_t)n_layer, sizeof(GPT2Layer));
    int D = n_embd, D4 = n_embd * 4;
    for (int l = 0; l < n_layer; ++l) {
        GPT2Layer *ly = &m->layers[l];
        ly->ln_1_gamma  = fmalloc(D);
        ly->ln_1_beta   = fmalloc(D);
        ly->c_attn_w    = fmalloc((size_t)D * 3 * D);
        ly->c_attn_b    = fmalloc(3 * D);
        ly->c_proj_w    = fmalloc((size_t)D * D);
        ly->c_proj_b    = fmalloc(D);
        ly->ln_2_gamma  = fmalloc(D);
        ly->ln_2_beta   = fmalloc(D);
        ly->mlp_fc_w    = fmalloc((size_t)D * D4);
        ly->mlp_fc_b    = fmalloc(D4);
        ly->mlp_proj_w  = fmalloc((size_t)D4 * D);
        ly->mlp_proj_b  = fmalloc(D);
    }

    m->ln_f_gamma = fmalloc(D);
    m->ln_f_beta  = fmalloc(D);
    return m;
}

void gpt2_model_free(gpt2_model *m)
{
    if (!m) return;
    free(m->wte); free(m->wpe);
    for (int l = 0; l < m->n_layer; ++l) {
        GPT2Layer *ly = &m->layers[l];
        free(ly->ln_1_gamma); free(ly->ln_1_beta);
        free(ly->c_attn_w);   free(ly->c_attn_b);
        free(ly->c_proj_w);   free(ly->c_proj_b);
        free(ly->ln_2_gamma); free(ly->ln_2_beta);
        free(ly->mlp_fc_w);   free(ly->mlp_fc_b);
        free(ly->mlp_proj_w); free(ly->mlp_proj_b);
    }
    free(m->layers);
    free(m->ln_f_gamma); free(m->ln_f_beta);
    free(m);
}

/* ── weight loading ──────────────────────────────────────────────────────── */

static int fread_or_fail(float *buf, size_t n, FILE *f, const char *name)
{
    if (fread(buf, sizeof(float), n, f) != n) {
        fprintf(stderr, "gpt2: short read loading %s\n", name);
        return -1;
    }
    return 0;
}

#define RLOAD(buf, n, label) if (fread_or_fail(buf, n, f, label) < 0) { fclose(f); return -1; }

int gpt2_model_load_weights(gpt2_model *m, const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) { perror(filename); return -1; }

    int D = m->n_embd, D4 = D * 4;

    RLOAD(m->wte, (size_t)m->n_vocab * D,  "wte");
    RLOAD(m->wpe, (size_t)m->n_ctx   * D,  "wpe");

    for (int l = 0; l < m->n_layer; ++l) {
        GPT2Layer *ly = &m->layers[l];
        RLOAD(ly->ln_1_gamma, D,                  "ln_1_gamma");
        RLOAD(ly->ln_1_beta,  D,                  "ln_1_beta");
        RLOAD(ly->c_attn_w,   (size_t)D * 3 * D,  "c_attn_w");
        RLOAD(ly->c_attn_b,   3 * D,               "c_attn_b");
        RLOAD(ly->c_proj_w,   (size_t)D * D,       "c_proj_w");
        RLOAD(ly->c_proj_b,   D,                   "c_proj_b");
        RLOAD(ly->ln_2_gamma, D,                   "ln_2_gamma");
        RLOAD(ly->ln_2_beta,  D,                   "ln_2_beta");
        RLOAD(ly->mlp_fc_w,   (size_t)D * D4,      "mlp_fc_w");
        RLOAD(ly->mlp_fc_b,   D4,                  "mlp_fc_b");
        RLOAD(ly->mlp_proj_w, (size_t)D4 * D,      "mlp_proj_w");
        RLOAD(ly->mlp_proj_b, D,                   "mlp_proj_b");
    }

    RLOAD(m->ln_f_gamma, D, "ln_f_gamma");
    RLOAD(m->ln_f_beta,  D, "ln_f_beta");

    fclose(f);
    return 0;
}

/* ── math primitives ─────────────────────────────────────────────────────── */

static void layernorm(const float *x, float *out,
                      const float *gamma, const float *beta, int n)
{
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += x[i];
    mean /= n;
    double var = 0.0;
    for (int i = 0; i < n; ++i) { double d = x[i] - mean; var += d * d; }
    var /= n;
    float denom = 1.0f / sqrtf((float)var + 1e-5f);
    for (int i = 0; i < n; ++i)
        out[i] = ((float)(x[i] - mean)) * denom * gamma[i] + beta[i];
}

/* y = A * x + b.  A is (rows × cols) row-major. */
static void linear(const float *A, const float *b, const float *x,
                   float *y, int rows, int cols)
{
    for (int r = 0; r < rows; ++r) {
        float s = b ? b[r] : 0.0f;
        const float *Ar = A + (size_t)r * cols;
        for (int c = 0; c < cols; ++c) s += Ar[c] * x[c];
        y[r] = s;
    }
}

static void gelu_inplace(float *x, int n)
{
    /* GeLU: x * Phi(x) approximated with tanh */
    const float k = 0.7978845608f; /* sqrt(2/pi) */
    for (int i = 0; i < n; ++i) {
        float v = x[i];
        x[i] = 0.5f * v * (1.0f + tanhf(k * (v + 0.044715f * v * v * v)));
    }
}

static void softmax_inplace(float *x, int n)
{
    float mx = x[0];
    for (int i = 1; i < n; ++i) if (x[i] > mx) mx = x[i];
    double sum = 0.0;
    for (int i = 0; i < n; ++i) { x[i] -= mx; sum += exp((double)x[i]); }
    float inv = 1.0f / (float)sum;
    for (int i = 0; i < n; ++i) x[i] *= inv;
}

/* ── multi-head causal self-attention ────────────────────────────────────── */

/*
 * MHA for all t positions simultaneously (O(t²) in memory).
 * hidden: (t × D) input/output (in-place add of attention output).
 */
static void mha(const float *x, float *out,
                const float *c_attn_w, const float *c_attn_b,
                const float *c_proj_w, const float *c_proj_b,
                int t, int D, int n_head)
{
    int head_dim = D / n_head;
    /* Project x → Q, K, V: shape (t × 3D) */
    float *qkv = fmalloc((size_t)t * 3 * D);
    for (int i = 0; i < t; ++i) {
        const float *xi = x + (size_t)i * D;
        float *qkv_i = qkv + (size_t)i * 3 * D;
        linear(c_attn_w, c_attn_b, xi, qkv_i, 3 * D, D);
    }

    float *attn_out = fcalloc((size_t)t * D);
    float scale = 1.0f / sqrtf((float)head_dim);

    for (int h = 0; h < n_head; ++h) {
        int hoff = h * head_dim;
        /* Compute attention scores (t × t), applying causal mask */
        float *scores = fmalloc((size_t)t * t);
        for (int i = 0; i < t; ++i) {
            const float *qi = qkv + (size_t)i * 3 * D + hoff;
            for (int j = 0; j <= i; ++j) { /* causal: j <= i */
                const float *kj = qkv + (size_t)j * 3 * D + D + hoff;
                float s = 0.0f;
                for (int d = 0; d < head_dim; ++d) s += qi[d] * kj[d];
                scores[(size_t)i * t + j] = s * scale;
            }
            /* mask future positions with -inf */
            for (int j = i + 1; j < t; ++j)
                scores[(size_t)i * t + j] = -1e9f;
        }
        /* Softmax per row */
        for (int i = 0; i < t; ++i)
            softmax_inplace(scores + (size_t)i * t, t);
        /* Weighted sum of values */
        for (int i = 0; i < t; ++i) {
            float *out_i = attn_out + (size_t)i * D + hoff;
            const float *row = scores + (size_t)i * t;
            for (int j = 0; j <= i; ++j) {
                const float *vj = qkv + (size_t)j * 3 * D + 2 * D + hoff;
                float w = row[j];
                for (int d = 0; d < head_dim; ++d) out_i[d] += w * vj[d];
            }
        }
        free(scores);
    }
    free(qkv);

    /* Project attn_out → out via c_proj */
    for (int i = 0; i < t; ++i) {
        float *o = out + (size_t)i * D;
        linear(c_proj_w, c_proj_b, attn_out + (size_t)i * D, o, D, D);
    }
    free(attn_out);
}

/* ── transformer block ───────────────────────────────────────────────────── */

static void transformer_block(float *h, float *tmp,
                               const GPT2Layer *ly, int t, int D, int n_head)
{
    int D4 = D * 4;

    /* ── attention sub-layer ── */
    /* layernorm_1(h) → tmp */
    for (int i = 0; i < t; ++i)
        layernorm(h + (size_t)i * D, tmp + (size_t)i * D,
                  ly->ln_1_gamma, ly->ln_1_beta, D);

    float *attn_out = fcalloc((size_t)t * D);
    mha(tmp, attn_out,
        ly->c_attn_w, ly->c_attn_b,
        ly->c_proj_w, ly->c_proj_b,
        t, D, n_head);

    /* residual */
    for (size_t i = 0; i < (size_t)t * D; ++i) h[i] += attn_out[i];
    free(attn_out);

    /* ── MLP sub-layer ── */
    float *mlp_in  = fmalloc(D);
    float *mlp_mid = fmalloc(D4);
    float *mlp_out = fmalloc(D);

    for (int i = 0; i < t; ++i) {
        float *hi = h + (size_t)i * D;
        layernorm(hi, mlp_in, ly->ln_2_gamma, ly->ln_2_beta, D);
        linear(ly->mlp_fc_w,   ly->mlp_fc_b,   mlp_in,  mlp_mid, D4, D);
        gelu_inplace(mlp_mid, D4);
        linear(ly->mlp_proj_w, ly->mlp_proj_b, mlp_mid, mlp_out, D,  D4);
        for (int j = 0; j < D; ++j) hi[j] += mlp_out[j];
    }
    free(mlp_in); free(mlp_mid); free(mlp_out);
}

/* ── forward pass ────────────────────────────────────────────────────────── */

int gpt2_model_forward(gpt2_model *m, const int *tokens, int t, float *out_logits)
{
    if (t <= 0 || t > m->n_ctx) return -1;
    int D = m->n_embd;

    float *h   = fcalloc((size_t)t * D);
    float *tmp = fmalloc((size_t)t * D);

    /* token + position embeddings */
    for (int i = 0; i < t; ++i) {
        int tok = tokens[i];
        if (tok < 0 || tok >= m->n_vocab) { free(h); free(tmp); return -1; }
        const float *te = m->wte + (size_t)tok * D;
        const float *pe = m->wpe + (size_t)i   * D;
        float *hi = h + (size_t)i * D;
        for (int j = 0; j < D; ++j) hi[j] = te[j] + pe[j];
    }

    /* transformer blocks */
    for (int l = 0; l < m->n_layer; ++l)
        transformer_block(h, tmp, &m->layers[l], t, D, m->n_head);

    /* final layer norm on the last token */
    float *last = h + (size_t)(t - 1) * D;
    float *ln   = tmp; /* reuse */
    layernorm(last, ln, m->ln_f_gamma, m->ln_f_beta, D);

    /* project to vocab via wte transpose (weight tying) */
    for (int v = 0; v < m->n_vocab; ++v) {
        float s = 0.0f;
        const float *ve = m->wte + (size_t)v * D;
        for (int j = 0; j < D; ++j) s += ve[j] * ln[j];
        out_logits[v] = s;
    }

    free(h); free(tmp);
    return 0;
}

/* ── generation ──────────────────────────────────────────────────────────── */

/* Numerically stable entropy from raw logits (double precision, log-sum-exp).
 * H = log(Z) - E_p[x]  where Z = sum_i exp(x_i - max_x), p_i = exp(x_i-max)/Z.
 * Avoids float32 underflow that zeroes all non-max probs after softmax. */
static float token_entropy_from_logits(const float *logits, int n)
{
    double mx = (double)logits[0];
    for (int i = 1; i < n; ++i)
        if ((double)logits[i] > mx) mx = (double)logits[i];

    double sum_exp = 0.0, sum_wx = 0.0;
    for (int i = 0; i < n; ++i) {
        double e = exp((double)logits[i] - mx);
        sum_exp += e;
        sum_wx  += e * (double)logits[i];
    }
    double log_z = log(sum_exp) + mx;
    return (float)(log_z - sum_wx / sum_exp);
}

int gpt2_generate(gpt2_model *m, int *context_tokens, int context_len,
                  int max_new_tokens, int *out_tokens, gpt2_entropy *entropy)
{
    int V = m->n_vocab, T = m->n_ctx;
    int buf_size = context_len + max_new_tokens;
    if (buf_size > T) buf_size = T;

    int *buf = (int *)malloc(sizeof(int) * buf_size);
    if (!buf) return -1;
    memcpy(buf, context_tokens, sizeof(int) * context_len);
    int cur_len = context_len;

    float *logits = fmalloc(V);
    int n = 0;

    for (int step = 0; step < max_new_tokens; ++step) {
        if (gpt2_model_forward(m, buf, cur_len, logits) != 0) {
            free(buf); free(logits); return -1;
        }

        /* entropy from raw logits before softmax (numerically stable) */
        if (entropy && entropy->per_token)
            entropy->per_token[n] = token_entropy_from_logits(logits, V);

        /* greedy: argmax */
        softmax_inplace(logits, V);

        int best = 0;
        float best_v = logits[0];
        for (int i = 1; i < V; ++i)
            if (logits[i] > best_v) { best_v = logits[i]; best = i; }

        out_tokens[n++] = best;

        if (cur_len < buf_size) {
            buf[cur_len++] = best;
        } else {
            /* slide the window by 1 */
            memmove(buf, buf + 1, sizeof(int) * (buf_size - 1));
            buf[buf_size - 1] = best;
        }
    }

    if (entropy) entropy->n = n;
    free(buf); free(logits);
    return n;
}
