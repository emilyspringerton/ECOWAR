/* mlp_infer.c -- see mlp_infer.h's own doc comment for the full design. */
#include "mlp_infer.h"
#include <math.h>
#include <stddef.h>

static void dense_layer(const float *in, const float *w, const float *b, float *out,
                         int in_size, int out_size, int activation) {
    for (int r = 0; r < out_size; r++) {
        float sum = b[r];
        const float *wr = w + (size_t)r * in_size;
        for (int c = 0; c < in_size; c++) sum += wr[c] * in[c];

        if (activation == 1) {         /* ReLU */
            out[r] = sum > 0.0f ? sum : 0.0f;
        } else if (activation == 2) {  /* Tanh */
            out[r] = tanhf(sum);
        } else {                       /* Linear */
            out[r] = sum;
        }
    }
}

void mlp_forward(const MlpModel *model, const float *input, float *output) {
    float buf_a[MLP_INFER_MAX_LAYER_WIDTH];
    float buf_b[MLP_INFER_MAX_LAYER_WIDTH];
    const float *cur_in = input;
    float *cur_out = buf_a;
    float *next_out = buf_b;

    for (int i = 0; i < model->n_layers; i++) {
        int in_size = model->layer_sizes[i];
        int out_size = model->layer_sizes[i + 1];
        int is_last = (i == model->n_layers - 1);
        float *dest = is_last ? output : cur_out;

        dense_layer(cur_in, model->weights[i], model->biases[i], dest,
                    in_size, out_size, model->activations[i]);

        if (!is_last) {
            cur_in = cur_out;
            /* swap scratch buffers so this layer's output becomes next layer's input without
               aliasing cur_out/next_out on the following iteration */
            float *tmp = cur_out;
            cur_out = next_out;
            next_out = tmp;
        }
    }
}
