---
name: imgui-input-race-crash-report
description: "The rare ImGui \"i >= 0 && i < Size\" assert was a WndProc-vs-render-thread race on InputEventsQueue, FIXED 2026-10-12 (backlog 98); debug builds write build/crash_*.txt"
metadata:
  node_type: memory
  type: project
  originSessionId: d902aca5-8413-4849-9ee0-5e0a8add44f4
  modified: 2026-10-11T23:09:48.099Z
---

2026-10-12: the recurring `Assertion failed: i >= 0 && i < Size, file imgui/imgui.h, line 2369` was
ImGui's input queue written by two threads: windproc (main thread, via ImGuiForwardWndProc) and the
render thread (Window::ImGuiNewFrame). Fixed with a recursive_mutex in core/WindowImGui.cpp around both;
closed as item 98 in docs/engine_backlog_done.md with the flood test (scratch flood.py posting WM_CHAR +
WM_MOUSEMOVE at MainWindowClass killed chasm 4/4 in 11-55 s before, survived 240 s after).

**Why:** if that assert, or a silent crash near ImGui input, ever comes back, the lock is the first suspect
(a new path feeding or draining the queue outside it), not a new bug class.

**How to apply:** debug builds write `build/crash_<date>_<time>.txt` on any IM_ASSERT or unhandled
exception (core/CrashReport.h, gdb self-attach) - read it first. 3rdparty/imgui_user_config.h routes
IM_ASSERT and is named in BOTH 3rdparty/makefile and engine.mk. Rebuilding libimgui.a needs the root
imgui_disable_alpha_compositing.patch applied to the submodule first (it had been lost once).
