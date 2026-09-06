---
name: build-toolchain-location
description: "Where the MinGW/MSYS2 g++ and make binaries live on this machine, needed to build win32_transparent since they aren't on the Bash tool's PATH"
metadata: 
  node_type: memory
  type: reference
  originSessionId: ae7bc229-6e6e-4804-a4a0-da96936d5d53
  modified: 2026-09-06T00:00:00.000Z
---

The project's build tools (g++, make) are not on PATH in the Bash tool's shell — `which g++`/`which make` fail, and even exporting PATH within a command doesn't seem to take effect (silent no-op, possibly sandboxed). Always invoke by full path instead.

- Compiler: `/c/msys64/mingw64/bin/g++.exe` (MSYS2 MinGW64, g++ 13.1.0)
- Build: use `/c/msys64/mingw64/bin/mingw32-make.exe`, NOT `/c/msys64/usr/bin/make.exe` — the latter is the MSYS2 (Cygwin-style) make and silently fails with exit 127 and zero output in this shell (likely a runtime-DLL/exec issue), while `mingw32-make.exe` runs fine.
- `make` itself still shells out to `g++`, so the subprocess also needs the toolchain on PATH: prefix the command with `PATH="/c/msys64/mingw64/bin:$PATH"` in the same invocation, e.g.:
  `PATH="/c/msys64/mingw64/bin:$PATH" /c/msys64/mingw64/bin/mingw32-make.exe APP=Tank -j4`

Also `nproc` is not available in this shell (empty output, no error) — don't rely on `-j$(nproc)`; pass an explicit job count instead.

**Header dependency tracking: FIXED — the Makefile now does track it** (`DEPFLAGS = -MMD -MP`, `DEPS = $(OBJS:.o=.d) ...`, each `%.o` depends on its `%.d`, `-include $(DEPS)`, and `clean` removes them; see the comment block around Makefile:121). An incremental build after editing a shared header now correctly rebuilds every `.cpp` that includes it. Do NOT re-add the "always `clean` + full rebuild after touching a `.h`" workaround.

Historical context for why that mechanism exists, worth keeping because the failure mode is so misleading: before it, `.o` files were rebuilt only when their own `.cpp` changed, so editing a shared header left every untouched includer silently stale. After adding `core/Vehicle.h` and refactoring `TankCharacter` onto it, an incremental `mingw32-make.exe APP=Tank -j4` linked cleanly but `wind.exe` segfaulted deterministically a couple of seconds into startup (during the first-frame render) on every plain run — while the identical scenario under `gdb -batch -ex run -ex bt` didn't reproduce it (timing masked it), which is a strong tell that it isn't a real logic bug. `git stash` + rebuild showed the last-committed state ran fine, and a clean rebuild of the *same* working-tree changes fixed it outright. If an ABI-mismatch-shaped crash like that ever reappears, suspect stale objects (a `.d` that never got generated, e.g. an object built before this mechanism landed) before hunting a logic regression.

See [[mcp_native_tools_setup]] — this was discovered while rebuilding `wind.exe` (via `make APP=Tank`) so the tank app's MCP server would have a binary to connect to.
