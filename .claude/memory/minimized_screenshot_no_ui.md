---
name: minimized-screenshot-no-ui
description: "an app started --minimized returns screenshots WITHOUT ImGui panels even with include_ui true; restore with ShowWindow(h,4) to check UI work without stealing focus; posted mouse messages cannot drive ImGui"
metadata:
  node_type: memory
  type: reference
  originSessionId: ab5494ad-10d2-4e73-8d9e-f67e1b05b3cd
  modified: 2026-10-25T11:16:01.298Z
---

`screenshot` with `include_ui: true` on an app started `--minimized` comes back with the 3D scene
only - no menu bar, no panels. ImGui has no display area while the window is minimised, so it
draws nothing. It looks exactly like a UI change that failed. Found 2026-09-26 checking the menu
bar in archer.

To check UI work: restore WITHOUT activating (`SW_SHOWNOACTIVATE` = 4), screenshot, then minimise
again with `SW_SHOWMINNOACTIVE` = 7 - neither takes the person's keyboard focus. PowerShell:
`Add-Type -Name W -Namespace U -MemberDefinition '[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);'`
then `[U.W]::ShowWindow((Get-Process archer).MainWindowHandle, 4)`.

Only ImGui is missing: core's UIOverlay (chasm's HUD, cards, pins) DOES draw in a minimised
screenshot - checked 2026-10-07. But `include_ui: false` drops the UIOverlay too, not just ImGui: a
chasm card/NEEDS ATTENTION check needs `include_ui: true` (seen 2026-10-16).

Hotkeys gated on HasFocus (F9/F10, the U hide-UI toggle) cannot be tested this way at all - the
person has to press them. Related: [[archer-app]].

ImGui cannot be DRIVEN reliably either (checked 2026-10-17 on chasm). Mouse messages posted to the
window (PostMessage WM_MOUSEMOVE, button, wheel) reached ImGui once - probably only because the
person's real cursor was over the window - and never again in two later instances, with or
without sleeps between the move and the action. The likely cause: ImGui's TrackMouseEvent gets an
immediate WM_MOUSELEAVE when the real cursor is elsewhere. So a header that is closed by default,
or content scrolled out of view, cannot be checked on screen: read what the panel shows through the
app's MCP tool instead, and say in the report which lines were not seen.

SCROLLING DOES WORK (chasm, 2026-10-25): post a WM_MOUSEMOVE then a WM_MOUSEWHEEL (screen coords in
lParam, -120 a notch) per notch, 40 ms apart, over the panel - ~14 notches reach chasm's Pick / Selected
header. Clicks still did not land, posted back to back or in two bursts. Script kept as the session's
scratchpad wheel.ps1; rewrite it from this description.

Two traps seen the same day: an `Add-Type` piped to Out-Null with -ErrorAction SilentlyContinue let the
ShowWindow(h,4) restore fail SILENTLY - screenshots then show no panels at all; check `IsIconic` after.
And an app started --minimized saves imgui.ini (cwd's, the person's layout file) with a 32x32 dock
space while minimised; restore it and wait ~5 s for ImGui to save the full layout before taskkill.
