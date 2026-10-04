---
name: background-app-stdout-pipe
description: A test script that starts an app with only stderr redirected and is itself piped (| grep) hangs forever - the app holds the pipe open
metadata:
  node_type: memory
  type: feedback
  originSessionId: 383a2647-bbc7-49ba-81f0-eb2ca2729af0
  modified: 2026-10-04T18:34:13.969Z
---

When a helper script starts an app in the background, redirect ALL of its streams:
`(./build/x.exe --minimized --mcp-port N >/dev/null 2>stderr.log </dev/null &)`.

**Why:** with only `2>log`, the app inherits the script's stdout. If the script's output is piped
(`bash check.sh | grep ...`), the pipe never closes while the app runs, so the command hangs until
the tool timeout. On 2026-10-04 (chasm work) this happened four times, looked like an MCP server
hang, and was misreported to a peer as one. "Window Restored" in the app log was a red herring.
Git Bash `timeout` also does not reliably kill a native Windows python.exe, so it is no guard.

**How to apply:** any time a script both launches a long-running exe and is run through a pipe.
Related: [[shell-heredoc-limit]] for the other shell gotchas.
