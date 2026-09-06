---
name: mcp-native-tools-setup
description: "Tank app's MCP server (stdio) setup, startup-race fix, and manual testing steps - now documented in-repo"
metadata: 
  node_type: memory
  type: reference
  originSessionId: e30244d1-aa77-4cb9-9d83-929a50a9e82d
  modified: 2026-08-30T16:10:04.486Z
---

Full details (what the MCP server is, how `tank-app` was registered with `claude mcp add`, the `Init()`-vs-constructor startup race that hid the tank tools and its fix in `core/Application.cpp`'s `FrameThreadFunction`, and manual stdio testing steps) now live in `docs/mcp_server.md` in the `win32_transparent` repo itself, so they're git-tracked and visible to anyone reading the repo - check there first.

See also [[build_toolchain_location]] for the `mingw32-make.exe APP=Tank` build requirement.
