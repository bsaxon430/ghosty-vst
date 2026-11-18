#!/bin/bash
# Ghosty Pro 2 installer (Mac): copies the plug-ins into YOUR plug-in folders (~/Library/Audio/Plug-Ins) and the app into
# Applications. No password needed.
#
# Ghosty Pro 2 lives next to the old Ghosty Pro (3.x): it has its own name and id (io.ghostblock.ghostypro2), and this
# installer NEVER touches the old Ghosty Pro, wherever it is installed.
#
# The only things it tidies up: earlier test copies of THIS Vital-based synth that were installed under the old name
# "Ghosty Pro" in your own plug-in folders (id io.ghostblock.ghostypro with version 1.5.5 or 4.0.x). Those go to the
# Trash (never deleted). Anything with version 3.x (the old Ghosty Pro) stays exactly where it is.
cd "$(dirname "$0")" || exit 1

NAME="Ghosty Pro 2"
OLD_ID="io.ghostblock.ghostypro"
USER_PLUGINS="$HOME/Library/Audio/Plug-Ins"
TRASH="$HOME/.Trash"
STAMP="$(date +%Y-%m-%d-%H%M%S)"

plist_value() { /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null; }

# an earlier copy of this Vital-based synth under the old name? (old id AND version 1.5.5 or 4.0.x; never 3.x)
is_vital_based_ghosty_pro() {
  local b="$1" id version
  id="$(plist_value "$b" CFBundleIdentifier)"
  version="$(plist_value "$b" CFBundleShortVersionString)"
  [ "$id" = "$OLD_ID" ] || return 1
  case "$version" in
    1.5.5|4.0.*) return 0 ;;
    *) return 1 ;;
  esac
}

# a free name in the Trash (never overwrites anything already there)
trash_name() {
  local name base ext n=1 t
  name="$(basename "$1")"
  base="${name%.*}"; ext="${name##*.}"
  t="$TRASH/$base (old $STAMP).$ext"
  while [ -e "$t" ]; do n=$((n + 1)); t="$TRASH/$base (old $STAMP $n).$ext"; done
  printf '%s' "$t"
}

mkdir -p "$TRASH"
moved=0
# 1. earlier copies of this synth installed as "Ghosty Pro" in your own folders
for dir in VST3 Components CLAP; do
  for b in "$USER_PLUGINS/$dir"/*.vst3 "$USER_PLUGINS/$dir"/*.component "$USER_PLUGINS/$dir"/*.clap; do
    [ -e "$b" ] || continue
    case "$(basename "$b")" in "$NAME".*) continue ;; esac     # (Ghosty Pro 2 itself: replaced below)
    if is_vital_based_ghosty_pro "$b"; then
      mv "$b" "$(trash_name "$b")" && moved=$((moved + 1))
    fi
  done
done
for app in "/Applications/Ghosty Pro.app" "$HOME/Applications/Ghosty Pro.app"; do
  [ -e "$app" ] && is_vital_based_ghosty_pro "$app" && mv "$app" "$(trash_name "$app")" && moved=$((moved + 1))
done
[ $moved -gt 0 ] && echo "Moved $moved earlier test copy(s) of this synth (installed as \"Ghosty Pro\") to the Trash."

# 2. a previous Ghosty Pro 2 goes to the Trash, then this one goes in
for b in "$USER_PLUGINS/VST3/$NAME.vst3" "$USER_PLUGINS/Components/$NAME.component" "/Applications/$NAME.app" "$HOME/Applications/$NAME.app"; do
  [ -e "$b" ] && mv "$b" "$(trash_name "$b")"
done
set -e
mkdir -p "$USER_PLUGINS/VST3" "$USER_PLUGINS/Components"
ditto "Plugins/$NAME.vst3" "$USER_PLUGINS/VST3/$NAME.vst3"
[ -d "Plugins/$NAME.component" ] && ditto "Plugins/$NAME.component" "$USER_PLUGINS/Components/$NAME.component" || true   # (builds from Brandon's Mac are VST3 only)
set +e
ditto "$NAME.app" "/Applications/$NAME.app" 2>/dev/null || { mkdir -p "$HOME/Applications"; ditto "$NAME.app" "$HOME/Applications/$NAME.app"; }
xattr -dr com.apple.quarantine "$USER_PLUGINS/VST3/$NAME.vst3" "$USER_PLUGINS/Components/$NAME.component" 2>/dev/null
xattr -dr com.apple.quarantine "/Applications/$NAME.app" "$HOME/Applications/$NAME.app" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null   # Logic / GarageBand: pick up the new AU

VERSION="$(plist_value "$USER_PLUGINS/VST3/$NAME.vst3" CFBundleShortVersionString)"
echo ""
echo "Done! $NAME ${VERSION:+$VERSION }is installed (it's under GhostBlock, next to your old Ghosty Pro)."
echo "Now rescan plug-ins in your DAW. FL Studio: Options > Manage plugins > Find more plugins."
