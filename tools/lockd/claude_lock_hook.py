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

It runs before EVERY tool call (matcher "*" since 2026-09-30) and does one of two things:

  - On Edit/Write/MultiEdit/NotebookEdit of a repo file: claims the target path for this
    session, using Claude Code's own session_id as the owner, and marks the claim `auto`.
    Granted (which includes already holding it) lets the edit through; held by somebody
    else blocks it with exit code 2. stderr on exit 2 goes back to the model, so the
    refusal arrives as something it can act on rather than an opaque failure.

  - On anything else: sends lock_heartbeat. That renews the session's EXPLICIT claims -
    #build, #port:*, files it asked for - so they last as long as the agent is working,
    not as long as it happens to keep making lock calls. A build or a replay makes none,
    and that is how two agents on 2026-09-30 each lost a lease mid-task, had it taken, and
    carried on as if they still held it.

Either reply can carry `lost`: leases this session held and no longer does, because they
expired or somebody broke them. When it does, THIS tool call is refused, once, with a
message naming each one and who holds it now. That is deliberately blunt. Losing a lease is
survivable; carrying on as if it were still held is the failure, and a note the agent might
skim past is how it stays silent. The broker forgets the loss once it has said so, so the
retry goes through.

The lockd tools themselves are passed straight through: the broker hears those directly and
their replies carry `lost` on their own.

IT FAILS OPEN. If lockd is not running, or is slow, or answers something unexpected, the
call proceeds. A deconfliction tool that stops all work the moment it dies is worse than
the clashes it prevents, and "did I remember to start lockd" is not a question anyone
should have to hold in their head to edit a file. An older broker without lock_heartbeat
answers the heartbeat with an error, which counts as "nothing to say".

The auto leases this takes are never released explicitly - the TTL is what ends them,
because there is no PostToolUse moment that means "done with this file", and the heartbeat
deliberately does not renew them.

That is why CLAIM_TTL_S is well below lockd's own 900s default. An agent that is still
editing renews them on its very next write anyway, so a short lease costs an active agent
nothing; all it changes is how long a file stays held after the last write to it. Measured
on the day this was installed: four ordinary edits in a row left that session sitting on
four files - CLAUDE.md among them - for the full fifteen minutes, which is a long time to
hold a file everyone touches on the strength of one edit. The broker no longer lets this
short length leak onto the session's explicit claims; it once did, and that was the bug.
"""
import json
import os
import sys
import urllib.error
import urllib.request

# 127.0.0.1, never localhost - see docs/mcp_server.md. LOCKD_URL is for testing a new broker
# on a side port without touching the one everybody is using.
LOCKD_URL = os.environ.get("LOCKD_URL", "http://127.0.0.1:8766/mcp")
TIMEOUT_S = 2.0
CLAIM_TTL_S = 300  # see the note on TTLs above
WRITING_TOOLS = ("Edit", "Write", "MultiEdit", "NotebookEdit")
LOCKD_TOOL_PREFIX = "mcp__lockd__"

# This file is <repo>/tools/lockd/claude_lock_hook.py, so the repo is three levels up. Taken
# from __file__ rather than from cwd or $CLAUDE_PROJECT_DIR: the script already knows where it
# is, and that is one fewer thing that can be wrong at the moment it is asked to decide
# whether somebody may write a file.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def allow():
    sys.exit(0)


def refuse(lines):
    sys.stderr.write("\n".join(lines) + "\n")
    sys.exit(2)


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


def call_tool(name, arguments):
    """The tool's payload, or None where the broker answered with an error rather than a
    result - which is what an older broker does with a tool it has never heard of."""
    body = json.dumps({
        "jsonrpc": "2.0", "id": 1, "method": "tools/call",
        "params": {"name": name, "arguments": arguments},
    }).encode()
    req = urllib.request.Request(LOCKD_URL, data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT_S) as response:
        result = json.loads(response.read().decode())
    if "result" not in result:
        return None
    payload = json.loads(result["result"]["content"][0]["text"])
    if "error" in payload:
        return None
    return payload


def lost_lines(payload):
    lines = []
    for entry in payload.get("lost", []):
        how = "expired" if entry.get("how") == "expired" else "was broken by hand"
        holder = entry.get("now_held_by")
        lines.append("  %s %s %ss ago (%s) - %s" % (
            entry.get("path"),
            how,
            entry.get("ago_s"),
            entry.get("reason", "no reason given"),
            ("now held by %s" % holder) if holder else "free now",
        ))
    return lines


def refuse_for_lost(payload, what_was_held):
    lines = ["lockd: you have LOST leases you were relying on. %s" % what_was_held]
    lines += lost_lines(payload)
    lines.append("You do not hold these any more. Do not build, run or edit on their strength: "
                 "lock_claim them again first (it may be refused - then do other work), and "
                 "re-read any file before editing it, since someone else may have changed it. "
                 "If something of yours is still running on a lost #build or #port, it is now "
                 "racing whoever took it. This notice is shown once; retrying goes through.")
    refuse(lines)


def claim_for_write(owner, path):
    payload = call_tool("lock_claim", {
        "owner": owner,
        "paths": [path],
        "reason": "auto-claimed by PreToolUse hook",
        "ttl_seconds": CLAIM_TTL_S,
        "auto": True,
    })
    if payload is None:
        allow()

    if payload.get("granted"):
        if payload.get("lost"):
            refuse_for_lost(payload, "The edit was NOT applied, so that you see this first.")
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
    if payload.get("lost"):
        lines.append("Also - you have LOST leases you were relying on:")
        lines += lost_lines(payload)
    refuse(lines)


def heartbeat(owner):
    payload = call_tool("lock_heartbeat", {"owner": owner})
    if payload is None or not payload.get("lost"):
        allow()
    refuse_for_lost(payload, "This tool call was NOT run, so that you see this first.")


def main():
    raw = sys.stdin.read()
    event = json.loads(raw)

    tool = event.get("tool_name") or ""
    owner = event.get("session_id")
    if not owner or tool.startswith(LOCKD_TOOL_PREFIX):
        allow()

    if tool in WRITING_TOOLS:
        tool_input = event.get("tool_input") or {}
        path = tool_input.get("file_path") or tool_input.get("notebook_path")
        if path and inside_repo(path):
            claim_for_write(owner, path)

    heartbeat(owner)


if __name__ == "__main__":
    try:
        main()
    except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError, IndexError):
        # Fail open, loudly enough to be findable but without blocking the call.
        sys.stderr.write("lockd: broker unreachable or unexpected reply, allowing call\n")
        sys.exit(0)
