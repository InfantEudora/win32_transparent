"""
Backlog item 88: an edge-triggered REAL input must survive a non-ticking pass.

Drives the on-screen CW button through PostMessage -> WndProc -> SubmitPointer ->
SubmitSystemKey, which is a genuinely ASYNCHRONOUS source relative to the physics thread. That
matters: tetris_input uses HoldKey, the SCRIPTED path, which item 84 already fixed - so a test
built on it would pass with or without this change and prove nothing.

Two things are checked, and the second is as important as the first:
  1. A CW press while PAUSED is delivered to the next stepped tick.      (the fix)
  2. MUTE, read every pass from UpdateView, still toggles exactly ONCE.  (the regression guard)

(2) is what a naive "just don't clear the edge while paused" would break: mute would re-fire on
every paused pass for as long as the edge sat there.
"""
import ctypes, json, os, subprocess, sys, time, urllib.request
from ctypes import wintypes

#127.0.0.1, never localhost: the server binds IPv4 only, and where localhost resolves to ::1 first
#every call pays a failed IPv6 connect - measured at 2,058 ms against 15 ms. See CLAUDE.md.
MCP = "http://127.0.0.1:8765/mcp"

#Derived from this script's own location rather than hardcoded, so the suite travels with the
#checkout: tools/uitest/ -> repo root -> apps/tetris.
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CWD = os.path.join(ROOT, "apps", "tetris")
#The app is run from its own folder because main.cpp counts ../../../shared_assets from there and
#imgui.ini and the save file are written beside the exe.
EXE = os.path.join(CWD, "build", "tetris.exe")

WM_ACTIVATE, WA_ACTIVE = 0x0006, 1
WM_LBUTTONDOWN, WM_LBUTTONUP = 0x0201, 0x0202
MK_LBUTTON = 0x0001

# Window is 1200x900; see ApplicationTetris::LayoutTouchButtons.
#   s=88 g=12 m=28  ->  row = 900-28-88 = 784
#   right cluster built leftwards from the right edge: ">" 1084, "CW" 984, "DROP" 884
CW_XY = (1028, 828)
MUTE_XY = (1140, 60)

user32 = ctypes.windll.user32
_rpc_id = [0]


def rpc(name, args=None):
    _rpc_id[0] += 1
    body = json.dumps({"jsonrpc": "2.0", "id": _rpc_id[0], "method": "tools/call",
                       "params": {"name": name, "arguments": args or {}}}).encode()
    req = urllib.request.Request(MCP, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        d = json.loads(r.read())
    return json.loads(d["result"]["content"][0]["text"])


def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def click(hwnd, xy):
    """A real click, focus re-asserted first.

    WM_ACTIVATE is posted rather than relying on SetForegroundWindow because Windows refuses that
    for a background process under a pile of conditions - it works most of the time and silently
    fails the rest, and a press arriving unfocused is dropped by InputController's focus gate
    (SubmitSystemKey drops key-DOWNS while !f_has_focus). See ui_overlay_plan.md section 14.
    """
    x, y = xy
    lp = (y << 16) | x
    user32.PostMessageW(hwnd, WM_ACTIVATE, WA_ACTIVE, 0)
    time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp)
    time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp)


def main():
    # A STALE APP ON THE PORT IS THE ONE FAILURE THAT LIES.
    # Every app binds 8765, and a second one starts perfectly happily while its server silently
    # fails to bind - so the test would drive the app it launched and READ the old one, reporting
    # whatever the previous build did. That cost a confused round here: a run against a leftover
    # instance of the deliberately-broken control build looked like the fix had regressed.
    try:
        rpc("status")
        print("FAIL: something is already answering on 8765 - kill it first, or this test will "
              "silently measure THAT app instead of the one it starts")
        return 1
    except Exception:
        pass

    proc = subprocess.Popen([EXE], cwd=CWD,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = []
    try:
        for _ in range(50):
            time.sleep(0.2)
            try:
                rpc("status")
                break
            except Exception:
                pass
        else:
            print("FAIL: app never answered on the MCP port")
            return 1

        hwnd = find_window(proc.pid)
        if not hwnd:
            print("FAIL: could not find the app window")
            return 1
        print("window handle %s, pid %d" % (hwnd, proc.pid))

        # Let it settle, then stop the simulation.
        time.sleep(1.0)
        rpc("tetris_pause", {"paused": True})
        time.sleep(0.3)

        # ---- 1. the fix: an edge raised while paused reaches the next tick -------------------
        before = rpc("tetris_state")
        if not before.get("paused"):
            print("FAIL: asked for a pause and did not get one")
            return 1
        rot0, tick0 = before["piece_rotation"], before["tick"]

        click(hwnd, CW_XY)
        # Deliberately generous: the point is that MANY non-ticking passes run, drain the event,
        # raise the edge and call NextInput before any tick happens. That is what used to eat it.
        time.sleep(0.4)

        mid = rpc("tetris_state")
        if mid["tick"] != tick0:
            print("FAIL: the simulation ticked while paused (tick %d -> %d)" % (tick0, mid["tick"]))
            return 1
        if mid["piece_rotation"] != rot0:
            print("FAIL: the piece rotated while PAUSED - it should wait for a tick")
            return 1

        rpc("tetris_step", {"num_ticks": 1})
        time.sleep(0.3)
        after = rpc("tetris_state")
        rot1 = after["piece_rotation"]

        if rot1 == rot0:
            failures.append("CW press while paused was LOST: rotation still %d after a step "
                            "(this is item 88 unfixed)" % rot0)
            print("FAIL rotate: %d -> %d (unchanged)" % (rot0, rot1))
        else:
            print("PASS rotate: paused press delivered to the stepped tick, rotation %d -> %d"
                  % (rot0, rot1))

        # ---- 2. the regression guard: chrome still fires exactly once ------------------------
        s0 = rpc("tetris_state")["sound"]
        click(hwnd, MUTE_XY)
        time.sleep(0.4)
        s1 = rpc("tetris_state")["sound"]
        if s1 == s0:
            failures.append("MUTE did not toggle at all (%s)" % s0)
            print("FAIL mute: no toggle, still %s" % s0)
        else:
            print("PASS mute: toggled %s -> %s while paused" % (s0, s1))
            # The important half: hold still for many more paused passes and confirm the edge is
            # NOT re-delivered. A kept-until-a-tick edge would flip this back and forth forever.
            time.sleep(1.0)
            s2 = rpc("tetris_state")["sound"]
            if s2 != s1:
                failures.append("MUTE re-fired on later paused passes (%s -> %s): the edge is "
                                "being re-delivered instead of consumed" % (s1, s2))
                print("FAIL mute: re-fired, now %s" % s2)
            else:
                print("PASS mute: still %s after 1s of paused passes - fired once, not repeatedly"
                      % s2)

        print("")
        if failures:
            for f in failures:
                print("FAILURE: " + f)
            return 1
        print("ALL CHECKS PASSED")
        return 0
    finally:
        try:
            proc.terminate()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
