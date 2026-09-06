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

## `scratch/` (ignored)

Temp files, traces, one-off scripts. Gitignored via `.claude/scratch/` in
`.gitignore`. Nothing in here is meant to be kept.
