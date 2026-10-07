---
name: minimized-screenshot-no-ui
description: "an app started --minimized returns screenshots WITHOUT ImGui panels even with include_ui true; restore with ShowWindow(h,4) to check UI work without stealing focus"
metadata:
  node_type: memory
  type: reference
  originSessionId: ab5494ad-10d2-4e73-8d9e-f67e1b05b3cd
  modified: 2026-10-07T17:21:36.011Z
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
screenshot - checked 2026-10-07.

Hotkeys gated on HasFocus (F9/F10, the U hide-UI toggle) cannot be tested this way at all - the
person has to press them. Related: [[archer-app]].
