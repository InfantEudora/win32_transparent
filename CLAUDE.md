# Working in this repo

Short notes for anyone driving this project from a terminal. The engine itself is described in
`readme.md`; `docs/` holds the deeper write-ups; `docs/engine_backlog.md` is the current work list
and `docs/tetris_agent_brief.md` is the best single tour of the core API.

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

---

## Build and run

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
mingw32-make.exe APP=Tetris -j8               # mingw32-make, not /usr/bin/make
./wind.exe 2>wind_stderr.log &
```

- One executable. `APP=` selects which `Application` subclass is built; the apps are the
  `apps/*.mk` fragments (Animation, Dozer, Grid, IsoAnimation, OCPP, Ship, Sim, Tank, Tetris,
  Tileset, UI).
- **The linker cannot overwrite a running `wind.exe`.** A build while it is running fails with
  `cannot open output file wind.exe: Permission denied`. Stop it first (`taskkill //F //IM
  wind.exe`). The same error with nothing running means a stale lock — deleting `wind.exe` clears
  it.
- `stderr` carries all logging. `stdout` is reserved for the MCP stdio transport and must stay
  clean.
- Header dependencies are tracked, so editing a header rebuilds its dependents. Unexplained heap
  corruption (`c0000374`) after switching `APP` means stale objects: `mingw32-make.exe clean`.

## Driving a running app

Every app embeds an MCP server on **`http://127.0.0.1:8765/mcp`**.

**Use `127.0.0.1`, never `localhost`.** The server binds IPv4 only, deliberately; where `localhost`
resolves to `::1` first, every call pays a failed IPv6 connect — measured at 2,058 ms against
15 ms. Everything still works, just ~137x slower, with nothing appearing to be wrong. See
`docs/mcp_server.md`.

Generic tools in every app: `status`, `object_list`, `object_get`, `object_set_transform`,
`object_move`, `sim_pause`, `sim_step`, `sim_command`, `asset_list`, `object_spawn`, `camera_get`,
`camera_set`, `screenshot`. `screenshot` returns a PNG and is the fastest way to check visual work —
though note it captures the 3D scene only, **not** ImGui, which is the engine's only text rendering
(backlog item 19).

**Pause before you measure.** A tool handler holds no lock, so reading a free-running simulation
races the physics thread. `sim_pause` freezes the simulation while leaving the render loop running
(the window stays responsive and `screenshot` still works), and `sim_step` then advances by an exact
number of whole ticks — input, animation, gameplay and physics — instead of sleeping and guessing.
Durations here are ticks, so this is the unit everything else is already written in.

---

## House style

Match the surrounding code: 4-space indent, `f_` prefix on booleans, and comments that explain
*why* rather than *what*. This codebase comments unusually heavily and unusually well; a change
that explains its own reasoning fits, one that just states the mechanism does not.

Durations in simulation code are counted in **ticks**, never in milliseconds — see
`Scene::GetPhysicsTick()` and the threading notes in `docs/tetris_agent_brief.md` §2.
