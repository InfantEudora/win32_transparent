# MCP Server (stdio + HTTP, per-app tools)

## What it is

`core/MCPServer.h/.cpp` is a process-wide MCP (Model Context Protocol) server embedded in every app in this project. Any translation unit can call `MCPServer::Get()->RegisterTool(...)` - from a static/global constructor, or from an app's own `Init()` - to expose one of its operations for querying/testing by an MCP client (Claude Code, Claude Desktop, ...).

Two transports, either or both of which can be running at once, share the same tool registry and JSON-RPC dispatch:

- **stdio** (`Start()`): a client spawns the process and owns its stdin/stdout for the life of the connection.
  - stdout carries ONLY newline-delimited JSON-RPC messages. Debug/log output goes to stderr instead (see `core/Debug_win32.cpp`) so it never corrupts the protocol stream.
  - stdin is read on its own thread (`MCPServer::ReaderLoop`), since it blocks and every app here already runs its own render/physics threads independently of it.
  - Closing the pipe (or the client killing the process) ends the app along with the MCP session - fine for short-lived tools, useless for a long-running graphical app a user is also looking at (see "Why HTTP too" below).
- **streamable HTTP** (`StartHttp(port)`): a small stateless JSON-RPC-over-HTTP server built directly on `TCPServer` (not `HTTPServer` - this endpoint has no need for its HTML/websocket/OCPP baggage). One POST body in, one JSON-RPC response (or a bare `202 Accepted` for notifications) out, connection closed - no sessions, no SSE, no keep-alive. Every tool call here is synchronous request/response with nothing server-initiated to deliver, so a persistent stream buys nothing.

The built-in `status` tool is always registered (confirms the process is alive); apps add their own on top.

Tank app tools (`ApplicationTank::RegisterMCPTools()`, called from `ApplicationTank::Init()`): `tank_drive`, `tank_steer`, `tank_telemetry`.

`core/Application.cpp`'s `FrameThreadFunction` calls both `MCPServer::Get()->Start()` and `MCPServer::Get()->StartHttp(8765)` unconditionally, so every app in the project gets both transports for free. If port 8765 is already bound (e.g. another instance of the same app is already running), `StartHttp` just logs an error and leaves HTTP transport off - it does not fail the process, and stdio (if a client is using it) is unaffected.

## Why HTTP too (2026-08-30)

stdio ties the app's process lifecycle to whichever MCP client happens to be attached: reconnecting or restarting the client's MCP session means killing/respawning `wind.exe`, which for a graphical app the user is actively looking at is actively harmful - the visible window disappears and reappears with every client-side reconnect, and there's no way to hand a client an already-running instance.

HTTP decouples them completely: `wind.exe` binds port 8765 once at startup and keeps running and listening regardless of who's connected. An MCP client attaches/detaches over the network whenever it wants; none of that ever touches the app process. This is the transport to use for testing any long-running graphical app in this project - register it with `--transport http` (below), not stdio.

## Registering it as a Claude Code MCP server

```
claude mcp add --scope local --transport http tank-app http://localhost:8765/mcp
```

The app must already be running (launch it once, however you like - a build + run, or a stray instance from a previous session) before this will connect; unlike the stdio form, `claude mcp add` here does not launch anything itself.

`claude mcp list` health-checks by spawning the binary and doing a lightweight probe - it can report "✔ Connected" even when the full tool list isn't actually available yet to a real session (see "Startup race" below), so don't treat "Connected" alone as proof the tools are usable. The HTTP transport doesn't have this particular failure mode (there's no separate process to spawn for the probe - it just hits the already-running app), but the general point stands: verify with an actual `tools/list`, not just the health check.

A reconnect (`/mcp` in a Claude Code session) is also not a respawn: it re-probes whatever's configured. For stdio that means spawning a fresh process and expecting to own it for the session - if the previous process is gone, the reconnect can appear to succeed ("Reconnected to tank-app") on a probe that spawns the binary, gets a response, and lets it exit again, without leaving anything actually listening. For HTTP it just re-opens a connection to the already-running app and leaves the process alone either way - another reason to prefer it here.

If you do still need stdio for something (e.g. a short-lived CLI tool rather than a graphical app), the old form still works:

```
claude mcp add --scope local tank-app -- "<repo>/wind.exe"
```

## Startup race (fixed 2026-08-30)

**Symptom:** `claude mcp list` said "✔ Connected", but the tank tools never showed up via `tools/list` to an actual client (`ToolSearch` never found `tank_drive`/etc. in a Claude Code session) - only the built-in `status` tool ever came back.

**Root cause:** `MCPServer::Get()->Start()` (spawns the stdin-reader thread) was called from `Application`'s base constructor - before `ApplicationTank::Init()` (which calls `RegisterMCPTools()`) ever ran. `Init()` isn't part of construction at all: it's invoked later from `Application::FrameThreadFunction`, a separate render thread spawned during `Application::Start()`, and can take several seconds (shader compilation, GLTF/texture loading) before returning. A real MCP client sends `tools/list` within milliseconds of spawning the process, so it always raced ahead of tool registration.

**Fix:** the `MCPServer::Get()->Start()` call now lives in `core/Application.cpp`'s `FrameThreadFunction`, immediately after the blocking `app->Init()` call returns (right before the physics thread is spawned). This is a single generic call site - not per-app - and it's correct for any app: `Init()` is a synchronous/blocking virtual call, so by the time it returns, whatever tools that app's `Init()` registered are guaranteed to be in place before the MCP server starts reading stdin at all. `StartHttp()` was added at the same call site for the same reason - both transports only ever start after tool registration is guaranteed complete.

**How it was verified:** piped `initialize` + `tools/list` at a freshly-spawned `wind.exe` with zero artificial delay (worst-case race) via a Bash `coproc`, and confirmed all 4 tools (not just `status`) came back in the response - both immediately after spawn and after an artificial delay.

## Manual stdio testing (without a full Claude Code session)

```bash
tasklist //FI "IMAGENAME eq wind.exe"        # confirm nothing stray is running first
coproc WIND { ./wind.exe 2>/tmp/wind_stderr.log; }   # redirect stderr so only JSON-RPC lines hit the read fd
printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n{"jsonrpc":"2.0","method":"notifications/initialized"}\n{"jsonrpc":"2.0","id":2,"method":"tools/list"}\n' >&"${WIND[1]}"
timeout 8 head -n 2 <&"${WIND[0]}"
taskkill //F //IM wind.exe                    # closing stdin does NOT kill the GUI/render process, only the reader thread
```

## Manual HTTP testing (without a full Claude Code session)

The app just needs to already be running (see build/run below) - no coproc juggling, since the listener is independent of whoever's talking to it:

```bash
curl -s -X POST http://localhost:8765/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
curl -s -X POST http://localhost:8765/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}' -w "\nHTTP_STATUS:%{http_code}\n"   # expect 202, empty body
curl -s -X POST http://localhost:8765/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"tank_telemetry","arguments":{}}}'
```

`wind.exe` keeps running (and the window stays open) after every one of these - there's no process to accidentally kill by testing.

## Build/run

Build with `mingw32-make.exe APP=Tank` (the Makefile's default `APP` is `Ship`; g++/make live at `/c/msys64/mingw64`, not on the default shell `PATH`). Then just run `./wind.exe` - both transports start on their own once `ApplicationTank::Init()` returns.
