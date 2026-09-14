#!/bin/bash
# The three top-right chrome buttons: New game, Pause, Mute.
#
# All three are checked WHILE THE SIMULATION IS PAUSED, which is the point of them. A HUD button
# that only works while the game is running is broken in the one case a player most needs it -
# you pause, and then you reach for "new game" or "mute". Each is handled somewhere that runs on
# non-ticking passes: New game submits a command (drained before the tick decision), Pause is the
# engine's own INPUT_PAUSE serviced by Scene::BeginPass, Mute is read in UpdateView.
SP="$(dirname "$0")"
MCP=http://127.0.0.1:8765/mcp

tool(){ curl -s -X POST $MCP -H "Content-Type: application/json" \
  -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$1\",\"arguments\":$2}}"; }
field(){ tool tetris_state '{}' | python -c "
import json,sys
print(json.loads(json.load(sys.stdin)['result']['content'][0]['text'])['$1'])"; }
click(){ powershell -NoProfile -ExecutionPolicy Bypass -File "$SP/click.ps1" -x $1 -y $2 -action down >/dev/null
         sleep 0.3
         powershell -NoProfile -ExecutionPolicy Bypass -File "$SP/click.ps1" -x $1 -y $2 -action up >/dev/null
         sleep 0.6; }

fails=0
check(){ if [ "$2" == "$3" ]; then echo "  OK    $1  ($3)"; else echo "  FAIL  $1  expected $2, got $3"; fails=$((fails+1)); fi; }

# Chrome cluster for a 1200x900 window: 64px buttons, 12px gap, 28px inset, built leftwards from
# the right edge as MUTE, PAUSE, NEW. y = 28..92, centre 60.
MUTE_X=1140; PAUSE_X=1064; NEW_X=988; TOP_Y=60

tool tetris_pause '{"paused":false}' >/dev/null; sleep 0.5

echo "--- PAUSE button (engine action, serviced by Scene::BeginPass) ---"
p0=$(field paused); click $PAUSE_X $TOP_Y; p1=$(field paused)
check "pressing II from running pauses" "True" "$p1"
click $PAUSE_X $TOP_Y; p2=$(field paused)
check "pressing II again resumes" "False" "$p2"

echo
echo "--- MUTE button (chrome, read in UpdateView) ---"
click $PAUSE_X $TOP_Y >/dev/null   # pause first, to prove it works while paused
check "paused for the remaining checks" "True" "$(field paused)"
s0=$(field sound); click $MUTE_X $TOP_Y; s1=$(field sound)
check "mute toggles sound WHILE PAUSED" "$( [ "$s0" == "True" ] && echo False || echo True )" "$s1"
click $MUTE_X $TOP_Y; s2=$(field sound)
check "mute toggles back" "$s0" "$s2"

echo
echo "--- NEW GAME button (command, drained before the tick decision) ---"
# Put something on the board first, so a restart is observable as more than a tick counter reset.
tool tetris_pause '{"paused":false}' >/dev/null; sleep 2.0
tool tetris_pause '{"paused":true}' >/dev/null; sleep 0.3
g0=$(field game_ticks)
check "game has been running" "yes" "$( [ "$g0" -gt 30 ] && echo yes || echo "no($g0)" )"
click $NEW_X $TOP_Y; g1=$(field game_ticks)
check "new game resets the game clock WHILE PAUSED" "yes" \
      "$( [ "$g1" -lt "$g0" ] && echo yes || echo "no($g0 -> $g1)" )"
check "still paused afterwards (restart does not resume)" "True" "$(field paused)"

echo
if [ $fails -eq 0 ]; then echo "ALL CHECKS PASSED"; else echo "$fails CHECK(S) FAILED"; fi
exit $fails
