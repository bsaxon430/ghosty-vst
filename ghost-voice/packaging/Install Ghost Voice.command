#!/bin/bash
# Ghost Voice installer (Mac; adapted from Ghost Voice's): copies the plug-ins into your user plug-in folders and the app into Applications.
#
# It first moves every OLD Ghost Voice copy to the Trash (never deletes anything), so your DAW can only find this one:
#   - /Library/Audio/Plug-Ins/VST3, Components, CLAP   (the whole-Mac folders: asks for your Mac password once)
#   - ~/Library/Audio/Plug-Ins/VST3, Components, CLAP  (your own folders)
# An old copy is any plug-in named "Ghost Voice" / "GhostVoice", or with Ghost Voice's id (com.ghostblock.ghostvoice).
cd "$(dirname "$0")" || exit 1

GV_ID="com.ghostblock.ghostvoice"
SYSTEM_PLUGINS="${GHOSTVOICE_SYSTEM_PLUGINS:-/Library/Audio/Plug-Ins}"   # (override only for testing)
USER_PLUGINS="$HOME/Library/Audio/Plug-Ins"
TRASH="$HOME/.Trash"
STAMP="$(date +%Y-%m-%d-%H%M%S)"

# is this plug-in bundle a Ghost Voice? (by name or by bundle id)
is_ghost_voice() {
  local b="$1" name id
  name="$(basename "$b")"
  case "$name" in
    "Ghost Voice".*|"GhostVoice".*|"Ghost Voice "*|"GhostVoice "*) return 0 ;;
  esac
  id="$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$b/Contents/Info.plist" 2>/dev/null)"
  [ "$id" = "$GV_ID" ]
}

# every Ghost Voice bundle in <plug-ins folder>/{VST3,Components,CLAP}, one per line
find_old() {
  local root="$1" dir b
  for dir in VST3 Components CLAP; do
    [ -d "$root/$dir" ] || continue
    for b in "$root/$dir"/*.vst3 "$root/$dir"/*.component "$root/$dir"/*.clap; do
      [ -e "$b" ] || continue
      is_ghost_voice "$b" && printf '%s\n' "$b"
    done
  done
}

# where in the Trash a bundle goes (never overwrites something already in the Trash)
RESERVED=""   # names handed out in this run (the admin moves happen later, all at once)
trash_name() {
  local name base ext n=1 t
  name="$(basename "$1")"
  base="${name%.*}"; ext="${name##*.}"
  t="$TRASH/$base (old $STAMP).$ext"
  while [ -e "$t" ] || [[ "$RESERVED" == *"|$t|"* ]]; do n=$((n + 1)); t="$TRASH/$base (old $STAMP $n).$ext"; done
  RESERVED="$RESERVED|$t|"
  printf '%s' "$t"
}

mkdir -p "$TRASH"
moved=0

# 1. the whole-Mac folders: needs the Mac's admin password (macOS asks in its own window)
SYSTEM_OLD=()
while IFS= read -r line; do [ -n "$line" ] && SYSTEM_OLD+=("$line"); done < <(find_old "$SYSTEM_PLUGINS")
if [ ${#SYSTEM_OLD[@]} -gt 0 ]; then
  echo "Found an old Ghost Voice installed for the whole Mac:"
  printf '   %s\n' "${SYSTEM_OLD[@]}"
  if [ -w "$SYSTEM_PLUGINS/VST3" ] && [ -w "$SYSTEM_PLUGINS/Components" ] && { [ ! -d "$SYSTEM_PLUGINS/CLAP" ] || [ -w "$SYSTEM_PLUGINS/CLAP" ]; }; then
    for b in "${SYSTEM_OLD[@]}"; do t="$(trash_name "$b")"; RESERVED="$RESERVED|$t|"; mv "$b" "$t" && moved=$((moved + 1)); done
  else
    echo "Moving it to the Trash. Your Mac will ask for your password (this only moves files, nothing is deleted)."
    script="$(mktemp "${TMPDIR:-/tmp}/ghostvoice-cleanup.XXXXXX")"
    {
      echo "#!/bin/bash"
      for b in "${SYSTEM_OLD[@]}"; do
        t="$(trash_name "$b")"; RESERVED="$RESERVED|$t|"
        # moved into YOUR Trash and made yours, so emptying the Trash works as usual
        printf 'mv %q %q && chown -R %q %q\n' "$b" "$t" "$(id -un)" "$t"
      done
    } > "$script"
    chmod 755 "$script"
    if osascript -e "do shell script \"/bin/bash '$script'\" with administrator privileges" >/dev/null; then
      moved=$((moved + ${#SYSTEM_OLD[@]}))
    else
      echo ""
      echo "The old copies were NOT moved (the password was cancelled or refused). Your DAW may keep opening the old Ghost Voice."
      echo "Run this installer again, or drag them to the Trash yourself from:"
      printf '   %s\n' "${SYSTEM_OLD[@]}"
    fi
    rm -f "$script"   # (our own temporary file)
  fi
fi

# 2. your own folders (this also moves the previous version of this Ghost Voice out of the way)
while IFS= read -r b; do
  [ -n "$b" ] || continue
  t="$(trash_name "$b")"; RESERVED="$RESERVED|$t|"
  mv "$b" "$t" && moved=$((moved + 1))
done < <(find_old "$USER_PLUGINS")
[ $moved -gt 0 ] && echo "Moved $moved old Ghost Voice plug-in(s) to the Trash."

# 3. install this Ghost Voice
set -e
mkdir -p "$USER_PLUGINS/VST3" "$USER_PLUGINS/Components"
ditto "Plugins/Ghost Voice.vst3" "$USER_PLUGINS/VST3/Ghost Voice.vst3"
if [ -d "Plugins/Ghost Voice.component" ]; then ditto "Plugins/Ghost Voice.component" "$USER_PLUGINS/Components/Ghost Voice.component"; fi   # (builds made without the Xcode app have no AU)
set +e
if [ -e "/Applications/Ghost Voice.app" ]; then mv "/Applications/Ghost Voice.app" "$(trash_name "/Applications/Ghost Voice.app")"; fi
if [ -e "$HOME/Applications/Ghost Voice.app" ]; then mv "$HOME/Applications/Ghost Voice.app" "$(trash_name "$HOME/Applications/Ghost Voice.app")"; fi
ditto "Ghost Voice.app" "/Applications/Ghost Voice.app" 2>/dev/null || { mkdir -p "$HOME/Applications"; ditto "Ghost Voice.app" "$HOME/Applications/Ghost Voice.app"; }
xattr -dr com.apple.quarantine "$USER_PLUGINS/VST3/Ghost Voice.vst3" "$USER_PLUGINS/Components/Ghost Voice.component" 2>/dev/null
xattr -dr com.apple.quarantine "/Applications/Ghost Voice.app" "$HOME/Applications/Ghost Voice.app" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null   # Logic / GarageBand: forget the old AU

VERSION="$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$USER_PLUGINS/VST3/Ghost Voice.vst3/Contents/Info.plist" 2>/dev/null)"
echo ""
echo "Done! Ghost Voice ${VERSION:+$VERSION }is installed (it's under GhostBlock)."
echo "Now rescan plug-ins in your DAW. FL Studio: Options > Manage plugins > Find more plugins."
