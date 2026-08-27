#ifndef MLP_INFER_H
#define MLP_INFER_H

/* mlp_infer.h/.c (S170-227, NORTHSTAR §21): a small, generic, dependency-free dense-MLP forward
 * pass -- the "embed the weights right into the c code" pattern for the RL policy network
 * (packages/common/gpt2_infer.c is the WRONG shape for this: that's a token-generation
 * transformer for the separate corpus-pretraining lineage, S170-220; this is a fixed-size
 * numeric-vector-in, numeric-vector-out policy net, matching the sibling SHANKPIT repo's own
 * neural_net.h/brain_weights.h precedent -- see that repo's own dense_layer()/bot_brain_forward()
 * for the exact shape this ports, generalized to an arbitrary number of layers instead of
 * SHANKPIT's own hardcoded 8->256->128->4).
 *
 * An MlpModel is deliberately just a thin, const, pointer-based VIEW over data that lives
 * elsewhere -- normally in a generated weights header (scripts/export_rl_policy_to_c.py writes
 * one after PPO training, S170-227's own export step) as `static const float ...[] = {...}`
 * arrays plus one `static const MlpModel RL_POLICY_MODEL = {...}` wiring them together. No
 * malloc/free anywhere in this file: unlike gpt2_infer.c (which loads a runtime-sized model from
 * a separately-shipped .bin file, since GPT-2-scale weights are too large to reasonably compile
 * in), this model is small enough that the weights themselves ARE the compiled binary, so
 * there's nothing to allocate or load at startup -- exactly the point of embedding. */

typedef struct {
    int n_layers;                    /* number of weight/bias/activation layers */
    const int *layer_sizes;          /* n_layers+1 entries: [input_size, hidden1, ..., output_size] */
    const float *const *weights;     /* n_layers entries; weights[i] is (layer_sizes[i+1] x layer_sizes[i]), row-major */
    const float *const *biases;      /* n_layers entries; biases[i] is layer_sizes[i+1] */
    const int *activations;          /* n_layers entries: 0=linear, 1=relu, 2=tanh -- same convention SHANKPIT's own dense_layer() uses */
} MlpModel;

/* mlp_forward: runs `input` (model->layer_sizes[0] floats) through every layer in order,
 * writing the final layer's output (model->layer_sizes[n_layers] floats) into `output`.
 * `output` must be preallocated by the caller to at least the model's own output size; a small
 * fixed-size stack scratch buffer is used internally for intermediate activations, sized to
 * MLP_INFER_MAX_LAYER_WIDTH -- generous for any policy net this small (SHANKPIT's own largest
 * hidden layer is 256; this repo's default net_arch is [64, 64]), not a real limitation for the
 * embedded-policy use case this file exists for. */
#define MLP_INFER_MAX_LAYER_WIDTH 512

void mlp_forward(const MlpModel *model, const float *input, float *output);

#endif /* MLP_INFER_H */
