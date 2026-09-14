# `.claude/`

Claude Code working files for this repo.

## `memory/` (tracked)

Claude's persistent project memory: one fact per file, plus `MEMORY.md` as the
index that gets loaded into context at the start of every session. This used to
live outside the repo under the per-user profile directory; it was moved in here
on 2026-09-06 so it is version-controlled and reviewable alongside the code.

Claude still reads and writes it at its usual per-user path, which is now a
**directory junction** pointing here:

```
C:\Users\Dick\.claude\projects\c--IDE-E-Mijn-Documenten-Projects-code-test-win32-transparent\memory
    -> <repo>\.claude\memory
```

Recreate it after a fresh clone (or on another machine) with:

```powershell
New-Item -ItemType Junction `
  -Path   "$env:USERPROFILE\.claude\projects\c--IDE-E-Mijn-Documenten-Projects-code-test-win32-transparent\memory" `
  -Target "<repo>\.claude\memory"
```

A junction, not a symlink, because junctions don't need Developer Mode or
elevation on Windows. Git only ever sees the real directory here, so the link
itself is never committed. If the junction is missing, Claude will just create a
fresh empty memory directory at the profile path and start over — the repo copy
stays intact, so restoring is a matter of deleting that directory and re-running
the command above.

## `settings.json` (tracked)

One `PreToolUse` hook, matching `Edit|Write|MultiEdit|NotebookEdit`, which runs
`tools/lockd/claude_lock_hook.py` before any write. That claims the target path
from the lock broker for the current session and blocks the edit if another agent
holds it. See `docs/lock_broker.md` §6 for what it does and why, and the top of
the hook script for how it behaves when the broker is down (it lets the edit
through).

Tracked rather than in `settings.local.json` on purpose: every agent working in
this checkout needs the same rule, and one that opted out would be exactly the one
that overwrites somebody's file.

The command path must be **`$CLAUDE_PROJECT_DIR`-qualified, never relative**, and
this one cost an agent its afternoon. It was written relative first, on the
assumption that hooks run with the working directory set to the project root. They
do not: cwd is the *session's*, and it tracks whatever `cd` the `Bash` tool last
did. So an agent that cd'd into `tools/assetpack` to build it then found every
subsequent write refused with

```
can't open file '...\tools\assetpack\tools\lockd\claude_lock_hook.py': [Errno 2]
```

because a path that fails to resolve exits 2 from the interpreter, and a
`PreToolUse` hook reads exit 2 as "block this edit". One `cd` disables writing
until you `cd` back, and nothing about the message says so. Measured 2026-09-14:
`$CLAUDE_PROJECT_DIR` does expand correctly here, with the hook both allowing and
blocking from a non-root cwd.

If a bad command ever does wedge writes this way, recovery is via the `Bash` tool —
this matcher does not cover it.

Measured when it was installed on 2026-09-14: the hook took effect **immediately**,
in the session that wrote this file, without reloading the window — the four edits
made right after it appeared in the lock table, auto-claimed. Do not rely on the
opposite being true either; if a change to the hook does not seem to be taking,
reload before concluding the hook itself is wrong.

## `scratch/` (ignored)

Temp files, traces, one-off scripts. Gitignored via `.claude/scratch/` in
`.gitignore`. Nothing in here is meant to be kept.
