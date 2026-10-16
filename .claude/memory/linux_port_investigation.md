---
name: linux-port-investigation
description: "Linux port - chasm RUNS on Linux (X11 backend, 2026-10-17): debug + one-file ship binary checked in WSLg; gamepads/XDND/raw mouse and MCP/ImGui still open; docs/linux_port_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 4e722717-a7a0-46eb-99ff-90a804914e0e
  modified: 2026-10-16T22:38:32.924Z
---

Plan and measurements: **`docs/linux_port_plan.md`** - read it first. Goal (Dick, 2026-10-16): one
binary for Arch AND Debian, like the one Windows exe, and **no toolkit** - SDL2 and GLFW were tried
before and disliked; the engine is hand-rolled on purpose.

**Done:** step 1 (core compiles; Dick committed it as a7cafcc). Step 2 = **X11, Dick's choice
2026-10-17** (runs on Wayland desktops through XWayland): `core/Window_x11.cpp` + `core/
InputController_linux.cpp`. In WSLg chasm renders, takes keys/mouse, plays sound; `make ship` gives an
8.6 MB file needing only libGL.so.1, libX11.so.6, glibc >= 2.34, runs from an empty folder. Dick is
taking it to real machines next - their results are the open question.

**Open:** gamepads (evdev; PollGamepad empty), file drop (XDND), raw mouse (XInput2), MCP/sockets and
ImGui on Linux (step 3, so no MCP screenshot there yet - use scratchpad xcap.c/xsend.c style X tools).

**Method - reuse it.** WSL: Ubuntu 22.04, g++ 11.2, WSLg (llvmpipe 4.5), **no passwordless sudo**:
headers via `apt-get download` + `dpkg -x` into `~/linux_port_sysroot/root`, given to make through
`CPLUS_INCLUDE_PATH`/`C_INCLUDE_PATH`; link symlinks in `~/linux_port_libs` via `LIBRARY_PATH`. Run
WSL scripts from PowerShell (Git Bash mangles `/mnt/c`); PowerShell Set-Content writes CRLF - strip it.
Grep logs for `fatal error:` too.

**Traps:** WSLg's PulseAudio is not up for the first seconds after WSL starts ("No backend") - not a
build bug. The packer's `.S` uses an ABSOLUTE path, so generated dirs are per platform. A Linux run
from apps/chasm/build rotates the same chasm.log as Windows - **check timestamps before restoring
logs** (one of Dick's logs was lost on 2026-10-17 by restoring blind).

Related: [[android_port_merge]], [[per_app_build_layout]], [[deterministic_sim_plan]].
