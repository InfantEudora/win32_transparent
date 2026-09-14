#!/usr/bin/env python
"""
PreToolUse hook turning lockd's advisory leases into enforced ones.

Installed in .claude/settings.json since 2026-09-14, so it applies to every agent working in
this checkout. Without it lockd only binds agents that remember to ask, and the agent least
likely to remember is the one doing a two-line patch fix, which is exactly the clash it
exists to prevent.

Only the standard library, deliberately: a hook has to work under whichever `python` is
first on PATH, and with /c/msys64/mingw64/bin ahead of it for a build that is MSYS's
interpreter, which has no third-party packages (see CLAUDE.md). urllib and json are in both.

What it does, on every Edit/Write/MultiEdit/NotebookEdit:

  - claims the target path for this session, using Claude Code's own session_id as the
    owner. The claim is implicit, so an agent that never heard of lockd is still protected
    and still protects everyone else.
  - lets the edit through if the claim is granted (which includes the common case of
    already holding it), blocks it with exit code 2 if somebody else holds it. stderr on
    exit 2 goes back to the model, so the refusal arrives as something it can act on
    rather than an opaque failure.

IT FAILS OPEN. If lockd is not running, or is slow, or answers something unexpected, the
edit proceeds. A deconfliction tool that stops all fourteen apps' worth of work the moment
it dies is worse than the clashes it prevents, and "did I remember to start lockd" is not
a question anyone should have to hold in their head to edit a file.

The leases this takes are never released explicitly - the TTL is what ends them, because
there is no PostToolUse moment that means "done with this file".

That is why CLAIM_TTL_S is well below lockd's own 900s default. An agent that is still
working refreshes its leases on its very next write anyway, so a short lease costs an
active agent nothing; all it changes is how long an ABANDONED one blocks somebody else.
Measured on the day this was installed: four ordinary edits in a row left that session
sitting on four files - CLAUDE.md among them - for the full fifteen minutes, which is a
long time to hold a file everyone touches on the strength of one edit.
"""
import json
import os
import sys
import urllib.error
import urllib.request

LOCKD_URL = "http://127.0.0.1:8766/mcp"  # 127.0.0.1, never localhost - see docs/mcp_server.md
TIMEOUT_S = 2.0
CLAIM_TTL_S = 300  # see the note on TTLs above
WRITING_TOOLS = ("Edit", "Write", "MultiEdit", "NotebookEdit")

# This file is <repo>/tools/lockd/claude_lock_hook.py, so the repo is three levels up. Taken
# from __file__ rather than from cwd or $CLAUDE_PROJECT_DIR: the script already knows where it
# is, and that is one fewer thing that can be wrong at the moment it is asked to decide
# whether somebody may write a file.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def allow():
    sys.exit(0)


def inside_repo(path):
    """Is this path somewhere lockd has any business arbitrating?

    Only repo files are worth a lease. An agent writing to its scratchpad under AppData - or
    anywhere else off the tree - would otherwise take one that no other agent can ever
    contend for: rows in lock_list that are pure noise, and that bury the ones which matter.
    """
    root = os.path.normcase(REPO_ROOT)
    try:
        # A relative path from the hook event is repo-relative by construction.
        resolved = os.path.normcase(os.path.abspath(os.path.join(REPO_ROOT, path)))
        return os.path.commonpath([resolved, root]) == root
    except ValueError:
        return False  # different drive; commonpath refuses, and the answer is no anyway


def main():
    raw = sys.stdin.read()
    event = json.loads(raw)

    if event.get("tool_name") not in WRITING_TOOLS:
        allow()

    tool_input = event.get("tool_input") or {}
    path = tool_input.get("file_path") or tool_input.get("notebook_path")
    owner = event.get("session_id")
    if not path or not owner:
        allow()

    if not inside_repo(path):
        allow()

    body = json.dumps({
        "jsonrpc": "2.0", "id": 1, "method": "tools/call",
        "params": {"name": "lock_claim", "arguments": {
            "owner": owner,
            "paths": [path],
            "reason": "auto-claimed by PreToolUse hook",
            "ttl_seconds": CLAIM_TTL_S,
        }},
    }).encode()

    req = urllib.request.Request(LOCKD_URL, data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT_S) as response:
        result = json.loads(response.read().decode())

    payload = json.loads(result["result"]["content"][0]["text"])
    if payload.get("granted"):
        allow()

    lines = ["lockd: another agent is editing this file. The edit was NOT applied."]
    for conflict in payload.get("conflicts", []):
        covered = conflict.get("covered_by")
        lines.append("  %s is held by %s for %ss (%s)%s" % (
            conflict.get("requested"),
            conflict.get("held_by"),
            conflict.get("held_for_s"),
            conflict.get("reason", "no reason given"),
            (", via the directory claim %s" % covered) if covered else "",
        ))
    lines.append("Work on something else and come back, or call lock_wait if it looks "
                 "about to finish. Re-read the file before retrying - it will have changed.")
    sys.stderr.write("\n".join(lines) + "\n")
    sys.exit(2)


if __name__ == "__main__":
    try:
        main()
    except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError):
        # Fail open, loudly enough to be findable but without blocking the edit.
        sys.stderr.write("lockd: broker unreachable or unexpected reply, allowing edit\n")
        sys.exit(0)
