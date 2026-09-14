---
name: shared-build-output-coordination
description: "ASK THE USER BEFORE BUILDING - another agent builds apps in this same tree, and the single wind.exe + single makefile means concurrent builds kill each other's running app"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 321ef6c3-8b60-4cdc-8703-73b7f520e3b5
  modified: 2026-09-14T09:41:53.886Z
---

**Ask the user before running `mingw32-make` in this tree.** Told 2026-09-12. Another agent is
building apps in the same working copy at the same time.

**Why:** there is one output binary (`wind.exe`) and one makefile for all twelve apps, selected by
`APP=`. So two agents building concurrently collide in three ways: the linker cannot overwrite a
running `wind.exe` (`Permission denied`); whoever builds last owns the binary, so the other
agent's `APP=` selection is silently replaced; and a `taskkill //F //IM wind.exe` before a build
kills the *other* agent's running app mid-session. Seen exactly this way during the Tank asset
migration - a verified-good Tank run disappeared between two MCP calls, and a screenshot came back
half black, with nothing wrong in its own log.

**How to apply:** before any build, ask the user whether the other agent has finished, and wait.
Symptoms to read as "the other agent, not my change": the app exiting with a clean log tail, a
`curl` to 127.0.0.1:8765 failing with exit 7 when it worked a moment earlier, a partially rendered
screenshot, or `wind.exe` having a newer mtime than my own build.

**There is now a lock for this.** `tools/lockd/` (built 2026-09-14, see `docs/lock_broker.md` and
the CLAUDE.md section) is an MCP broker other agents in this tree claim paths from; the shared
`build/core` tree is the named lock `#build` and a running app's port is `#port:8765`. Claim
`#build` before `mingw32-make` rather than guessing whether the other agent has finished. Still
ask the user when the broker is not running - it is advisory unless the PreToolUse hook is
installed, and it holds nothing across its own restart.

**Agreed direction that removes the problem:** each app gets **its own output exe and its own
makefile**, rather than one `wind.exe` selected by `APP=`. Planned as part of the asset
reorganisation - see [[asset-layout-plan]] and `docs/asset_layout_plan.md`. Once that lands, two
agents can build different apps without interfering at all, and the `.current_app` sentinel that
guards `main.o` and `core/File.o` against stale `APP`-dependent objects stops being needed.
