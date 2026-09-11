---
name: shell-heredoc-limit
description: Heredocs here need a QUOTED delimiter and break above ~30KB with a misleading ENAMETOOLONG - use a file-writing tool for anything large
metadata:
  type: feedback
---

Two separate heredoc failures keep being rediscovered by every agent on this project. Both are now
written up in `CLAUDE.md` at the repo root, which is the file to keep current.

1. **Quote the delimiter: `<<'EOF'`, never `<<EOF`.** Unquoted, the shell expands `$`, backticks
   and `\` inside the body, mangling Python, GLSL and anything with a `$` in it. This is the
   "heredoc quoting choked on my script" failure.

1b. **A quoted delimiter still collapses `\\` to `\`.** It protects `$` and backticks but not
   backslashes, so any escaped string changes one level on the way through - a C `"...\n"` written
   from a heredoc'd Python script arrives as a real newline inside the literal and fails to compile.
   Measured 2026-09-11. If the text contains a backslash, write the script with the file-writing
   tool and then run it.

2. **The whole command, heredoc body included, goes to `CreateProcess` as one string**, which
   Windows caps at 32,767 characters. Over that the call fails with
   `ENAMETOOLONG: name too long, uv_spawn` - which mentions neither length nor heredocs and reads
   like a path problem. Observed writing a 35,280-byte document.

**Why:** small heredocs genuinely work and are convenient, so the failure only shows up on the one
occasion it is most expensive - part-way through writing a large file.

**How to apply:** use heredocs for throwaway scripts, always with a quoted delimiter. Use the
Write/Edit tools for documents and source files, regardless of size. Do not conclude from an
`ENAMETOOLONG` that the path is wrong.

Related: [[build_toolchain_location]], [[project_overview]]
