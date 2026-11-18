"""Gradio UI for creating a Seed-VC-native fused CAMPPlus style profile."""
from __future__ import annotations
from functools import lru_cache
import json
from pathlib import Path
import torch
import gradio as gr

from ghostvoice.seedvc_style import (
    blend_campplus_styles,
    enroll_campplus_identity,
    load_seedvc_campplus,
    save_style_profile,
)


@lru_cache(maxsize=2)
def cached_campplus(seed_root: str, checkpoint: str, device: str):
    return load_seedvc_campplus(seed_root, checkpoint, device)


def run_fusion(a_files, b_files, seed_root, checkpoint, mix_b, device):
    if not a_files or not b_files:
        raise gr.Error("Upload at least one clean reference take for both Voice A and Voice B.")
    if not seed_root or not checkpoint:
        raise gr.Error("Enter the local Seed-VC folder and CAMPPlus checkpoint path.")
    try:
        model = cached_campplus(str(seed_root), str(checkpoint), str(device or "cpu"))
        a, report_a = enroll_campplus_identity(a_files, model, device)
        b, report_b = enroll_campplus_identity(b_files, model, device)
        fused, report_fusion = blend_campplus_styles(a, b, float(mix_b))
        report = {
            "backend": "Seed-VC V1 CAMPPlus style space",
            "voice_a": report_a,
            "voice_b": report_b,
            "fusion": report_fusion,
            "status": "style_profile_created_audio_not_rendered",
            "warning": "This is model-native style interpolation, not a trained identity-fusion network. Run the patched Seed-VC SVC experiment and listen before judging the result.",
        }
        out_prefix = Path("outputs") / "ghost_voice_c_seedvc"
        npz_path, json_path = save_style_profile(out_prefix, fused, report, a, b)
        return str(npz_path), json.dumps(report, indent=2)
    except Exception as exc:
        raise gr.Error(f"Fusion profile creation failed: {exc}")


with gr.Blocks(title="Ghost Voice — Seed-VC Identity Fusion") as demo:
    gr.Markdown("# GHOST VOICE — TWO-TO-ONE IDENTITY EXPERIMENT")
    gr.Markdown(
        "Upload multiple clean reference takes for each voice. This creates a 192-D CAMPPlus style profile in Seed-VC's own conditioning space. "
        "It does **not** render audio by itself; use the patched Seed-VC SVC pipeline described in README.md to generate the singing test."
    )
    with gr.Row():
        voice_a = gr.File(label="VOICE A — reference takes", file_count="multiple", file_types=[".wav", ".flac"], type="filepath")
        voice_b = gr.File(label="VOICE B — reference takes", file_count="multiple", file_types=[".wav", ".flac"], type="filepath")
    mix = gr.Slider(0.0, 1.0, value=0.5, step=0.01, label="Voice B contribution")
    with gr.Row():
        seed_root = gr.Textbox(label="Local Seed-VC repository folder", placeholder="/path/to/seed-vc")
        checkpoint = gr.Textbox(label="CAMPPlus checkpoint", placeholder="/path/to/campplus_cn_common.bin")
    device = gr.Dropdown(choices=["cuda", "mps", "cpu"], value="cuda" if torch.cuda.is_available() else ("mps" if torch.backends.mps.is_available() else "cpu"), label="Device")
    create = gr.Button("CREATE FUSED VOICE C PROFILE", variant="primary")
    profile = gr.File(label="Fused Seed-VC style profile (.npz)")
    report = gr.Code(label="Enrollment and fusion diagnostics", language="json")
    create.click(run_fusion, inputs=[voice_a, voice_b, seed_root, checkpoint, mix, device], outputs=[profile, report])
    gr.Markdown("Use only voices you own or have explicit permission to process. The Seed-VC research backend is GPL-3.0; review licensing before any commercial distribution.")


if __name__ == "__main__":
    demo.launch()
