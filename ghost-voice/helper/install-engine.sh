#!/bin/bash
# Ghost Voice: install the optional neural voice engine (AGPL-3.0-only, like Ghost Voice; see ../NOTICE.md).
#   install-engine.sh [LAB_DIR]        LAB_DIR = the folder holding the lab's ghostvoice/ package and patches/ (default: next to this script)
# Everything goes into one folder that can simply be deleted again:
#   ~/Library/Application Support/GhostBlock/Ghost Voice/engine   (override with GHOSTVOICE_ENGINE)
# What it fetches (about 4.5 GB on disk, most of it the models; ~2.5 GB download):
#   uv 0.5.11 (Astral, MIT / Apache-2.0)        -> a private Python 3.10, nothing touches the system Python
#   Seed-VC @ 51383ef (Plachtaa/seed-vc, GPL-3.0) and its pinned Python packages: torch 2.5.1 on Apple Silicon; torch 2.2.2 on
#   Intel Macs (build 3: PyTorch's last macOS x86_64 wheels are 2.2.x; GHOSTVOICE_TORCH overrides)
#   the models Seed-VC's singing conversion downloads itself: Plachta/Seed-VC (GPL-3.0), nvidia/bigvgan_v2_44khz_128band_512x
#   (MIT), openai/whisper-small (Apache-2.0), lj1995/VoiceConversionWebUI rmvpe.pt (MIT), funasr/campplus (Apache-2.0)
# Progress lines: "GV_STEP <n>/7 <words>"; the last line is "GV_INSTALLED" or "GV_FAILED <words>".
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LAB="$(cd "${1:-$HERE/lab}" && pwd)" || { echo "GV_FAILED no lab folder"; exit 1; }
ENGINE="${GHOSTVOICE_ENGINE:-$HOME/Library/Application Support/GhostBlock/Ghost Voice/engine}"
SEEDVC_SHA=51383efd921027683c89e5348211d93ff12ac2a8
UV_VERSION=0.5.11
fail() { echo "GV_FAILED $*"; exit 1; }
mkdir -p "$ENGINE" || fail "cannot make $ENGINE"
cd "$ENGINE" || fail "cannot open $ENGINE"

echo "GV_STEP 1/7 getting uv $UV_VERSION (Python installer)"
case "$(uname -m)" in arm64) UVA=aarch64-apple-darwin ;; x86_64) UVA=x86_64-apple-darwin ;; *) UVA=x86_64-unknown-linux-gnu ;; esac
[ "$(uname -s)" = Linux ] && UVA="$(uname -m)-unknown-linux-gnu"
if [ ! -x uv/uv ]; then
  mkdir -p uv && curl -fsSL "https://github.com/astral-sh/uv/releases/download/$UV_VERSION/uv-$UVA.tar.gz" | tar -xz -C uv --strip-components 1 || fail "uv download"
fi
export UV_CACHE_DIR="$ENGINE/uv-cache" UV_PYTHON_INSTALL_DIR="$ENGINE/python"

echo "GV_STEP 2/7 a private Python 3.10"
[ -x venv/bin/python ] || ./uv/uv venv --python 3.10 venv -q || fail "python"
PY="$ENGINE/venv/bin/python"

echo "GV_STEP 3/7 Seed-VC @ ${SEEDVC_SHA:0:7}"
if [ ! -f seed-vc/inference.py ]; then
  rm -rf seed-vc seed-vc.zip
  curl -fsSL -o seed-vc.zip "https://codeload.github.com/Plachtaa/seed-vc/zip/$SEEDVC_SHA" || fail "Seed-VC download"
  "$PY" -c "import zipfile,sys; zipfile.ZipFile('seed-vc.zip').extractall('.')" && mv "seed-vc-$SEEDVC_SHA" seed-vc && rm seed-vc.zip || fail "Seed-VC unpack"
fi

# torch per machine: Apple Silicon (and Linux test machines) 2.5.1; Intel Macs (and anything running as x86_64, e.g. under
# Rosetta) 2.2.2, the last release with macOS x86_64 wheels
TORCH=2.5.1
[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = x86_64 ] && TORCH=2.2.2
TORCH="${GHOSTVOICE_TORCH:-$TORCH}"
echo "GV_STEP 4/7 Python packages (torch $TORCH on $(uname -m) and Seed-VC's pins)"
printf 'torch==%s\ntorchaudio==%s\nnumpy==1.26.4\n' "$TORCH" "$TORCH" > pins.txt
if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = x86_64 ] && [ -z "${GHOSTVOICE_TORCH:-}" ]; then
  # Intel Macs: the exact, wheels-only package set (requirements-macos-x86_64.txt explains why)
  ./uv/uv pip install --python "$PY" -r "$HERE/requirements-macos-x86_64.txt" --only-binary :all: \
    --no-binary argbind --no-binary antlr4-python3-runtime --no-binary randomname > pip.log 2>&1 || { tail -n 25 pip.log; fail "packages (see $ENGINE/pip.log)"; }
else
  ./uv/uv pip install --python "$PY" -c pins.txt torch torchaudio scipy==1.13.1 librosa==0.10.2 "huggingface-hub>=0.28.1" munch==4.0.0 \
    einops==0.8.0 descript-audio-codec==1.0.0 transformers==4.46.3 soundfile==0.12.1 numpy pyyaml hydra-core==1.3.2 accelerate > pip.log 2>&1 \
    || { tail -n 25 pip.log; fail "packages (see $ENGINE/pip.log)"; }
fi

echo "GV_STEP 5/7 the Ghost Voice blend (the lab's patch on Seed-VC)"
rm -rf lab && mkdir -p lab && cp -R "$LAB/ghostvoice" "$LAB/patches" lab/ || fail "lab files"
grep -q GHOSTVOICE_SEEDVC_FUSION_PATCH seed-vc/inference.py || "$PY" lab/patches/apply_seedvc_fusion_patch.py seed-vc || fail "patch"

echo "GV_STEP 6/7 the models (about 2.5 GB, once)"
(cd seed-vc && HF_HUB_CACHE=./checkpoints/hf_cache "$PY" - <<'PY') || fail "models"
from hf_utils import load_custom_model_from_hf
from huggingface_hub import hf_hub_download
load_custom_model_from_hf("Plachta/Seed-VC", "DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema_v2.pth", "config_dit_mel_seed_uvit_whisper_base_f0_44k.yml")
load_custom_model_from_hf("lj1995/VoiceConversionWebUI", "rmvpe.pt", None)
load_custom_model_from_hf("funasr/campplus", "campplus_cn_common.bin", None)
for f in ("bigvgan_generator.pt", "config.json"):
    hf_hub_download("nvidia/bigvgan_v2_44khz_128band_512x", f)
for f in ("config.json", "model.safetensors", "preprocessor_config.json"):
    hf_hub_download("openai/whisper-small", f)
print("models ok")
PY

echo "GV_STEP 7/7 checking"
GHOSTVOICE_ENGINE="$ENGINE" "$PY" "$HERE/ghostvoice_engine.py" status || fail "check"
cp "$HERE/ghostvoice_engine.py" "$ENGINE/ghostvoice_engine.py"
echo "GV_INSTALLED"
