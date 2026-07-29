#!/usr/bin/env python3
"""
Convert an already-saved REDGARDEN arena-bot checkpoint (scripts/colab_train.py's own
output_dir, a standard HuggingFace GPT2LMHeadModel.save_pretrained() directory) to the flat
binary weight format packages/common/gpt2_infer.c's ported C inference engine expects.

Same conversion scripts/colab_train.py's own export_weights_to_c_bin() runs automatically right
after training -- this script exists separately for the case where you already have a saved
checkpoint (downloaded from Drive, or trained in an earlier run) and just want to (re-)export it
without retraining. Mirrors the sibling gpt2-alpine-c repo's own convert_checkpoint.py write
order verbatim (S170-220) -- see that file, or packages/common/gpt2_infer.h's own doc comment,
for the exact layout.

Usage:
    python3 scripts/export_weights_to_c.py <checkpoint-dir> weights/redgarden-arena-bot.bin
"""
import sys

import numpy as np


def tensor_to_np(t):
    return t.detach().cpu().float().numpy()


def write_f32(f, arr):
    f.write(arr.astype(np.float32).flatten().tobytes())


def main(checkpoint_dir: str, output_bin: str):
    from transformers import GPT2LMHeadModel

    model = GPT2LMHeadModel.from_pretrained(checkpoint_dir)
    model.eval()
    sd = model.state_dict()

    def get(key):
        if key in sd:
            return tensor_to_np(sd[key])
        raise KeyError(f"Missing weight key: {key}")

    n_layer = model.config.n_layer
    print(f"Checkpoint: {checkpoint_dir}  layers={n_layer}  "
          f"embd={model.config.n_embd}  vocab={model.config.vocab_size}")

    with open(output_bin, "wb") as f:
        write_f32(f, get("transformer.wte.weight"))   # (V, D)
        write_f32(f, get("transformer.wpe.weight"))   # (T, D)

        for l in range(n_layer):
            p = f"transformer.h.{l}."
            write_f32(f, get(p + "ln_1.weight"))
            write_f32(f, get(p + "ln_1.bias"))
            # Conv1D weights are (in, out) -> transpose to (out, in) for the C engine's
            # row-major y = W*x + b convention.
            write_f32(f, get(p + "attn.c_attn.weight").T)
            write_f32(f, get(p + "attn.c_attn.bias"))
            write_f32(f, get(p + "attn.c_proj.weight").T)
            write_f32(f, get(p + "attn.c_proj.bias"))
            write_f32(f, get(p + "ln_2.weight"))
            write_f32(f, get(p + "ln_2.bias"))
            write_f32(f, get(p + "mlp.c_fc.weight").T)
            write_f32(f, get(p + "mlp.c_fc.bias"))
            write_f32(f, get(p + "mlp.c_proj.weight").T)
            write_f32(f, get(p + "mlp.c_proj.bias"))

        write_f32(f, get("transformer.ln_f.weight"))
        write_f32(f, get("transformer.ln_f.bias"))

    import os
    size_mb = os.path.getsize(output_bin) / 1024 / 1024
    print(f"Wrote {output_bin} ({size_mb:.1f} MB)")
    print(f"C engine config needed to load this: gpt2_model_new({model.config.vocab_size}, "
          f"{model.config.n_ctx}, {model.config.n_embd}, {model.config.n_layer}, "
          f"{model.config.n_head})")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 scripts/export_weights_to_c.py <checkpoint-dir> <output.bin>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
