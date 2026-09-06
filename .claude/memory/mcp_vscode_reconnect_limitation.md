---
name: mcp-vscode-reconnect-limitation
description: "In the Claude Code VSCode extension there is no /mcp slash command - typing it just opens an 'MCP servers' panel, and that panel doesn't list locally-added servers (claude mcp add --scope local)"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 2b9d6267-6a36-4434-a188-306a85c531bd
  modified: 2026-08-30T17:59:21.832Z
---

Typing `/mcp` in this VSCode extension environment does not run a reconnect command - it opens a "Customize > MCP servers" panel instead. That panel only shows pre-authenticated claude.ai connectors (Slack, Gmail, Google Drive, ...); a server just added via `claude mcp add --scope local --transport http tank-app http://localhost:8765/mcp` did not appear in it at all, even though `claude mcp add` reported success and the endpoint was confirmed live via curl.

**Why:** Observed directly - user opened the panel via `/mcp` and screenshotted it (2026-08-30); tank-app was absent while the three claude.ai connectors showed their real states (Connected/Needs Auth).

**How to apply:** Don't tell the user to "run /mcp to reconnect" as if it's a slash command in this environment - describe what actually happens (it opens the MCP servers panel) and set expectations that locally-added project-scope servers may need a session/extension restart rather than a panel reconnect, since the panel doesn't surface them. See [[mcp_native_tools_setup]] for the tank-app server itself.
