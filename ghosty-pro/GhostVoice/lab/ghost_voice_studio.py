"""Actual rendering UI: requires separate patched Seed-VC backend + checkpoint.

Does not fake audio from style embeddings. The button runs full inference
and returns actual recorded output files when the model has succeeded.
"""
from __future__ import annotations
import json
from datetime import datetime, timezone
from pathlib import Path
from uuid import uuid4
import gradio as gr
from ghostvoice.bench import run_experiment


def create_voice(a, b, performance, seed_root, steps, right_to_use):
    if not right_to_use:
        raise gr.Error('Please confirm permission to use these recordings.')
    if not a or not b or not performance:
        raise gr.Error('Provide A and B reference WAVs and a source singing performance.')
    out = Path('outputs') / f'experiment_{datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")}_{uuid4().hex[:7]}'
    try:
        result = run_experiment(a=a, b=b, source=performance,
                                seed_root=seed_root, output=out,
                                mixes=(0.5,), steps=int(steps))
        renders = {i['job_id']: i.get('wav') for i in result['renders']}
        return (renders['anchor_a'], renders['anchor_b'], renders['fusion_050'],
                str(out / 'experiment.json'), str(out / 'blind_listening' / 'index.html'))
    except Exception as exc:
        raise gr.Error(f'Render failed: {exc}') from exc


with gr.Blocks(title='Ghost Voice — Singing Identity Fusion Research') as demo:
    gr.Markdown('# GHOST VOICE — CREATE VOICE C')
    gr.Markdown('Experimental **real audio rendering** through separately installed Seed-VC. Not a production-ready voice generator. A and B anchors use the same source performance and model settings as C.')
    with gr.Row():
        voice_a = gr.File(label='Voice A — reference WAV(s)', file_count='multiple', type='filepath')
        voice_b = gr.File(label='Voice B — reference WAV(s)', file_count='multiple', type='filepath')
    source = gr.Audio(label='Singing performance to reproduce', type='filepath', sources=['upload'])
    root = gr.Textbox(label='Patched Seed-VC checkout directory', placeholder='/path/to/seed-vc')
    steps = gr.Slider(15, 70, value=40, step=1, label='Diffusion steps')
    permission = gr.Checkbox(label='I have the right to process these voices for this research experiment')
    create = gr.Button('CREATE & RENDER VOICE C (EXPERIMENT)', variant='primary')
    with gr.Row():
        a = gr.Audio(label='Voice A anchor — same melody', interactive=False)
        b = gr.Audio(label='Voice B anchor — same melody', interactive=False)
        c = gr.Audio(label='Voice C fused — same melody', interactive=False)
    report = gr.File(label='Full experimental report')
    blind = gr.File(label='Blind listening test page')
    create.click(create_voice, inputs=[voice_a, voice_b, source, root, steps, permission],
                 outputs=[a, b, c, report, blind])
    gr.Markdown('No audio is uploaded to a remote inference API by this UI. Check installed dependencies and model licenses. Do not distribute GPL model code inside proprietary builds without complying with its obligations.')


if __name__ == '__main__':
    demo.launch()
