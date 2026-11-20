#!/bin/bash
# GhostBlock Mac setup, in one step: downloads Brandon's private GhostBlock-Setup (sounds, packs, breaks, presets,
# settings) and every GhostBlock plugin, installs it all, and cleans up. Paste into Terminal (with the key filled in):
#   curl -fsSL https://raw.githubusercontent.com/bsaxon430/ghosty-vst/builds/get-ghostblock-setup.sh | GB_KEY=THE-KEY bash
# Apple Silicon Macs get the universal plugins (the Intel-only copies from Brandon's MacBook wouldn't load there).
# Safe to run again: anything already downloaded is kept, a stopped download carries on.
set -euo pipefail
API="${GB_API:-https://api.github.com/repos/bsaxon430/GhostBlock-Setup}"
DIR="$HOME/Downloads/GhostBlock-Setup"
mkdir -p "$DIR"
caffeinate -dimsu -w $$ 2>/dev/null &   # keep the Mac awake until this finishes
echo
echo "  ================================================"
echo "    GhostBlock: installing everything on this Mac"
echo "    This takes a while (about 35 GB). Leave this"
echo "    window open and the Mac plugged in."
echo "  ================================================"
echo
KEY="${GB_KEY:-}"
if [ -z "$KEY" ]; then printf "Paste the GhostBlock key, then press Return: "; read -rs KEY < /dev/tty; echo; fi
[ -n "$KEY" ] || { echo "No key given."; exit 1; }
H=(-H "Authorization: Bearer $KEY" -H "X-GitHub-Api-Version: 2022-11-28")
REL=$(curl -fsS "${H[@]}" "$API/releases/tags/mac-setup" 2>/dev/null) || { echo "The key didn't work, or this Mac isn't online. Check the Wi-Fi and the key, then try again."; exit 1; }
ARM=0; [ "$(sysctl -in hw.optional.arm64 2>/dev/null || echo 0)" = 1 ] && ARM=1
LIST=$(printf '%s' "$REL" | /usr/bin/python3 -c "
import sys,json
for a in json.load(sys.stdin)['assets']:
    if $ARM and a['name']=='GhostBlock-Plugins.zip': continue   # Intel-only copies: Apple Silicon gets the universal ones
    print(a['id'],a['size'],a['name'])")
N=$(printf '%s\n' "$LIST" | grep -c .)
TOTAL=$(printf '%s\n' "$LIST" | awk '{s+=$2} END {print s}')
FREE=$(df -k "$HOME" | awk 'NR==2 {print $4*1024}')
need=$((TOTAL * 2 + 5000000000))
if [ "$FREE" -lt "$need" ]; then
  echo "This Mac needs about $((need / 1000000000)) GB free for this and has $((FREE / 1000000000)) GB. Free up space, then run it again."
  exit 1
fi
echo "Step 1 of 3: downloading $N files ($((TOTAL / 1000000000)) GB)"
i=0
while read -r id size name; do
  i=$((i + 1)); f="$DIR/$name"
  if [ -f "$f" ] && [ "$(stat -f %z "$f")" = "$size" ]; then echo "  [$i/$N] already have $name"; continue; fi
  echo "  [$i/$N] $name ($((size / 1000000)) MB)"
  for t in 1 2 3 4 5 6; do
    curl -fL --progress-bar -C - "${H[@]}" -H "Accept: application/octet-stream" "$API/releases/assets/$id" -o "$f" && break
    echo "  the connection dropped, carrying on..."; sleep 10
  done
  [ "$(stat -f %z "$f")" = "$size" ] || { echo; echo "The download stopped. Run the same line again and it carries on from here."; exit 1; }
done <<< "$LIST"
echo
echo "Step 2 of 3: putting the sounds, presets and settings in place"
bash "$DIR/install-ghostblock.sh" "$DIR"
if [ $ARM = 1 ]; then
  echo
  echo "Step 3 of 3: installing the plugins (Apple Silicon versions)"
  curl -fsSL https://raw.githubusercontent.com/bsaxon430/ghosty-vst/builds/install-plugins.sh | bash
else
  echo
  echo "Step 3 of 3: plugins installed with the rest"
fi
rm -rf "$DIR"
echo
echo "  ================================================"
echo "    ALL DONE. Open Ableton or FL Studio and rescan"
echo "    plug-ins. Everything is there."
echo "  ================================================"
