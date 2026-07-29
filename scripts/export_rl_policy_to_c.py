#!/usr/bin/env python3
"""
scripts/export_rl_policy_to_c.py (S170-227, NORTHSTAR §21) -- extracts a trained PPO policy's
own action-mean network (the part that matters for deterministic inference in a real match --
the value/critic network is training-only scaffolding, discarded here) and writes it as literal
C float arrays, matching the sibling SHANKPIT repo's own brain_weights.h pattern -- the actual
"embed weights right into the C code" shape, appropriate here because this network is small (a
few thousand params, unlike packages/common/gpt2_infer.c's own GPT-2-shaped model, which is
runtime-loaded from a separate .bin file instead because it's too large to compile in).

Usage:
    python3 scripts/export_rl_policy_to_c.py rl_checkpoints/ppo_arena_final.zip \\
        packages/common/rl_policy_weights.h

Generated header defines the weight/bias arrays, a `static const MlpModel RL_POLICY_MODEL`
(packages/common/mlp_infer.h's own struct) wiring them together, and a convenience
`rl_policy_forward(const float *obs, float *out_action)` wrapper that also clips the raw network
output to the real action-space bounds scripts/rl_env.py's own ArenaTrainingEnv already uses
(move_x/move_z to +-MOVE_TARGET_RANGE, cast_q/cast_w/cast_r to [-1, 1]) -- PPO's own action_net
is a plain, unconstrained Linear layer (no output activation), so nothing about the raw forward
pass itself guarantees in-range values; a real embedded caller needs this clip, not just the
Gaussian-sampling policy's own training-time action-space wrapping, which doesn't exist any more
once only the mean network gets embedded.

VERIFICATION (S170-227): `stable_baselines3`/`torch` extraction from a REAL trained model was
NOT run in the environment this was written in -- no trained checkpoint exists yet (this whole
RL pipeline, S170-224 through this file, was built and spec'd in one pass, before any real
training run). What WAS verified: this file's own core array-writing logic
(`write_c_header_from_layers`) against a HAND-CONSTRUCTED small `torch.nn.Sequential` shaped
exactly like SB3's own `policy_net` + `action_net` (Linear/Tanh pairs, then a final Linear) --
torch itself IS available in this environment, just not stable_baselines3 -- generating a real C
header, compiling it against packages/common/mlp_infer.c, and confirming the C forward pass
produces the SAME output as the original PyTorch network for real, non-trivial input (see this
pass's own commit message for the concrete comparison). `extract_layers_from_sb3_policy()`
itself (the part that reads a REAL SB3 model's `.mlp_extractor.policy_net`/`.action_net`
attributes) is written to Stable-Baselines3's documented internal structure but has not been run
against a real SB3 model -- flagged here, not claimed, same discipline S170-225/226 already set.
"""

import sys


def extract_layers_from_sequential(seq, final_linear=None):
    """Walks a torch.nn.Sequential of alternating Linear/activation modules (SB3's own
    mlp_extractor.policy_net shape) and returns a list of (weight_ndarray, bias_ndarray,
    activation_code) tuples, one per Linear layer -- activation_code follows
    packages/common/mlp_infer.h's own convention (0=linear, 1=relu, 2=tanh). If `final_linear`
    is given (SB3's own separate action_net, appended after policy_net with no activation), it's
    included as the last layer with activation_code 0. This function only depends on torch
    tensor .detach()/.numpy() and standard nn.Module introspection -- no stable_baselines3
    import needed, which is exactly why it was independently testable in this environment (see
    this file's own module doc comment)."""
    import torch.nn as nn

    layers = []
    pending_weight = None
    pending_bias = None
    for module in seq:
        if isinstance(module, nn.Linear):
            pending_weight = module.weight.detach().cpu().numpy()
            pending_bias = module.bias.detach().cpu().numpy()
        elif isinstance(module, nn.ReLU):
            layers.append((pending_weight, pending_bias, 1))
            pending_weight = pending_bias = None
        elif isinstance(module, nn.Tanh):
            layers.append((pending_weight, pending_bias, 2))
            pending_weight = pending_bias = None
        else:
            raise ValueError(f"Unrecognized module in Sequential: {type(module)} -- "
                              f"extend extract_layers_from_sequential to handle it")
    if pending_weight is not None:
        # A trailing Linear with no activation module after it in the Sequential itself.
        layers.append((pending_weight, pending_bias, 0))

    if final_linear is not None:
        layers.append((
            final_linear.weight.detach().cpu().numpy(),
            final_linear.bias.detach().cpu().numpy(),
            0,
        ))

    return layers


def extract_layers_from_sb3_policy(model):
    """Reads a REAL loaded Stable-Baselines3 PPO model's actor network -- NOT independently
    verified against a real SB3 model in this environment, see this file's own module doc
    comment. model.policy.mlp_extractor.policy_net is the hidden-layer stack (Linear+Tanh pairs
    by default); model.policy.action_net is the final Linear projecting to the raw action mean,
    with no activation -- exactly the shape extract_layers_from_sequential's own final_linear
    parameter exists for."""
    policy_net = model.policy.mlp_extractor.policy_net
    action_net = model.policy.action_net
    return extract_layers_from_sequential(policy_net, final_linear=action_net)


def write_c_header_from_layers(layers, output_path, guard_name="RL_POLICY_WEIGHTS_H",
                                model_name="RL_POLICY_MODEL"):
    """The actually-portable core of this file: given a plain list of (weight, bias,
    activation_code) tuples (regardless of where they came from -- a real SB3 model, or the
    hand-built torch.nn.Sequential this function's own verification run used), writes a
    self-contained C header with the literal float arrays, a packages/common/mlp_infer.h
    MlpModel instance wiring them together, and a clipped rl_policy_forward() convenience
    wrapper. weight arrays are written row-major (out_features x in_features), matching both
    torch.nn.Linear's own native storage layout AND packages/common/mlp_infer.c's own
    dense_layer() expectation -- no transpose needed, unlike the GPT-2 Conv1D export in
    scripts/colab_train.py/export_weights_to_c.py, which needs one specifically because GPT-2's
    own HuggingFace Conv1D stores weights the opposite way round from nn.Linear."""
    n_layers = len(layers)
    layer_sizes = [layers[0][0].shape[1]] + [w.shape[0] for w, _, _ in layers]

    lines = []
    lines.append(f"#ifndef {guard_name}")
    lines.append(f"#define {guard_name}")
    lines.append("")
    lines.append("/* Generated by scripts/export_rl_policy_to_c.py (S170-227) -- DO NOT EDIT BY HAND.")
    lines.append(" * Re-run the export script after retraining instead. See that script's own")
    lines.append(" * module doc comment, and packages/common/mlp_infer.h, for the full design. */")
    lines.append('#include "mlp_infer.h"')
    lines.append("")

    for i, (w, b, _act) in enumerate(layers):
        out_f, in_f = w.shape
        flat_w = ", ".join(f"{v:.8g}f" for v in w.flatten())
        flat_b = ", ".join(f"{v:.8g}f" for v in b.flatten())
        lines.append(f"static const float layer{i}_w[{out_f * in_f}] = {{ {flat_w} }};")
        lines.append(f"static const float layer{i}_b[{out_f}] = {{ {flat_b} }};")
        lines.append("")

    sizes_str = ", ".join(str(s) for s in layer_sizes)
    weights_str = ", ".join(f"layer{i}_w" for i in range(n_layers))
    biases_str = ", ".join(f"layer{i}_b" for i in range(n_layers))
    acts_str = ", ".join(str(act) for _, _, act in layers)

    lines.append(f"static const int {model_name}_SIZES[{n_layers + 1}] = {{ {sizes_str} }};")
    lines.append(f"static const float *const {model_name}_WEIGHTS[{n_layers}] = {{ {weights_str} }};")
    lines.append(f"static const float *const {model_name}_BIASES[{n_layers}] = {{ {biases_str} }};")
    lines.append(f"static const int {model_name}_ACTS[{n_layers}] = {{ {acts_str} }};")
    lines.append("")
    lines.append(f"static const MlpModel {model_name} = {{")
    lines.append(f"    {n_layers}, {model_name}_SIZES, {model_name}_WEIGHTS, {model_name}_BIASES, {model_name}_ACTS")
    lines.append("};")
    lines.append("")
    lines.append(f"#define RL_POLICY_OBS_SIZE {layer_sizes[0]}")
    lines.append(f"#define RL_POLICY_ACTION_SIZE {layer_sizes[-1]}")
    lines.append("/* Move-target/cast-flag bounds -- must match scripts/rl_env.py's own")
    lines.append(" * MOVE_TARGET_RANGE and action_space definition exactly. PPO's own action_net")
    lines.append(" * is a plain unconstrained Linear layer (no output activation), so the raw")
    lines.append(" * forward pass alone does not guarantee in-range values -- this wrapper clips.")
    lines.append(" */")
    lines.append("#define RL_POLICY_MOVE_TARGET_RANGE 20.0f")
    lines.append("")
    lines.append("/* rl_policy_forward: runs the policy network and clips the raw action-mean")
    lines.append(" * output to the real action-space bounds -- out_action must be preallocated")
    lines.append(" * to RL_POLICY_ACTION_SIZE floats. */")
    lines.append(f"static inline void rl_policy_forward(const float *obs, float *out_action) {{")
    lines.append(f"    mlp_forward(&{model_name}, obs, out_action);")
    lines.append("    if (out_action[0] < -RL_POLICY_MOVE_TARGET_RANGE) out_action[0] = -RL_POLICY_MOVE_TARGET_RANGE;")
    lines.append("    if (out_action[0] >  RL_POLICY_MOVE_TARGET_RANGE) out_action[0] =  RL_POLICY_MOVE_TARGET_RANGE;")
    lines.append("    if (out_action[1] < -RL_POLICY_MOVE_TARGET_RANGE) out_action[1] = -RL_POLICY_MOVE_TARGET_RANGE;")
    lines.append("    if (out_action[1] >  RL_POLICY_MOVE_TARGET_RANGE) out_action[1] =  RL_POLICY_MOVE_TARGET_RANGE;")
    lines.append("    for (int i = 2; i < RL_POLICY_ACTION_SIZE; i++) {")
    lines.append("        if (out_action[i] < -1.0f) out_action[i] = -1.0f;")
    lines.append("        if (out_action[i] >  1.0f) out_action[i] =  1.0f;")
    lines.append("    }")
    lines.append("}")
    lines.append("")
    lines.append(f"#endif /* {guard_name} */")

    with open(output_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    import os
    print(f"Wrote {output_path} ({os.path.getsize(output_path)} bytes, {n_layers} layers, "
          f"sizes={layer_sizes})")


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 scripts/export_rl_policy_to_c.py <ppo_model.zip> <output.h>")
        sys.exit(1)
    model_path, output_path = sys.argv[1], sys.argv[2]

    from stable_baselines3 import PPO
    model = PPO.load(model_path)
    layers = extract_layers_from_sb3_policy(model)
    write_c_header_from_layers(layers, output_path)


if __name__ == "__main__":
    main()
