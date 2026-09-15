# Working in this repo

Short notes for anyone driving this project from a terminal. The engine itself is described in
`readme.md`; `docs/` holds the deeper write-ups; `docs/engine_backlog.md` is the current work list
(open items only — closed ones, with their verification notes, are in
`docs/engine_backlog_done.md`).

---

## Shell gotchas

These have each cost more than one session. They are quirks of the tooling, not of the project.

### Heredocs: quote the delimiter, and keep them small

**Always `<<'EOF'`, never `<<EOF`.** With an unquoted delimiter the shell expands `$`, backticks
and `\` inside the body, which quietly mangles Python, GLSL, format strings and anything else
containing a `$`. This is the "heredoc quoting choked on my script" failure.

**But a quoted delimiter is not fully literal here, and this is the part that keeps biting.** It
does protect `$` and backticks. It does *not* leave backslashes alone: `\\` still collapses to `\`
on the way through. Measured with `cat > f <<'EOF'`:

| written in the heredoc | lands in the file |
|---|---|
| `\n` | `\n` |
| `\\n` | `\n` |
| `$HOME`, `` `date` `` | unchanged |

So any **escaped string** passed through a heredoc is silently altered one level. Writing a C string
`"...\n"` from a Python script in a heredoc produces a *real newline* inside the C literal and a
`missing terminating " character` compile error; `'\0'` arrives as `' '`. That cost two rounds on
2026-09-11 alone.

**Rule: if the text you are writing contains a backslash, do not put it in a heredoc.** Use the
file-writing tool for the script, then run the script. Heredocs are for throwaway commands with no
escapes in them.

**Keep the whole command under roughly 30 KB.** The command — heredoc body included — is passed to
`CreateProcess` as a single string, and Windows caps that at 32,767 characters. Exceed it and the
call fails with:

```
ENAMETOOLONG: name too long, uv_spawn
```

which says nothing about length and looks like a path problem. Writing
`docs/tetris_agent_brief.md` (35,280 bytes) failed exactly this way.

So: a short script or a small file by heredoc is fine and convenient — that works, and is used all
over this repo's tooling. Anything approaching tens of kilobytes must be written with a
file-writing tool instead, or split into pieces. Reach for the file-writing tool for documents and
source files; keep heredocs for throwaway scripts.

### Other shell notes

- **The working directory can reset between tool calls.** `cd "$(mktemp -d ...)" && ...` in
  particular has been seen to leave the next call somewhere else. Prefer absolute paths when a
  later call depends on where you are.
- **`&&` after a native `.exe` is unreliable** — check `$?` explicitly rather than chaining, if the
  exit status matters.
- Bash and PowerShell are both available and take their own syntax. This file's examples are Bash.
- **Two `python`s.** With `/c/msys64/mingw64/bin` on `PATH` for a build, `python` is MSYS's
  interpreter, which has no `PIL` and no third-party packages. The repo's tools want the Windows
  Python (3.9, has Pillow). Run the build and the tools in separate calls, or the tool dies with
  `No module named 'PIL'` right after a build that worked.

---

## Build and run

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
cd apps/tetris && mingw32-make.exe -j8        # mingw32-make, not /usr/bin/make
./build/tetris.exe 2>stderr.log &
```

- **`CONFIG=release` is the other build**, and it is worth knowing about because it is worth 90% of
  the file: `tetris.exe` is 41.9 MB, `tetris_release.exe` is 3.3 MB. Debug is the default and
  nothing about it changed.

  ```bash
  mingw32-make.exe CONFIG=release -j8        # -> build/tetris_release.exe
  ```

  The two have **separate object trees** (`build/obj/<config>/` and `build/core/<config>/`) and
  **separate exe names in the same folder**. That is deliberate on both counts: separate trees mean
  switching configuration costs nothing, and separate names mean neither exe can silently be the
  other one. The folder cannot change, because `main.cpp` counts `../../../shared_assets` from it
  and `imgui.ini` and save files are written beside it.

- **`make ship` is the build you hand to someone else.** `CONFIG=release` on its own is *not* that:
  it still carries the MCP server, the sockets, the ImGui panels, and no assets. Five settings have
  to agree, so they are named once as a target rather than typed out each time:

  ```bash
  mingw32-make.exe ship -j8        # -> build/tetris_baked_nomcp_nonet_noimgui_release.exe
  ```

  which is `CONFIG=release BAKE_ASSETS=1 USE_MCP=0 USE_NET=0 USE_IMGUI=0`. The result binds **no
  port** and needs **no `shared_assets/` beside it**. The flag most worth not forgetting is
  `USE_MCP=0`: a shipped game still listening on 8765 looks perfectly fine while running, which is
  exactly why it should not be a thing anyone has to remember. `USE_SOUND` is deliberately *not* in
  the set - it is a property of the app, not of the build. An app that has not declared
  `ASSET_ROOTS` cannot ship yet and fails with a message saying so. Measured 2026-09-14, Tetris:
  debug 43.9 MB, release 3.47 MB, ship 3.40 MB - the ship exe is *smaller* than the loose release
  despite carrying 932 KB of compressed assets, because dropping ImGui and the server saves more
  than the blob costs.

- **One exe per app.** Each app is a folder under `apps/` with its own `makefile`, `main.cpp`,
  `assets/` and `build/<name>.exe`. There is no root makefile and no `APP=` any more; the fifteen
  apps are `animation bomber breakout dozer grid isoanimation ocpp pinball ship sim tank testfx
  tetris tileset ui`.
- **`build/core` is shared between apps**, so **build one app at a time** - two concurrent builds
  race on the same object files. When a core source changes, the next build of every app relinks;
  that is a link, not a recompile, and is expected.
- The linker cannot overwrite a running exe (`Permission denied`) - stop the app first
  (`taskkill //F //IM tetris.exe`). The same error with nothing running means a stale lock;
  deleting the exe clears it.
- `stderr` carries all logging. `stdout` is reserved for the MCP stdio transport and must stay
  clean.
- Header dependencies are tracked, so editing a header rebuilds its dependents. `make clean` in an
  app folder removes only that app; `make cleancore` clears the shared objects.

### Assets

An asset is named `<category>/<file>` - `meshes/tank.glb`, `shaders/default.vert`,
`sound/bleep.wav` - never by where it sits. Each app's `main.cpp` declares the roots those names
resolve against, its own first and then `shared_assets`, so an app can override a shared shader
just by having one of its own. `shared_assets/` holds only what `core/` loads or what two or more
apps use. See `core/File.h` and `docs/asset_layout_plan.md`.

Anything that opens a file itself rather than going through `LoadFile` has to resolve the name
first - `ResolveAssetPath` / `ResolveAssetDirectory`. Three places already do
(`Directory::GetFiles`, `HTTPServer`'s hot-reload watcher, `Texture::LoadHDRFromFile`); a fourth
would be a bug that looks like a missing file.

## Driving a running app

Every app embeds an MCP server on **`http://127.0.0.1:8765/mcp`**.

**Only one app at a time.** They all bind the same port, and a second app starts perfectly well
while its server silently fails to bind - so `screenshot` then returns the FIRST app's window and
nothing looks wrong. A screenshot showing the wrong game is this, every time. `netstat -ano | grep
8765` names the process actually holding it.

**Use `127.0.0.1`, never `localhost`.** The server binds IPv4 only, deliberately; where `localhost`
resolves to `::1` first, every call pays a failed IPv6 connect — measured at 2,058 ms against
15 ms. Everything still works, just ~137x slower, with nothing appearing to be wrong. See
`docs/mcp_server.md`.

Generic tools in every app: `status`, `object_list`, `object_get`, `object_set_transform`,
`object_move`, `sim_pause`, `sim_step`, `sim_command`, `asset_list`, `object_spawn`, `camera_get`,
`camera_set`, `screenshot`. `screenshot` returns a PNG and is the fastest way to check visual work.
It includes the ImGui debug panels by default — telemetry, the inspector, buttons and sliders exist
only there — so pass `include_ui: false` when you want the clean 3D scene instead.

**Pause before you measure.** A tool handler holds no lock, so reading a free-running simulation
races the physics thread. `sim_pause` freezes the simulation while leaving the render loop running
(the window stays responsive and `screenshot` still works), and `sim_step` then advances by an exact
number of whole ticks — input, animation, gameplay and physics — instead of sleeping and guessing.
That includes **edge-triggered scripted input** (a fire, a serve, a rotate), which was silently
undeliverable while stepping until 2026-09-14; if one seems to do nothing under `sim_step`, suspect
a regression of that rather than the action.
Durations here are ticks, so this is the unit everything else is already written in.

---

## Working alongside other agents

Several agents share this one checkout, and they clash on files. `tools/lockd/` is a standalone
MCP broker that arbitrates it: claim the paths you are about to edit, edit them, release them.
See `docs/lock_broker.md`.

```bash
cd tools/lockd && ./build/lockd.exe 2>lockd.log &            # http://127.0.0.1:8766/mcp
```

`.mcp.json` at the repo root registers it for you - no `claude mcp add` needed, every agent that
opens this checkout gets the same broker, which is the point. You are asked to approve it once,
the first time. If the `lock_*` tools are not in your tool list, the broker was not running when
the session started, or the session predates the registration; restart the session.

- `lock_claim` **everything the task needs in one call** - claims are all-or-nothing, and
  accumulating them one at a time is how two agents deadlock on each other.
- Refused? Do other work and come back. Do not edit anyway. The reply names who holds it and why.
- **Re-read the files after a claim is granted.** The lease reserves the right to edit; it does
  not make a read taken before the lease current.
- `lock_release` when done.

**This is enforced, not just advised.** A `PreToolUse` hook (`.claude/settings.json`) claims the
target of every `Edit`/`Write` for your session and refuses the write if another agent holds it,
so you are protected and protecting others whether or not you call the tools yourself. Claiming
explicitly is still worth doing: it takes the whole set at once, states a reason the other agent
can read, and tells you about a collision *before* you have written the change. The hook lets the
edit through if the broker is not running.

It also covers the two shared resources this file already has rules for: `#build` for the shared
`build/core` tree (build one app at a time) and `#port:8765` for the port a running app binds
(one app at a time). Claiming those is how to make either rule hold rather than hope.

Port **8766**, and `127.0.0.1` rather than `localhost`, for the same reasons as everything else
here - see the two notes above.

---

## House style

Match the surrounding code: 4-space indent, `f_` prefix on booleans, and comments that explain
*why* rather than *what*. This codebase comments unusually heavily and unusually well; a change
that explains its own reasoning fits, one that just states the mechanism does not.

Durations in simulation code are counted in **ticks**, never in milliseconds — see
`Scene::GetPhysicsTick()` and the threading notes in `docs/tetris_agent_brief.md` §2.
