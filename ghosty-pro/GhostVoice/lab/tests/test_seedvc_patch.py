from pathlib import Path
import importlib.util


def test_patch_script_modifies_seedvc_inference_and_is_idempotent(tmp_path):
    project = Path(__file__).resolve().parents[1]
    patch_path = project / "patches" / "apply_seedvc_fusion_patch.py"
    spec = importlib.util.spec_from_file_location("ghostvoice_seedvc_patcher", patch_path)
    patcher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patcher)

    seed_root = tmp_path / "seed-vc"
    seed_root.mkdir()
    original = '''from hf_utils import load_custom_model_from_hf

def main(args):
    style2 = campplus_model(feat2.unsqueeze(0))
    while True:
        cat_condition = torch.cat([prompt_condition, chunk_cond], dim=1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fp16", type=str2bool, default=True)
    args = parser.parse_args()
'''
    inference = seed_root / "inference.py"
    inference.write_text(original)
    patcher.patch(seed_root)
    patched = inference.read_text()
    compile(patched, str(inference), "exec")
    assert "build_fused_style_condition" in patched
    assert "cat_condition = chunk_cond" in patched
    assert inference.with_name("inference.py.ghostvoice-backup").read_text() == original

    try:
        patcher.patch(seed_root)
    except RuntimeError as exc:
        assert "already has" in str(exc)
    else:
        raise AssertionError("Patch should refuse to apply twice")
