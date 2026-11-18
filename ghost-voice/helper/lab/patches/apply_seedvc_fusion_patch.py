"""Patch a local Seed-VC V1 inference.py to accept two fused identity references.

Usage from the Ghost Voice project root:
  python patches/apply_seedvc_fusion_patch.py /path/to/seed-vc

The patch is intentionally conservative and aborts if the expected upstream
code anchors are not found. It creates inference.py.ghostvoice-backup first.
"""
from __future__ import annotations
import argparse
from pathlib import Path


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(f"Expected exactly one anchor for {label}, found {count}. No file was written.")
    return source.replace(old, new, 1)


def patch(root: Path) -> Path:
    target = root / "inference.py"
    if not target.is_file():
        raise FileNotFoundError(f"Could not find {target}")
    source = target.read_text(encoding="utf-8")
    if "GHOSTVOICE_SEEDVC_FUSION_PATCH" in source:
        raise RuntimeError("Seed-VC inference.py already has the Ghost Voice fusion patch.")

    source = replace_once(
        source,
        "from hf_utils import load_custom_model_from_hf\n",
        "from hf_utils import load_custom_model_from_hf\n"
        "from ghostvoice.seedvc_style import build_fused_style_condition  # GHOSTVOICE_SEEDVC_FUSION_PATCH\n",
        "Ghost Voice import",
    )
    source = replace_once(
        source,
        "style2 = campplus_model(feat2.unsqueeze(0))\n",
        "if bool(args.identity_a) != bool(args.identity_b):\n"
        "        raise ValueError('Provide both --identity-a and --identity-b, or neither.')\n"
        "    if args.identity_a and args.identity_b:\n"
        "        style2, ghostvoice_fusion_report = build_fused_style_condition(\n"
        "            args.identity_a, args.identity_b, campplus_model, device,\n"
        "            weight_b=args.identity_mix_b, return_report=True)\n"
        "        print('GHOST VOICE FUSION:', ghostvoice_fusion_report)\n"
        "        if args.fused_identity_no_prompt:\n"
        "            # Seed-VC eval.py already demonstrates this promptless CFM path.\n"
        "            mel2 = mel2[:, :, :0]\n"
        "    else:\n"
        "        style2 = campplus_model(feat2.unsqueeze(0))\n",
        "CAMPPlus style injection",
    )
    source = replace_once(
        source,
        "cat_condition = torch.cat([prompt_condition, chunk_cond], dim=1)\n",
        "if args.identity_a and args.identity_b and args.fused_identity_no_prompt:\n"
        "            cat_condition = chunk_cond\n"
        "        else:\n"
        "            cat_condition = torch.cat([prompt_condition, chunk_cond], dim=1)\n",
        "promptless fused-identity condition",
    )
    source = replace_once(
        source,
        'parser.add_argument("--fp16", type=str2bool, default=True)\n',
        'parser.add_argument("--fp16", type=str2bool, default=True)\n'
        '    parser.add_argument("--identity-a", action="append", default=[], help="Voice A reference WAV; repeat for multiple takes")\n'
        '    parser.add_argument("--identity-b", action="append", default=[], help="Voice B reference WAV; repeat for multiple takes")\n'
        '    parser.add_argument("--identity-mix-b", type=float, default=0.5, help="Voice B weight in fused CAMPPlus style")\n'
        '    parser.add_argument("--fused-identity-no-prompt", type=str2bool, default=True, help="Remove anchor prompt mel/content so fused style is tested on its own")\n',
        "CLI arguments",
    )

    backup = target.with_name(target.name + ".ghostvoice-backup")
    if backup.exists():
        raise FileExistsError(f"Refusing to overwrite existing backup: {backup}")
    backup.write_text(target.read_text(encoding="utf-8"), encoding="utf-8")
    target.write_text(source, encoding="utf-8")
    return target


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("seed_vc_root", type=Path)
    args = parser.parse_args()
    patched = patch(args.seed_vc_root.resolve())
    print(f"Patched: {patched}")
    print(f"Backup:  {patched.with_name(patched.name + '.ghostvoice-backup')}")


if __name__ == "__main__":
    main()
