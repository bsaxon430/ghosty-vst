#!/bin/bash
# Ghost Voice: install CHOP's optional tools (AGPL-3.0-only, like Ghost Voice; see ../NOTICE.md). Build 9.
#   install-chop-tools.sh phrases   -> a private Python 3.10 + numpy, for PHRASES (OpenVPI's audio-slicer, MIT; ships with Ghost Voice)
#   install-chop-tools.sh words     -> the above + WhisperX 3.3.1 (m-bain/whisperX, BSD-2) and its models, for WORDS
#                                      (about 1.5 GB on disk: torch 2.2.2, faster-whisper "small", the wav2vec2 word aligner)
# Everything goes into one folder that can simply be deleted again:
#   ~/Library/Application Support/GhostBlock/Ghost Voice/choptools   (override with GHOSTVOICE_CHOPTOOLS)
# Progress lines: "GV_STEP <n>/<m> <words>"; the last line is "GV_INSTALLED" or "GV_FAILED <words>".
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
WHAT="${1:-phrases}"
DIR="${GHOSTVOICE_CHOPTOOLS:-$HOME/Library/Application Support/GhostBlock/Ghost Voice/choptools}"
UV_VERSION=0.5.11
fail() { echo "GV_FAILED $*"; exit 1; }
N=3; [ "$WHAT" = words ] && N=5
mkdir -p "$DIR" || fail "cannot make $DIR"
cd "$DIR" || fail "cannot open $DIR"

echo "GV_STEP 1/$N getting the Python installer"
case "$(uname -m)" in arm64) UVA=aarch64-apple-darwin ;; x86_64) UVA=x86_64-apple-darwin ;; *) UVA=x86_64-unknown-linux-gnu ;; esac
[ "$(uname -s)" = Linux ] && UVA="$(uname -m)-unknown-linux-gnu"
if [ ! -x uv/uv ]; then
  mkdir -p uv && curl -fsSL "https://github.com/astral-sh/uv/releases/download/$UV_VERSION/uv-$UVA.tar.gz" | tar -xz -C uv --strip-components 1 || fail "Python installer download"
fi
export UV_CACHE_DIR="$DIR/uv-cache" UV_PYTHON_INSTALL_DIR="$DIR/python"

echo "GV_STEP 2/$N a private Python 3.10"
[ -x venv/bin/python ] || ./uv/uv venv --python 3.10 venv -q || fail "python"
PY="$DIR/venv/bin/python"

echo "GV_STEP 3/$N the phrase finder (numpy)"
"$PY" -c "import numpy" 2>/dev/null || ./uv/uv pip install --python "$PY" numpy==1.26.4 -q > pip-phrases.log 2>&1 || fail "numpy (see $DIR/pip-phrases.log)"
cp "$HERE/chop_tools.py" "$DIR/chop_tools.py"; rm -rf "$DIR/third_party"; cp -R "$HERE/third_party" "$DIR/third_party"
"$PY" "$DIR/chop_tools.py" status | grep -q '"phrases": true' || fail "check"
touch phrases.ok
if [ "$WHAT" != words ]; then echo "GV_INSTALLED"; exit 0; fi

echo "GV_STEP 4/$N the word finder (torch 2.2.2 + WhisperX, about 1 GB)"
if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = x86_64 ]; then
  # Intel Macs: the exact, wheels-only package set (PyTorch's last macOS x86_64 wheels are 2.2.x)
  ./uv/uv pip install --python "$PY" -r "$HERE/requirements-words-macos-x86_64.txt" --only-binary :all: --no-binary docopt > pip-words.log 2>&1 \
    || { tail -n 25 pip-words.log; fail "packages (see $DIR/pip-words.log)"; }
elif [ "$(uname -s)" = Linux ]; then
  ./uv/uv pip install --python "$PY" --extra-index-url https://download.pytorch.org/whl/cpu --index-strategy unsafe-best-match \
    "torch==2.2.2+cpu" "torchaudio==2.2.2+cpu" whisperx==3.3.1 numpy==1.26.4 transformers==4.46.3 > pip-words.log 2>&1 \
    || { tail -n 25 pip-words.log; fail "packages (see $DIR/pip-words.log)"; }
else
  ./uv/uv pip install --python "$PY" torch==2.2.2 torchaudio==2.2.2 whisperx==3.3.1 numpy==1.26.4 transformers==4.46.3 > pip-words.log 2>&1 \
    || { tail -n 25 pip-words.log; fail "packages (see $DIR/pip-words.log)"; }
fi

echo "GV_STEP 5/$N the word models (about 500 MB, once)"
HF_HOME="$DIR/hf" TORCH_HOME="$DIR/torch" "$PY" - <<'PY' || fail "models"
import whisperx
whisperx.load_model("small", "cpu", compute_type="int8", language="en")
whisperx.load_align_model(language_code="en", device="cpu")
print("models ok")
PY
"$PY" "$DIR/chop_tools.py" status | grep -q '"words": true' || fail "check"
touch words.ok
echo "GV_INSTALLED"
