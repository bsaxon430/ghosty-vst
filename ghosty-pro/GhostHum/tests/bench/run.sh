#!/bin/bash
# Runs one system over the benchmark sets. Usage: run.sh <system> <out_root> <vocadito_dir> <avp_dir> [HumBench path] [python]
#   system: old | new (HumBench engines)  |  basicpitch | pyin | crepe (refs.py, melody only)
# Writes <out_root>/melody/<system>/vocadito_N.json and <out_root>/beat/<system>/<avp file>.json
S=$1; OUT=$2; VOC=$3; AVP=$4; HB=${5:-./HumBench}; PY=${6:-python3}; HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT/melody/$S" "$OUT/beat/$S"
case $S in
  old|new)
    for i in $(seq 1 40); do "$HB" melody "$VOC/Audio/vocadito_$i.wav" "$OUT/melody/$S/vocadito_$i.json" $S || echo "FAIL melody $i"; done
    for w in "$AVP"/AVP_Dataset/*/*/*.wav; do k=$(basename "${w%.wav}"); "$HB" beat "$w" "$OUT/beat/$S/$k.json" $S || echo "FAIL beat $k"; done ;;
  *)
    for i in $(seq 1 40); do "$PY" "$HERE/refs.py" $S "$VOC/Audio/vocadito_$i.wav" "$OUT/melody/$S/vocadito_$i.json" > /dev/null 2>&1 || echo "FAIL $S $i"; done ;;
esac
echo "done $S"
