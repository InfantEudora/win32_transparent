# Linux port: chasm as one file on Debian and Arch

Plan for building the engine, and chasm first, on Linux, as a single binary the way the Windows
build is a single exe.

Written 2026-10-16. Every fact below was measured in WSL (Ubuntu 22.04, g++ 11.2, glibc 2.35,
WSLg), not recalled.

**Status: chasm runs on Linux.** Step 1 (core compiles) is done and committed. Step 2, the window
backend, is **X11** (Dick's choice, 2026-10-17: it runs on X11 desktops and, through XWayland, on
Wayland ones) and is built: `make` and `make ship` give a working chasm, measured in WSLg (§3.5).
Open: gamepads, file drop and raw mouse under step 2; step 3 (MCP, ImGui); step 4 (shipping).

---

## 1. The shape of the binary

**One binary serves Debian and Arch.** They differ in how software is packaged, not in what an
x86-64 ELF needs. The one constraint is glibc: it runs forward only, so the binary is built on
the OLDEST glibc it has to run on. WSL's Ubuntu 22.04 has 2.35, below Debian 12 (2.36), Debian
13 and Arch, so a binary built there runs on all three.

**It cannot be `-static` like the Windows exe, and that is forced.** The GL driver is the
system's (Mesa or NVIDIA) and is loaded at run time; a static glibc cannot load anything. So:

| | |
|---|---|
| glibc | dynamic - on every machine |
| libstdc++, libgcc | inside the exe, as on Windows (`-static-libstdc++ -static-libgcc`) |
| our libraries (thirdparty, imgui, rp3d) | static, as on Windows |
| sound server (PulseAudio, ALSA) | opened at run time by miniaudio - measured: `libpulse.so.0`, `libasound.so.2` by name |
| window system, EGL | opened at run time by the step-2 backend |

With baked assets that is still one file, needing glibc and nothing else at link time. A `.deb`
or a `PKGBUILD` would only wrap it.

---

## 2. What was in the way (measured before step 1)

- **26 of 75 core files and 26 of 58 chasm files compiled clean as they were.** Every failure
  stopped at `windows.h` or `winsock2.h`. With those includes switched off in a copy, the errors
  sat in six headers - `glad.h`, `Window.h`, `RawInput.h`, `Application.h`, `TCPServer.h`,
  `MCPServer.h` - and only 18 core and 4 chasm `.cpp` files had Win32 of their own. The same
  chokepoint pattern the Android port found.
- **`glad.h` typedef'd `GLsizeiptr` as `long long`.** Mesa's `glext.h` says `long`, which is
  64-bit on Linux, and the compiler calls that a conflict. `Window.h` included `GL/glu.h`, which
  nothing uses.
- **Silent stubs**: `#if _WIN32` blocks with no `#else`, which compile clean on Linux and do
  nothing - all of chasm's key bindings, core's default key maps, the save list, the save folder,
  command-line flags, the recordings list, and `GetExecutableDirectory`, which fell through to
  Android's empty "writable data dir" and so made every exe-relative path cwd-relative. No build
  would ever have flagged these.
- **Shaders**: 20 in the tree said `#version 460` while the engine asks for a 4.5 context.
  NVIDIA allows it; Mesa refuses. As `450` all of them compile under Mesa - which is the compiler
  every AMD and Intel player on Linux gets - and none uses a 4.6 feature.
- **Asset names match their files' case** (190 literals checked) and none uses a backslash, so
  the case-sensitive filesystem costs nothing.
- **WSLg gives a 4.5 core context**, through llvmpipe (software). Enough to start and screenshot.

---

## 3. Step 2 - the window backend (X11, built 2026-10-17)

### 3.1 What it has to provide

Everything the Linux link reports undefined after step 1 (§5), all of it behind interfaces that
already exist:

- **`Window`** - `CreateNewWindow`, `Init`/`InitOpenGL`, `Show`, `MakeContextCurrent`/
  `ReleaseContext`, `PumpUntilQuit`, `SwapWindowBuffers`, `Resize`, `SetFullscreen`,
  `IsMinimized`, `SetTitle`, `SetAlwaysOnTop`, `GetDisplaySize`, `RequestQuitAll`, file drop.
  `Window.h` says which members are Win32's; the rest is the seam.
- **`GLSetPlatform(loader, swap_interval)`** before `InitGLExtensions` - see `glad.h`.
- **Input acquisition**: key, mouse, wheel and focus events into `SubmitSystemKey`/
  `SubmitAxisDelta`, keys translated to the VK codes in `core/KeyCodes.h`; `PollDevices`,
  `PollGamepad`, `ListDevices`, `SendMotorData`. A pad reports in XInput's bit layout and goes
  through `ApplyGamepadButtons`, which step 1 made shared.

### 3.2 GL on Wayland is simpler than on Win32

Measured with a 92-line probe (raw `wl_surface` + `xdg_toplevel`, no toolkit): a 4.5 core debug
context from `eglCreateContext` directly - the attributes are core EGL 1.5, no dummy context - and
**all 99 of the engine's entry points from `eglGetProcAddress`, `glClear` included, with no libGL
linked.** WGL needs a dummy context to reach `wglCreateContextAttribsARB`, and
`wglGetProcAddress` returns NULL for GL 1.1, which is why `glad.h` mixes `<GL/gl.h>` with
pointers. On Linux glad can load everything, 1.1 too, and the binary links no GL library at all.
That part is not done yet: GL 1.x calls are still direct calls declared by `<GL/gl.h>`.

The probe linked `libwayland-client`, `libwayland-egl` and `libEGL` - the Linux counterparts of
user32, gdi32 and opengl32, on every Wayland desktop - and could `dlopen` them instead. "No
library at all" is not available for GL on any OS: the driver is a library.

### 3.3 Where Wayland is harder than Win32

- **No title bar on GNOME.** The frame is the client's job unless the compositor offers
  `zxdg_decoration_manager_v1`. GNOME (Debian's default desktop) does not, and neither does
  WSLg's Weston (probe); KDE, Sway and Hyprland do. Windowed mode on GNOME needs our own bar and
  edges, drawn with UIOverlay, calling `xdg_toplevel.move`/`resize`. Fullscreen needs none.
- **The compositor owns placement**: no positioning, no starting minimised, no "which window is
  in front". `Resize`'s centring and `--minimized` have no equivalent. For agent runs, run
  offscreen instead - `screenshot` reads the renderer's FBO anyway.
- **A hidden window's swap blocks.** At swap interval 1, `eglSwapBuffers` waits for a frame
  callback a hidden surface never gets. Interval 0 and pace the loop ourselves, which is what the
  render loop already does for a minimised window on Windows.
- **Protocols come as XML**: `wayland-scanner` makes C of them (xdg-shell is 182 lines of
  tables), generated once and committed. Raw mouse deltas are `zwp_relative_pointer_v1`.
- **Keys arrive as evdev codes** (positional); letters by layout, and text, want `libxkbcommon`.

**Gamepads are neither Wayland's nor X11's**: `/dev/input/event*` through the kernel's evdev
interface, no library. The Xbox, PlayStation and Switch Pro drivers all report the kernel's
standard gamepad layout. Needed whichever window system is chosen.

### 3.4 The choice

- **Wayland native** - the future, and the part above. Does not run on X11-only sessions
  (Xfce, Cinnamon, MATE, i3): there is no compositor to talk to.
- **X11 (Xlib + GLX)** - closest in shape to the Win32 code, frame drawn by the window manager,
  positioning and minimise work. Runs on X11 sessions AND on every Wayland desktop through
  XWayland. Blurry under fractional scaling; the legacy path.
- **Both** - one Window, two backends chosen at start.

Estimate, not measured: a native Wayland backend is 1.5-2x the size of today's Win32 window and
raw-input code, mostly because of the frame.

**Chosen: X11.** Wayland native stays possible later as a second backend behind the same Window;
§3.2-3.3 are what it would take.

### 3.5 What was built, and what it does

- **`core/Window_x11.cpp`** - the Window methods for X11: an Xlib window with a 24-bit visual (a
  32-bit one would let the desktop show through wherever alpha is not 1), a GLX 4.5 core debug
  context from `glXCreateContextAttribsARB` (no dummy context), glad's lookup through
  `glXGetProcAddressARB`, vsync through `GLX_EXT_swap_control` or `GLX_MESA_swap_control`, the
  close button (`WM_DELETE_WINDOW`, honouring `f_close_asks_app`), Escape and Alt+Enter as
  windproc has them, fullscreen and always-on-top by `_NET_WM_STATE`, `--minimized` as an iconic,
  unfocused start. `XInitThreads` first, because the main thread pumps the connection while the
  render thread swaps on it.
- **`core/InputController_linux.cpp`** - `HandleXEvent`, X11's `HandleMessage`: keys as VK codes
  (keysym at index 0, so letters follow the layout as VKs do; sided modifiers, as RawInput.cpp
  resolves them; autorepeat as repeated presses), the buttons, the wheel, motion deltas, crossing,
  focus. `PollDevices` gives the cursor each tick from the last reported position.
- **Linked, not loaded**: `libGL.so.1` and `libX11.so.6`, on every desktop as user32 and
  opengl32 are on Windows. The 15 GL 1.x calls resolve from libGL.
- `Window.cpp`'s shared part (window list, `RequestQuitAll`, the back-buffer blit) moved to its
  top, outside the Win32 guard, so both backends use one copy.

**Measured in WSLg** (Mesa llvmpipe, XWayland):

- Debug build: the title screen renders as on Windows - shadows, SSAO, UIOverlay text - and logs
  the same six errors the Windows build does (`bow`, `quiver`, `arrow_full` not found).
- Input, driven with synthetic X events: Down moved the menu highlight, hovering highlighted
  Settings, a click opened the Settings page.
- Sound: miniaudio opened PulseAudio (44.1 kHz stereo) and loaded the Ogg sounds. **WSLg's sound
  server is not up for the first seconds after WSL starts**: a run then gets "No backend" and
  carries on silent. Not the build - the same probe succeeded 14 times in a row a minute later.
- `SIGTERM` takes the Ctrl+C path: "Shutting down by Ctrl+C", both threads joined.
- `make ship`: **one file, 8.6 MB**, needing `libGL.so.1`, `libX11.so.6`, `libm` and `libc`, and
  glibc 2.34 or newer (Debian 12 and 13, Arch, Ubuntu 22.04+; not Debian 11). Copied alone into
  an empty folder it runs, every asset from its baked table, and writes `chasm.log` beside itself.
- WSLg ignores the iconic hint: `--minimized` windows open visible there. Real window managers
  are expected to honour it; untested.
- `Current Display Settings` reads 640 x 480 under WSLg's XWayland - the root window's size there.
  Only logged.

**Still open in step 2**: gamepads (evdev - `PollGamepad` is empty, see
`InputController_linux.cpp`), file drop (XDND), raw mouse deltas (XInput2; today's are the
accelerated cursor's, and stop at the screen edge), and per-monitor display size (XRandR).

---

## 4. Steps 3 and 4

**Step 3 - the developer surface**, which a shipped build does not have (`make ship` turns it all
off), but agents need: sockets (`Socket`, `TCPServer`, `TCPClient`, `HTTPServer`: Winsock to BSD
sockets, and ignore `SIGPIPE`, or a closed connection kills the game), `MCPServer`'s stdio reader
(`ReadFile` on the raw handle) and its base64 (`CryptBinaryToString` - wincrypt), `FileWatcher`
(`inotify`), and ImGui's platform half (`WindowImGui.cpp`). Until then Linux defaults
`USE_MCP`, `USE_NET` and `USE_IMGUI` to 0 (engine.mk, PLATFORM block).

**Step 4 - ship it**: `make ship` on Linux (the asset packer builds there already); where saves,
settings and the log go (beside the binary as on Windows, or `~/.local/share/chasm`, which an
installed package needs); packaging if wanted (tar, `.deb`, `PKGBUILD`, a `.desktop` file); and
the Debian and Arch VMs. A VM's GL usually stops below 4.5 - check `glxinfo -B`, and if so run
with `LIBGL_ALWAYS_SOFTWARE=1` for llvmpipe's 4.5. Speed needs real hardware.

---

## 5. Step 1 - core compiles on Linux (DONE 2026-10-16)

**What changed**, all of it compiled and run on Windows afterwards (§6):

- **Build switch**: `engine.mk`, `3rdparty/makefile`, `tools/tools.mk` detect the platform.
  Windows' flags, paths and exe names are unchanged; Linux has its own link line, no `.exe`, and
  its own object trees (`build/core/linux_*`, `build/obj/linux_*`, `3rdparty/build/linux`,
  `libs/linux`) so one checkout builds from both sides. miniaudio adds PulseAudio and ALSA,
  which costs Windows nothing (`miniaudio_config.h` says why). `FileWatcher.cpp` now goes with
  `USE_NET=0`, being HTTPServer's alone - so the Windows ship exe drops it too.
- **`glad.h`/`glad.cpp`**: WGL behind `_WIN32`; Mesa's `glext.h` kept out (`GL_GLEXT_LEGACY`);
  `GLsizeiptr` is `intptr_t`, the same type on Win64; every GL lookup goes through one
  `LoadGLProc`; `GLSetPlatform` and `GLSetSwapInterval` for the window backend.
- **`Window` is the platform seam**: the Win32 handles are behind `#if`, and Application,
  Renderer and the debug UI reach the window system only through methods (context hand-over,
  the message pump, display size, always-on-top). `Window.cpp` and `RawInput.cpp` are the Win32
  backend and compile to nothing elsewhere.
- **`core/KeyCodes.h`**: the VK codes are the desktop keycode space everywhere, so bindings and
  recordings mean the same key on both. One list, `static_assert`ed against `<winuser.h>` on
  Windows - all 87 values passed - and enumerators elsewhere. The VK tables' guards are
  `!__ANDROID__` now, not `_WIN32`: Android's keys are AKEYCODE numbers.
- **Small OS pieces**: `GetExecutableDirectory` (`/proc/self/exe`), the command line
  (`/proc/self/cmdline`, so `--minimized` and `--mcp-port` need no change to any `main.cpp`),
  `PrecisionSleeper` (`clock_nanosleep`), `BackgroundWork`'s priority, Ctrl+C (`SIGINT`/`SIGTERM`,
  same two steps), the log file's exclusivity (`flock`), and `Directory::ListFiles`/`Create`/
  `FullPath`, `FileWriteTime`, `CopyFileShared`, `OpenInFileBrowser` in core - replacing the same
  Win32 loops that chasm and Application each had.
- **`CrashReport`**: a Linux arm - fatal signals on an alternate stack, the same
  `crash_<stamp>.txt`; gdb attaches for every thread in debug (`PR_SET_PTRACER`, for Yama), the
  failing thread as module+offset in release. Tested standalone in WSL, both ways.
- **chasm**: `main.cpp` has a `main`; the save, record, title and sound files use the core
  helpers. **Shaders**: the 20 `#version 460` became `450`.

- **`Debug_win32.cpp`** is the desktop console backend now, Linux's too - only `SetupConsole`'s
  colour switch is Windows'. Kept under its name for the Android port's notes and scripts.
- **The baked-asset folder** is per platform as well (`build/linux_generated_*`): its `.S` names
  the blob by absolute path, and a Linux stub left in the Windows folder broke the next Windows
  ship build - found, and fixed, while checking this step.

**State of the Linux build, measured**: `make` and `make ship` in `apps/chasm`, through the repo's
own makefiles, compile every core and chasm source with no error. `3rdparty` builds `libs/linux/`
(miniaudio with PulseAudio and ALSA, no WASAPI), and the asset packer builds and bakes. The link
then stops at **29 symbols, every one of them step 2's**:

- 13 `Window` methods: `CreateNewWindow`, `Init`, `Show`, `MakeContextCurrent`, `ReleaseContext`,
  `PumpUntilQuit`, `SwapWindowBuffers`, `Resize`, `SetFullscreen`, `IsMinimized`, `SetTitle`,
  `GetDisplaySize`, `RequestQuitAll`;
- `InputController::PollDevices`;
- 15 GL 1.x functions still called directly (`glEnable`, `glDisable`, `glDepthMask`, `glViewport`,
  `glBlendFunc`, `glCullFace`, `glDepthFunc`, `glScissor`, `glDrawArrays`, `glDrawBuffer`,
  `glReadBuffer`, `glReadPixels`, `glPixelStorei`, `glGetIntegerv`, `glDeleteTextures`) - which
  glad loads like the rest once the backend hands it `eglGetProcAddress` (§3.2), or which
  `-lOpenGL` resolves if a GL library at link time is acceptable after all.

---

## 6. Checked on Windows after step 1

- chasm debug and `make ship` build clean, with no new warnings - the ship exe twice, the second
  time after the generated-folder fix in §5.
- The debug exe started with `--minimized --mcp-port 8769`: both flags took, raw input came up,
  it rendered at 1440x810, and vsync toggled both ways through the new `GLSetSwapInterval`
  (off 90 fps, on 75 fps on a 75 Hz screen).
- The ship exe started, loaded every shader and mesh from its baked table, and logged the same
  six errors as the debug exe and as before the change (chasm asking `GetAssetsFromGLTF` for
  `bow`, `quiver` and `arrow_full`, which it does not find).
- Not run: the other apps. Core changed under all of them - nothing in it should move their
  behaviour, but archer's replay test is the check for that (CLAUDE.md).

---

## 7. Building on Linux today

Once, on the build machine (WSL here):

```bash
sudo apt install build-essential mesa-common-dev libgl-dev libx11-dev
# mesa-utils too, for glxinfo -B
```

On Arch: `sudo pacman -S base-devel libglvnd libx11 mesa` (mesa for the GL headers).

Then, as on Windows but with `make`:

```bash
cd 3rdparty && make -j8            # -> libs/linux/libthirdparty.a, libimgui.a
cd apps/chasm && make -j8          # -> build/chasm_nophysics_nomcp_nonet_noimgui
cd apps/chasm && make ship -j8     # -> build/chasm_baked_nophysics_nomcp_nonet_noimgui_release
```

The ship file is what to hand to another machine: copy it anywhere, `chmod +x` it if the copy lost
the bit, and run it. It needs a desktop (X11 or Wayland), and nothing else installed.

Building through `/mnt/c` works but is slow; a clone on the Linux side is much faster and sees only
committed work. The separate trees mean the two never collide either way.

**Recordings and trace baselines made on Windows will not replay bit-exact on Linux**: glibc's
maths functions can differ from MinGW's in the last bit. That is within the agreed scope (replay is
same-machine only, `docs/replay_determinism_plan.md`), but Linux needs baselines of its own.
