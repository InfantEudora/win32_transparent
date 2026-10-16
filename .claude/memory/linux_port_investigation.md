---
name: linux-port-investigation
description: Linux port - step 1 DONE 2026-10-16 (core+chasm compile and bake on Linux; link stops at the 29 window-backend symbols); step 2 waits on Wayland/X11/both; docs/linux_port_plan.md
metadata:
  node_type: memory
  type: project
  originSessionId: 4e722717-a7a0-46eb-99ff-90a804914e0e
  modified: 2026-10-16T21:51:59.186Z
---

Plan and measurements: **`docs/linux_port_plan.md`** - read it first. Goal (Dick, 2026-10-16): one
binary for Arch AND Debian, like the one Windows exe, and **no toolkit** - SDL2 and GLFW were tried
before and disliked; the engine is hand-rolled on purpose.

**Step 1 done 2026-10-16**: `make` and `make ship` in apps/chasm compile and bake on Linux through the
repo's own makefiles; the link stops at exactly 29 symbols - 13 `Window` methods, `PollDevices`, 15
GL 1.x direct calls. Windows checked after: debug + ship build, chasm ran, flags/vsync/raw input fine.

**Step 2 needs Dick's decision**: Wayland native, X11, or both. A 92-line raw Wayland probe in WSLg
got a 4.5 core context via EGL and all 99 engine GL entry points (glClear too) from eglGetProcAddress
with no libGL - so glad can load GL 1.1 as well and the binary links no GL library.

**Method - reuse it.** WSL here: Ubuntu 22.04, g++ 11.2, glibc 2.35, WSLg (llvmpipe 4.5), **no
passwordless sudo**: dev headers via `apt-get download` + `dpkg -x` into `~/linux_port_sysroot/root`,
handed to make through `CPLUS_INCLUDE_PATH`/`C_INCLUDE_PATH` (env vars pass engine.mk's command-line
check). Run WSL scripts from PowerShell - Git Bash mangles `/mnt/c` paths. Grep build logs for
`fatal error:` too, not just `: error:`.

**Traps found:** a `.S` from the packer names the blob by ABSOLUTE path, so generated dirs must be per
platform (they are now). Linux defaults USE_MCP/NET/IMGUI to 0 until step 3. Android's keycodes are
AKEYCODE, so VK guards are `!__ANDROID__`, not removed. Windows replays/baselines won't match Linux.

**How to apply:** start from the doc; after a Linux-side change re-run the WSL build and check the
undefined-symbol list only shrinks. Related: [[android_port_merge]], [[per_app_build_layout]],
[[deterministic_sim_plan]].
