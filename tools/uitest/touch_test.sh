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
#  - TEST LEVEL AND EDGE ACTIONS DIFFERENTLY. Movement (IsKeyDown) survives being applied on a
#    non-ticking pass; rotation (WasKeyPressed) does not. See the note above the rotation checks.
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

# EDGE-triggered actions are checked UNPAUSED, and that is a statement about the engine rather than
# about the buttons. Rotation fires on WasKeyPressed, which is true for exactly one tick. While the
# simulation is paused the physics loop keeps running NON-TICKING passes, each of which calls
# UpdateInput -> ApplyPendingEvents; the press is drained there, raises its edge on a pass that
# does not tick, and NextInput clears it before any tick can see it. Backlog item 84 fixed exactly
# this for SYNTHETIC holds by advancing them inside the ticking branch; real asynchronous events
# still fall through. Unpaused there are no non-ticking passes to lose it on, and it works.
echo
echo "--- EDGE-triggered actions, unpaused (see the note above) ---"
tool tetris_pause '{"paused":false}' >/dev/null; sleep 0.4
r0=$(field piece_rotation); click $CW_X $ROW_Y down; click $CW_X $ROW_Y up; sleep 0.4
r1=$(field piece_rotation)
check "CW button rotates the piece" "changed" "$( [ "$r0" != "$r1" ] && echo changed || echo "same($r0)" )"

r0=$(field piece_rotation); click $CCW_X $ROW_Y down; click $CCW_X $ROW_Y up; sleep 0.4
r1=$(field piece_rotation)
check "CCW button rotates the piece" "changed" "$( [ "$r0" != "$r1" ] && echo changed || echo "same($r0)" )"

# And assert the limitation itself, so that if someone fixes item 84's remaining half this test
# fails loudly and gets updated rather than quietly continuing to test around it.
echo
echo "--- the known limitation, asserted so a fix does not go unnoticed ---"
tool tetris_pause '{"paused":true}' >/dev/null; sleep 0.3
r0=$(field piece_rotation); click $CW_X $ROW_Y down; step 4; r1=$(field piece_rotation)
click $CW_X $ROW_Y up
check "edge actions are STILL lost under sim_step (item 84's other half)" "$r0" "$r1"

echo
if [ $fails -eq 0 ]; then echo "ALL CHECKS PASSED"; else echo "$fails CHECK(S) FAILED"; fi
exit $fails
