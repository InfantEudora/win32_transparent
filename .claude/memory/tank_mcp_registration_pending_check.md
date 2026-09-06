---
name: tank-mcp-registration-pending-check
description: "tank-app MCP server registration confirmed working after session restart - status/tank_drive/tank_steer/tank_telemetry are live tools"
metadata: 
  node_type: memory
  type: project
  originSessionId: 2b9d6267-6a36-4434-a188-306a85c531bd
  modified: 2026-08-30T18:01:01.804Z
---

On 2026-08-30, `tank-app` was registered as a local HTTP MCP server (`claude mcp add --scope local --transport http tank-app http://localhost:8765/mcp`) while `wind.exe` was already running with its MCP HTTP endpoint live. It did not appear mid-session (see [[mcp_vscode_reconnect_limitation]]), but after a full session/extension restart the tools (`status`, `tank_drive`, `tank_steer`, `tank_telemetry`) showed up correctly via ToolSearch, and `status` returned `{"status":"running"}` confirming the connection works end-to-end.

**Why:** confirms the fix for [[mcp_vscode_reconnect_limitation]] is "restart the session/extension" - mid-session reconnect doesn't pick up newly-added local MCP servers, but a restart does.

**How to apply:** This item is resolved - no more pending verification needed. If tank-app tools ever stop appearing again, restart the session/extension first before deeper debugging.
