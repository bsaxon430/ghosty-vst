"""Legacy ECAPA research UI. Use seedvc_app.py for the renderer-native path."""
from pathlib import Path
import json
import gradio as gr
from ghostvoice.pipeline import build_identity


def run(a_files, b_files, mix_b):
    if not a_files or not b_files:
        raise gr.Error("Add at least one reference take to both Voice A and Voice B.")
    out = Path("outputs") / "legacy_ecapa_profile"
    try:
        report = build_identity(a_files, b_files, float(mix_b), str(out), novelty=0.0)
    except Exception as e:
        raise gr.Error(f"Legacy ECAPA analysis failed: {e}")
    return str(out.with_suffix(".npz")), json.dumps(report, indent=2)


with gr.Blocks(title="Ghost Voice — Legacy ECAPA Baseline") as demo:
    gr.Markdown("# GHOST VOICE — LEGACY SPEECH-EMBEDDING BASELINE")
    gr.Markdown(
        "This tab is retained for historical comparison only. It uses speech-trained ECAPA embeddings and its profiles are **not compatible** with Seed-VC or any singing renderer. "
        "For the current model-native experiment, run `python seedvc_app.py`."
    )
    with gr.Row():
        vocal_a = gr.File(label="VOICE A — WAV references", file_count="multiple", file_types=[".wav"], type="filepath")
        vocal_b = gr.File(label="VOICE B — WAV references", file_count="multiple", file_types=[".wav"], type="filepath")
    mix = gr.Slider(0, 1, value=0.5, step=0.01, label="Voice B contribution")
    go = gr.Button("ENROLL LEGACY BASELINE", variant="secondary")
    profile = gr.File(label="Legacy ECAPA profile (.npz)")
    report = gr.Code(label="Analysis report", language="json")
    go.click(run, inputs=[vocal_a, vocal_b, mix], outputs=[profile, report])

if __name__ == "__main__":
    demo.launch()
