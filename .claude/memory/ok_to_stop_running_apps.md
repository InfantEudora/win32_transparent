---
name: ok-to-stop-running-apps
description: The user is fine with Claude killing running app instances (e.g. archer.exe) when a relink or the MCP port needs them gone
metadata:
  node_type: memory
  type: feedback
  originSessionId: 9d71f293-16f6-4781-a640-5c4b9931508c
  modified: 2026-09-29T17:39:19.902Z
---

Stopping a running app instance (taskkill //F //IM <app>.exe) to relink or free port 8765 is fine without asking - said 2026-09-23 while iterating on archer.

**Why:** the linker cannot overwrite a running exe, and waiting for the user to close it stalls every build/verify loop.

**How to apply:** kill the instance, rebuild, mention in the reply that you did. Still ask before stopping OTHER tools that are not a game app (lockd, another agent's build). See [[running-app-is-user-driven]].

Refinement (2026-09-29): when driving your OWN test instance on another port (e.g. --mcp-port 8768) while the user plays theirs on 8765, stop yours by PID (`netstat -ano | grep 8768` names it, then `taskkill //F //PID <pid>`), not by image name - `//IM archer.exe` would also close the game they are playing. Killing by name is for when the exe must go to relink.
