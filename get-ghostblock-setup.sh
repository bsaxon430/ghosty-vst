#!/bin/bash
# GhostBlock Mac setup, in one step: downloads every file of Brandon's private GhostBlock-Setup release
# (plugins, sounds, presets, settings) and installs them. Run in Terminal:
#   bash <(curl -fsSL https://raw.githubusercontent.com/bsaxon430/ghosty-vst/builds/get-ghostblock-setup.sh)
# It asks for a read-only GitHub key for GhostBlock-Setup; the key is used for this download only and not saved.
# Safe to run again: files already downloaded are kept, a stopped download carries on.
set -euo pipefail
API="${GB_API:-https://api.github.com/repos/bsaxon430/GhostBlock-Setup}"
DIR="$HOME/Downloads/GhostBlock-Setup"
mkdir -p "$DIR"
echo "GhostBlock Mac setup"
echo
KEY="${GB_KEY:-}"
if [ -z "$KEY" ]; then printf "Paste your GhostBlock-Setup key (it won't show as you paste), then press Return: "; read -rs KEY < /dev/tty; echo; fi
[ -n "$KEY" ] || { echo "No key given."; exit 1; }
H=(-H "Authorization: Bearer $KEY" -H "X-GitHub-Api-Version: 2022-11-28")
REL=$(curl -fsS "${H[@]}" "$API/releases/tags/mac-setup") || { echo "That key didn't work (or this Mac is offline). Check the key and try again."; exit 1; }
LIST=$(printf '%s' "$REL" | /usr/bin/python3 -c "import sys,json;[print(a['id'],a['size'],a['name']) for a in json.load(sys.stdin)['assets']]")
N=$(printf '%s\n' "$LIST" | grep -c .)
TOTAL=$(printf '%s\n' "$LIST" | awk '{s+=$2} END {print s}')
FREE=$(df -k "$HOME" | awk 'NR==2 {print $4*1024}')
need=$((TOTAL * 2 + 5000000000))
echo "$N files, $((TOTAL / 1000000000)) GB to download."
if [ "$FREE" -lt "$need" ]; then
  echo "Not enough free space: this needs about $((need / 1000000000)) GB free (download + unpacked), this Mac has $((FREE / 1000000000)) GB."
  exit 1
fi
i=0
while read -r id size name; do
  i=$((i + 1)); f="$DIR/$name"
  if [ -f "$f" ] && [ "$(stat -f %z "$f")" = "$size" ]; then echo "[$i/$N] have $name"; continue; fi
  echo "[$i/$N] downloading $name ($((size / 1000000)) MB)"
  for t in 1 2 3 4 5; do
    curl -fL --progress-bar -C - "${H[@]}" -H "Accept: application/octet-stream" "$API/releases/assets/$id" -o "$f" && break
    echo "  connection dropped, carrying on ($t)"; sleep 5
  done
  [ "$(stat -f %z "$f")" = "$size" ] || { echo "$name didn't finish downloading. Run the same command again to carry on."; exit 1; }
done <<< "$LIST"
echo
echo "All files downloaded. Installing..."
bash "$DIR/install-ghostblock.sh" "$DIR"
echo
printf "Delete the downloaded files (about %s GB) now that everything is installed? [y/N] " "$((TOTAL / 1000000000))"
read -r ans < /dev/tty || ans=n
case "$ans" in y|Y|yes) rm -rf "$DIR"; echo "Deleted.";; *) echo "Kept in $DIR";; esac
