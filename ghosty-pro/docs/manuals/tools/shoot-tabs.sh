#!/bin/bash
APP="$1"; OUT="$2"; shift 2
HOME=/root/work/manual-shots/home; mkdir -p $HOME
"$APP" >/dev/null 2>&1 &
PID=$!
sleep 6
WID=$(xdotool search --pid $PID 2>/dev/null | tail -1)
eval $(xdotool getwindowgeometry --shell $WID)
echo "win $X $Y $WIDTH $HEIGHT" > "$OUT/geom.txt"
for spec in "$@"; do  # name:x:y (window-relative)
  n=${spec%%:*}; r=${spec#*:}; cx=${r%%:*}; cy=${r#*:}
  xdotool mousemove $((X+cx)) $((Y+cy)) click 1; sleep 2; [ -n "$GOTX" ] && { xdotool mousemove $((X+GOTX)) $((Y+GOTY)) click 1; sleep 1.5; }
  import -window root /tmp/shot_full.png; convert /tmp/shot_full.png -crop ${WIDTH}x${HEIGHT}+${X}+${Y} +repage "$OUT/$n.png"
done
kill $PID
