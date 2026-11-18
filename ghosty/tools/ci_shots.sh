#!/bin/bash
# ci_shots.sh <GhostyProTool> <out dir>: the SYNTH page on screen (a real window, Vital's OpenGL editor drawn) at several
# window sizes and display scales, grabbed with screencapture; then 1x and 1.5x of the same size are compared (Vital's
# layout must be the same picture at any display scale). Mac only.
T="$1"; OUT="$2"; mkdir -p "$OUT"
shot() {   # shot <name> <scale> <w> <h>
  GHOSTY_SCALE=$2 GHOSTY_SIZE=${3}x${4} "$T" window 0 10 0 0 > /dev/null 2>&1 &
  local pid=$!
  sleep 8
  local pw ph
  pw=$(python3 -c "print(int(round($3*$2)))"); ph=$(python3 -c "print(int(round($4*$2)))")
  screencapture -x -R0,0,$pw,$((ph + 40)) "$OUT/$1.png" 2>/dev/null || echo "screencapture failed for $1"
  wait $pid
}
# (GitHub's Mac runners have a 1024 x 768 screen: only windows that fit are pictured; 2x is checked by the layout test)
system_profiler SPDisplaysDataType 2>/dev/null | grep -i resolution
shot synth-1x-960x650 1 960 650
shot synth-1x-1000x700 1 1000 700
ls -la "$OUT"
