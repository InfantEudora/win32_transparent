#!/bin/bash
# End-to-end check of the on-screen buttons driven by a synthetic Win32 mouse as pointer 0.
#
# Every press goes through PostMessage -> WndProc -> InputController::HandleMessage ->
# SubmitPointer, i.e. exactly the path a real click takes. Nothing here reaches into the input
# system directly.
#
# TWO THINGS THE HARNESS HAS TO DO THAT ARE NOT OBVIOUS:
#  - MAKE THE APP CONSIDER ITSELF FOCUSED, by posting WM_ACTIVATE (inside click.ps1) and NEVER by
#    calling SetForegroundWindow. SubmitSystemKey drops key-DOWNS while !f_has_focus, deliberately,
#    so an unfocused press hit-tests perfectly and is then discarded - which looks exactly like a
#    broken button. Grabbing the real foreground would fix that and yank the window in front of
#    whoever is at the keyboard on every click, and race whatever they are doing. See README.md.
#  - NOT USE A STALE APP. Every app binds port 8765 and a second one starts happily while its
#    server silently fails to bind, so a leftover instance answers instead and the suite reports
#    on the WRONG BUILD. Checked below before anything else.
#
# Both movement and rotation are now checked PAUSED AND STEPPED. Until backlog item 88 rotation
# could not be - an edge raised on a non-ticking pass was cleared before any tick saw it - and the
# split that forced is gone. See the note above the rotation checks.
SP="$(dirname "$0")"
MCP=http://127.0.0.1:8765/mcp

call(){ curl -s -X POST $MCP -H "Content-Type: application/json" -d "$1"; }
tool(){ call "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$1\",\"arguments\":$2}}"; }
field(){ tool tetris_state '{}' | python -c "
import json,sys
print(json.loads(json.load(sys.stdin)['result']['content'][0]['text'])['$1'])"; }
click(){ powershell -NoProfile -ExecutionPolicy Bypass -File "$SP/click.ps1" -x $1 -y $2 -action $3 >/dev/null; sleep 0.4; }
step(){ tool tetris_step "{\"num_ticks\":$1}" >/dev/null; }

fails=0
check(){ if [ "$2" == "$3" ]; then echo "  OK    $1  ($3)"; else echo "  FAIL  $1  expected $2, got $3"; fails=$((fails+1)); fi; }

# IS THE APP ANSWERING THE ONE THAT WAS JUST BUILT? A suite that reports on a stale build is worse
# than one that fails: it is confidently wrong. The exe cannot be relinked while it runs
# ("Permission denied"), so a build newer than the running process means the rebuild went somewhere
# the running app is not - the classic being a forgotten taskkill after an edit.
# Resolved here rather than in PowerShell so it does not depend on the directory the suite was
# launched from - and quoted all the way through, because this repo's path contains a space.
# The braces matter: `cd X && pwd -W || pwd` parses as `((cd && pwd -W) || pwd)` only by accident -
# with a second `cd` in the fallback it becomes `((A && B) || C) && D` and BOTH pwds run, so the
# path arrives as two concatenated lines and every lookup silently misses. Grouping the fallback
# keeps it to one cd and one answer. pwd -W is MSYS's Windows-form path, which is what PowerShell
# needs; plain pwd is the fallback on a shell without it.
TETRIS_EXE="$(cd "$SP/../../apps/tetris" && { pwd -W 2>/dev/null || pwd; })/build/tetris.exe"
staleness=$(powershell -NoProfile -Command "
  \$p = Get-Process tetris -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not \$p) { 'noproc'; exit }
  \$exe = Get-Item '$TETRIS_EXE' -ErrorAction SilentlyContinue
  if (-not \$exe) { 'noexe'; exit }
  if (\$exe.LastWriteTime -gt \$p.StartTime) { 'stale' } else { 'ok' }" 2>/dev/null | tr -d '\r')
case "$staleness" in
  ok) ;;
  noproc) echo "FAIL: no tetris.exe is running - start apps/tetris first (it owns port 8765)"; exit 1;;
  stale)  echo "FAIL: tetris.exe on disk is NEWER than the running process - you are about to test"
          echo "      the previous build. Stop it (taskkill //F //IM tetris.exe) and restart it."; exit 1;;
  *)      echo "note: could not determine whether the running app is current ($staleness)";;
esac

# Button centres for a 1200x900 window: size 88, gap 12, inset 28, bottom row y = 900-28-88 = 784.
# Left cluster runs rightwards from the left edge; right cluster runs LEFTWARDS from the right
# edge, so ">" is outermost on the right exactly as "<" is on the left.
LEFT_X=72;   DOWN_X=172;  CCW_X=272    # left thumb:  <   v   CCW
DROP_X=928;  CW_X=1028;   RIGHT_X=1128 # right thumb: DROP CW  >
ROW_Y=828
NONE_X=600;  NONE_Y=450      # middle of the board - no button there

tool tetris_restart '{"seed":7}' >/dev/null
tool tetris_pause '{"paused":true}' >/dev/null
# A restart leaves no piece for a few ticks, and every check compares a value against itself - so
# start from a state where a piece IS falling, or the first comparison measures a spawn.
for i in $(seq 1 40); do
  if [ "$(field phase)" == "falling" ]; then break; fi
  step 1
done
check "a piece is falling before the first press" "falling" "$(field phase)"
echo "start: piece=$(field piece) x=$(field piece_x) rot=$(field piece_rotation)"

echo
echo "--- LEVEL-triggered actions, paused and single-stepped (deterministic) ---"
x0=$(field piece_x); click $LEFT_X $ROW_Y down; step 2; x1=$(field piece_x); click $LEFT_X $ROW_Y up
check "LEFT button moves the piece left" $((x0-1)) $x1

x0=$(field piece_x); click $RIGHT_X $ROW_Y down; step 2; x1=$(field piece_x); click $RIGHT_X $ROW_Y up
check "RIGHT button moves it back" $((x0+1)) $x1

x0=$(field piece_x); r0=$(field piece_rotation)
click $NONE_X $NONE_Y down; step 3; x1=$(field piece_x); r1=$(field piece_rotation)
click $NONE_X $NONE_Y up
check "a press on empty space moves nothing" "$x0" "$x1"
check "a press on empty space rotates nothing" "$r0" "$r1"

# pieces_placed is captured across this pair because 60 ticks of gravity can lock the piece and
# spawn the next one - and a fresh piece appears at the SPAWN COLUMN, so "did x change" would be
# measuring a spawn rather than a press. Seen once as "expected 0, got 3" with nothing wrong.
p0=$(field pieces_placed)
x0=$(field piece_x); click $LEFT_X $ROW_Y down; step 40; x1=$(field piece_x)
check "a HELD button auto-repeats (DAS/ARR works through a touch button)" "yes" \
      "$( [ $((x0-x1)) -gt 1 ] && echo yes || echo "no(moved $((x0-x1)))" )"
click $LEFT_X $ROW_Y up; step 20; x2=$(field piece_x)
check "no new piece spawned during the hold (else the next check is meaningless)" "$p0" "$(field pieces_placed)"
check "releasing it stops the repeat" "$x1" "$x2"

# EDGE-triggered actions, PAUSED AND SINGLE-STEPPED - which became possible only when backlog
# item 88 closed, and this block is what that fix is for.
#
# Rotation fires on WasKeyPressed, true for exactly one tick. Until item 88 the physics loop's
# non-ticking passes drained the event, raised the edge and had NextInput clear it again before any
# tick could see it - so these checks had to run UNPAUSED, against the clock, and a third check
# asserted the loss on purpose so that a fix would fail loudly here. It did.
#
# An unread edge now survives until a ticking pass consumes it, so rotation is testable the same
# deterministic, timing-free way movement above already was: press, step, look.
echo
echo "--- EDGE-triggered actions, paused and single-stepped (item 88) ---"
tool tetris_pause '{"paused":true}' >/dev/null; sleep 0.3

r0=$(field piece_rotation); click $CW_X $ROW_Y down; click $CW_X $ROW_Y up; step 2
r1=$(field piece_rotation)
check "CW button rotates the piece under sim_step" "changed" \
      "$( [ "$r0" != "$r1" ] && echo changed || echo "same($r0)" )"

r0=$(field piece_rotation); click $CCW_X $ROW_Y down; click $CCW_X $ROW_Y up; step 2
r1=$(field piece_rotation)
check "CCW button rotates the piece under sim_step" "changed" \
      "$( [ "$r0" != "$r1" ] && echo changed || echo "same($r0)" )"

# THE OTHER HALF OF ITEM 88'S RULE, and the one a careless fix breaks: an edge must be CONSUMED by
# the tick that reads it, not merely kept. Nothing reads rotation while paused, so if the press
# were being re-delivered rather than consumed, stepping again would rotate a second time from the
# same single click - and one press would turn the piece for as long as the game stayed paused.
r1=$(field piece_rotation); step 4; r2=$(field piece_rotation)
check "one press is one rotation - the edge is consumed, not re-delivered" "$r1" "$r2"

echo
if [ $fails -eq 0 ]; then echo "ALL CHECKS PASSED"; else echo "$fails CHECK(S) FAILED"; fi
exit $fails
