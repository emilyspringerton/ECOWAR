/* tests/test_mlp_infer.c -- headless test for packages/common/mlp_infer.c (S170-227). Uses
 * small, hand-computable weight sets (not random data) so expected outputs can be verified by
 * arithmetic in this file's own comments, not just "doesn't crash" -- the actual correctness bar
 * for a forward pass, unlike gpt2_infer.c's own synthetic-weights test (which only needed to
 * prove "runs to completion, no NaN" since verifying real transformer attention math by hand
 * isn't practical; a small MLP's matmul + activation IS practical to hand-verify, so this test
 * holds itself to that higher bar). */
#include <stdio.h>
#include <math.h>

#include "../packages/common/mlp_infer.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static int nearly(float a, float b) { return fabsf(a - b) < 0.001f; }

/* Single linear layer, identity-like weights: input [1,2,3] through a 3x3 identity weight
 * matrix + zero bias, linear activation -> output should be exactly the input, unchanged. The
 * simplest possible hand-verifiable case: if this fails, the basic matmul indexing is wrong. */
static void test_single_linear_identity_layer(void) {
    static const int sizes[2] = {3, 3};
    static const float w[9] = {
        1,0,0,
        0,1,0,
        0,0,1,
    };
    static const float b[3] = {0, 0, 0};
    static const float *weights[1] = { w };
    static const float *biases[1] = { b };
    static const int acts[1] = { 0 }; /* linear */
    MlpModel model = { 1, sizes, weights, biases, acts };

    float input[3] = {1.0f, 2.0f, 3.0f};
    float output[3];
    mlp_forward(&model, input, output);

    CHECK(nearly(output[0], 1.0f) && nearly(output[1], 2.0f) && nearly(output[2], 3.0f),
          "a single identity-weight linear layer passes the input through unchanged");
}

/* Single linear layer, a real (non-identity) hand-computed matmul: 2 inputs -> 2 outputs.
 * out[0] = 2*1 + 3*2 + bias(1) = 2+6+1 = 9
 * out[1] = 4*1 + 5*2 + bias(2) = 4+10+2 = 16 */
static void test_single_linear_layer_matmul_and_bias(void) {
    static const int sizes[2] = {2, 2};
    static const float w[4] = {
        2, 3,
        4, 5,
    };
    static const float b[2] = {1, 2};
    static const float *weights[1] = { w };
    static const float *biases[1] = { b };
    static const int acts[1] = { 0 };
    MlpModel model = { 1, sizes, weights, biases, acts };

    float input[2] = {1.0f, 2.0f};
    float output[2];
    mlp_forward(&model, input, output);

    CHECK(nearly(output[0], 9.0f), "row 0's matmul + bias computes exactly 2*1 + 3*2 + 1 = 9");
    CHECK(nearly(output[1], 16.0f), "row 1's matmul + bias computes exactly 4*1 + 5*2 + 2 = 16");
}

/* ReLU activation: a layer whose pre-activation sum is negative for one output and positive for
 * another -- confirms ReLU actually zeroes the negative one and passes the positive one through
 * unchanged, not e.g. always-zero or always-passthrough. */
static void test_relu_activation(void) {
    static const int sizes[2] = {1, 2};
    static const float w[2] = { 1, -1 }; /* out[0] = 1*x, out[1] = -1*x */
    static const float b[2] = { 0, 0 };
    static const float *weights[1] = { w };
    static const float *biases[1] = { b };
    static const int acts[1] = { 1 }; /* ReLU */
    MlpModel model = { 1, sizes, weights, biases, acts };

    float input[1] = { 5.0f };
    float output[2];
    mlp_forward(&model, input, output);

    CHECK(nearly(output[0], 5.0f), "ReLU passes a positive pre-activation value through unchanged");
    CHECK(nearly(output[1], 0.0f), "ReLU zeroes a negative pre-activation value");
}

/* Tanh activation: pre-activation 0 -> tanh(0) = 0 exactly, a clean hand-verifiable point on
 * the curve (unlike an arbitrary nonzero input, which would need a calculator to state as a
 * literal expected constant in this comment). */
static void test_tanh_activation_at_zero(void) {
    static const int sizes[2] = {1, 1};
    static const float w[1] = { 0 }; /* forces the pre-activation sum to 0 regardless of input */
    static const float b[1] = { 0 };
    static const float *weights[1] = { w };
    static const float *biases[1] = { b };
    static const int acts[1] = { 2 }; /* Tanh */
    MlpModel model = { 1, sizes, weights, biases, acts };

    float input[1] = { 123.0f }; /* irrelevant -- weight is 0 */
    float output[1];
    mlp_forward(&model, input, output);

    CHECK(nearly(output[0], 0.0f), "tanh(0) == 0 exactly, confirming the activation actually runs");
}

/* Two-layer network, matching this repo's own real default shape (a hidden layer with an
 * activation, then a linear output layer) -- the actual multi-layer buffer-chaining logic
 * mlp_forward's own doc comment describes, not just a single dense_layer call in isolation.
 * Hand-computed:
 *   hidden = ReLU(W1 * [1,1] + b1) where W1 = [[1,1],[1,-3]], b1 = [0,0]
 *     h[0] = ReLU(1*1 + 1*1 + 0) = ReLU(2) = 2
 *     h[1] = ReLU(1*1 + -3*1 + 0) = ReLU(-2) = 0
 *   output = W2 * hidden + b2 where W2 = [[2, 5]], b2 = [1]
 *     out[0] = 2*2 + 5*0 + 1 = 5 */
static void test_two_layer_network_chains_correctly(void) {
    static const int sizes[3] = {2, 2, 1};
    static const float w1[4] = {
        1, 1,
        1, -3,
    };
    static const float b1[2] = { 0, 0 };
    static const float w2[2] = { 2, 5 };
    static const float b2[1] = { 1 };
    static const float *weights[2] = { w1, w2 };
    static const float *biases[2] = { b1, b2 };
    static const int acts[2] = { 1, 0 }; /* ReLU hidden, linear output */
    MlpModel model = { 2, sizes, weights, biases, acts };

    float input[2] = { 1.0f, 1.0f };
    float output[1];
    mlp_forward(&model, input, output);

    CHECK(nearly(output[0], 5.0f),
          "a real two-layer ReLU-hidden + linear-output network computes the hand-verified result (5.0), confirming inter-layer buffer chaining is correct, not aliased or stale");
}

int main(void) {
    test_single_linear_identity_layer();
    test_single_linear_layer_matmul_and_bias();
    test_relu_activation();
    test_tanh_activation_at_zero();
    test_two_layer_network_chains_correctly();

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
