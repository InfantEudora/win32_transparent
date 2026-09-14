# lockd - a file-lock broker for several agents in one checkout

## What it is

`tools/lockd/` is a standalone MCP server that arbitrates who is editing what. An agent
claims the paths it is about to touch, edits them, and releases them; a second agent asking
for the same path is refused and told who holds it, how long they have held it and why, so
it can go and do something else instead of writing over the top.

It exists because several agents in one working copy clash on files, and the usual answer -
a `git worktree` each - is not wanted here: these are small tools and patch fixes against a
shared build, and the merge traffic costs more than the collisions do. This is the other
answer. It is advisory by default and enforced when the hook in section 6 is installed.

**It arbitrates more than files.** `build/core` is shared between all fourteen apps and only
one may build at a time; port 8765 holds one running app at a time. Both are named locks
(`#build`, `#port:8765`) in the same table, and they are arguably worth more than the file
locks, because both rules are currently kept by convention and nothing enforces either.

## 1. Running it

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
cd tools/lockd && mingw32-make.exe -j8        # mingw32-make, not /usr/bin/make
./build/lockd.exe 2>lockd.log &               # http://127.0.0.1:8766/mcp
```

Start it once and leave it. It holds no files open, does no I/O and costs nothing idle.
`--port` and `--root` override the defaults; `--help` prints them.

### Watching and clearing it from a terminal

The same exe is also the client. These do **not** start a server - they talk to the one
already running and exit, which matters because a second server would fail to bind the port
and then answer nothing, looking for all the world like it was working:

```bash
./build/lockd.exe --list                 # the lock table
./build/lockd.exe --release <owner>      # everything one owner holds
./build/lockd.exe --release-all          # everything there is - the panic button
```

```
9 lock(s) on 127.0.0.1:8766

  PATH                     OWNER     AGE      LEFT     REASON
  engine.mk                8923730d  5m08s    4m13s    auto-claimed by PreToolUse hook
  tools/assetpack/         8923730d  5m08s    4m13s    assetpack step 4: BAKE_ASSETS rules..
  #build                   8923730d  5m08s    4m13s    assetpack step 4: BAKE_ASSETS rules..
  tools/lockd/lockd.cpp    65345b23  3m02s    4m33s    auto-claimed by PreToolUse hook
```

Owner is the agent's session id, shortened to its first group. `--list` exits non-zero and
says so plainly when nothing is listening, so it is usable from a script.

**`--release-all` is an operator command and there is deliberately no MCP tool for it.** An
agent able to wipe the whole table could undo everyone else's protection in one call, which
is a worse failure than the collisions this prevents. It is composed client-side out of
`lock_list` and `lock_break`, so no such tool needs to exist. It prints what it took, with
owners and reasons, before taking it - that log is the only record of what was interrupted.
Nothing is lost for long either way: an agent still working re-claims on its next write
through the hook.

Killing the broker clears the table too, and is the cruder version of the same button. Prefer
`--release-all`: it leaves the server up, so agents' next claims succeed instead of failing
open until someone restarts it.

**Port 8766, not 8765.** 8765 is the port every app's embedded MCP server binds, exclusively
and one app at a time. The broker has to outlive every app start, stop and relink, so it
cannot share that port or that lifetime.

**`127.0.0.1`, never `localhost`.** Same reason as every other server here - `TCPServer`
binds IPv4 only, and where `localhost` resolves to `::1` first every call pays a failed IPv6
connect first. Measured at 2,058 ms against 15 ms elsewhere in this repo; nothing appears to
be wrong, it is just ~137x slower. See `docs/mcp_server.md`.

## 2. Registering it with an agent

`.mcp.json` at the repo root does this, and it is checked in:

```json
{ "mcpServers": { "lockd": { "type": "http", "url": "http://127.0.0.1:8766/mcp" } } }
```

Project scope rather than `claude mcp add` per agent, deliberately: an agent that never
registered the broker is precisely the one that will clobber somebody's file, so the
registration has to arrive with the checkout rather than depend on each agent having been told.
Claude Code asks each user to approve a project-scoped server once, the first time.

If the `lock_*` tools are missing from a session's tool list, either the broker was not running
when that session started or the session predates the registration - restart it. Note that the
VS Code extension's `/mcp` panel does not list locally-added servers, so it is not a way to check;
the tool list is.

Every agent working in this checkout must reach the *same* broker. That is also why **stdio
transport is deliberately not started** even though `core/MCPServer` supports it: an stdio server
is spawned by its client, so each agent would get a private broker with a private table and every
one of them would grant everything.

`.mcp.json` registers only lockd. The apps' own server on 8765 is left out on purpose - it is
bound by whichever app is running, often none, and a server that is usually absent is noise in
every session that is not driving an app.

## 3. What an agent does

1. `lock_claim` every path the task needs, **in one call**, with a one-line `reason`.
2. If refused: do other work and retry, or `lock_wait` if the holder looks about to finish.
   Do not edit anyway.
3. **Re-read the files.** A lease reserves the right to edit; it does not make a read taken
   before the lease current. This is the mistake worth designing against - the clash being
   prevented is usually not two simultaneous writes but a stale read-modify-write.
4. Edit.
5. `lock_release` when done, or with no `paths` to hand back everything at once.

## 4. Tools

| tool | arguments | notes |
|---|---|---|
| `lock_claim` | `owner`, `paths`, `reason`, `ttl_seconds` | all-or-nothing; returns `conflicts` on refusal |
| `lock_release` | `owner`, `paths` (optional) | no `paths` releases everything that owner holds |
| `lock_list` | - | every lease, with owner, reason, age, time left |
| `lock_refresh` | `owner`, `ttl_seconds` | rarely needed; any call carrying `owner` already refreshes |
| `lock_wait` | `owner`, `paths`, `timeout_seconds` | 1..30s, default 20 |
| `lock_break` | `paths` and/or `owner` | force-release; needs one of the two, so it cannot clear the table by accident |

`owner` is a stable id for the agent - its session id is ideal, and is what the hook uses.
`paths` takes a bare string or an array.

## 5. What counts as the same path

Two spellings of one file becoming two independent leases would look exactly like the broker
silently doing nothing, so keys are canonicalised hard:

| written | key |
|---|---|
| `core\Application.cpp` | `core/application.cpp` |
| `C:\IDE-E\...\win32_transparent\CORE\Application.CPP` | `core/application.cpp` |
| `./core//Application.cpp` | `core/application.cpp` |
| `apps/tetris/` | `apps/tetris/` (directory claim) |
| `#Build` | `#build` (named resource) |

Backslashes become slashes, runs of slashes collapse, everything is lowercased (Windows
filenames are case-insensitive), a leading `./` or `/` goes, and an absolute path under the
repo root is made relative to it - which is what lets an agent claiming `core/Application.cpp`
and a hook reporting the absolute path Claude Code handed it mean the same thing.

A **trailing slash claims a directory** and everything below it, for "I am restructuring this
folder". The overlap test runs both ways round: a folder claim is refused while somebody holds
one file inside it, as well as the other way round. A one-directional check would let a
folder-wide claim sail straight past the file locks it was about to trample.

A key starting with `#` is a **named resource**, not a path, and is only lowercased.

## 6. Enforcement: the PreToolUse hook

Advisory locks only bind agents that remember to ask, and the agent least likely to remember
is the one doing a two-line patch fix - which is the clash this exists to prevent.
`tools/lockd/claude_lock_hook.py` closes that, and **is installed** in `.claude/settings.json`
(tracked, so it reaches every agent in this checkout):

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Edit|Write|MultiEdit|NotebookEdit",
        "hooks": [
          { "type": "command",
            "command": "python \"$CLAUDE_PROJECT_DIR/tools/lockd/claude_lock_hook.py\"",
            "timeout": 5 }
        ]
      }
    ]
  }
}
```

**`$CLAUDE_PROJECT_DIR`, never a relative path.** Hook cwd is the session's and tracks whatever
`cd` the `Bash` tool last did, so a relative command breaks the moment an agent cd's somewhere
to build - and breaks *closed*, because an unresolvable script exits 2 and `PreToolUse` reads
exit 2 as "block this edit". `.claude/README.md` records the incident.

The hook only claims paths **inside the repo**; it locates the root from its own `__file__` and
lets anything else through unclaimed. A write to a scratchpad under `AppData` would otherwise
take a lease nothing can ever contend for - rows in `lock_list` that are pure noise and bury the
ones that matter.

On every write the hook claims the target for the session
(Claude Code gives hooks the `session_id`, which is a ready-made owner id), allows the edit if
granted, and blocks it with exit code 2 if somebody else holds it - stderr on exit 2 goes back
to the model, so the refusal arrives as something it can act on. An agent that has never heard
of lockd is protected and protects everyone else, without being told anything.

**It fails open.** Broker down, slow, or answering something unexpected - the edit proceeds. A
deconfliction tool that halts all work the moment it dies is worse than the collisions it
prevents, and "did I remember to start lockd" is not a question worth holding in your head to
edit a file.

The hook never releases - TTL ends its leases, because there is no PostToolUse moment that
means "finished with this file". It therefore claims for **300s** rather than lockd's own 900s
default. An agent still working refreshes on its next write anyway, so the shorter lease costs
an active agent nothing and only shortens how long an *abandoned* one blocks somebody else.
Four ordinary edits in a row, measured on the day it was installed, left that session holding
four files - `CLAUDE.md` among them - for the full fifteen minutes. An agent wanting to hand a
file back sooner calls `lock_release` itself.

## 7. Why it is built the way it is

**Leases, not locks.** Agents die mid-task - a crash, a closed tab, a context window running
out. A lock with no expiry means the first agent that dies wedges a file until somebody clears
it by hand, at the worst possible moment. Every lease carries a TTL (default 900s, clamped to
30..3600) and is reaped on access and on a 5s timer. An owner still working says so by calling
anything at all: **every call carrying an owner refreshes that owner's whole set**, including a
*refused* claim - an agent waiting on someone else is still alive.

**All-or-nothing claims.** A partial claim leaves an agent holding some of what it needs and
waiting for the rest, which is how two agents that cannot see each other deadlock: A holds
`Application.cpp` and wants `InputController.cpp`, B holds the reverse. Claim everything up
front and there is nothing to hold while waiting. This is also why `lock_wait` is capped at 30
seconds - it is for a holder seconds from finishing, not a queue. An agent parked in a tool
call is doing nothing at all, which is strictly worse than the same agent going away and coming
back.

**In memory only.** Restarting lockd clears every lease, and that is the intended way out of a
wedged table rather than an omission. Persisting them would mean a restart faithfully restoring
locks held by agents that no longer exist.

**A tool, not an app.** Every app here embeds `core/MCPServer`, so hanging the broker off one
is tempting and wrong twice over: the apps all bind 8765 and only one runs at a time, while the
broker must outlive all of them; and an app build writes into the shared `build/core` tree, so
building the deconfliction tool would race exactly the builds it exists to deconflict.
`tools/tools.mk` compiles its four core sources into `tools/lockd/build/obj` instead, and the
shared tree is untouchable from here by construction.

**Four core sources and about 300 lines of its own.** `MCPServer` pulls in `TCPServer` and
`Debug` and stops there - no window, no GL, no physics - so the broker gets the same JSON-RPC
dispatch, tool registry and HTTP transport every app has, nearly for free. That is the reason
this was worth building rather than reaching for something external.

## 8. What it does not do

- **It does not stop anything outside Claude Code.** An editor, a script or an agent without
  the hook writes whatever it likes. It is a protocol between cooperating agents, with the hook
  as a backstop for the ones that forget.
- **It does not know about git.** A lease says "I am editing this", not "this is checked out".
  Branch and index state are still yours to manage.
- **It does not make a stale read current** (section 3, step 3).
- **Directory claims are string prefixes**, not filesystem walks. `apps/tetris/` covers
  `apps/tetris/anything`, and knows nothing about symlinks or junctions.

## 9. Verified

`tools/lockd/` was exercised on 2026-09-14 against a running broker: refusal reporting holder,
reason and age; normalisation across backslashes, case, absolute paths and duplicate slashes;
atomicity (a mixed claim takes nothing - the free half stayed claimable); directory claims in
both directions; named resources; re-claiming your own; `lock_wait` returning on its timeout
rather than hanging; `lock_list`; release-all and release not touching another owner's leases;
malformed input reported rather than aborting the process (it builds `-fno-exceptions`, so a
wrong JSON type would otherwise `std::abort()` the broker and every lease in it); **40 threads
claiming one path simultaneously, exactly one winner**; and a lease expiring 30s after its
owner went silent, then being claimable by someone else. The hook was driven with the event
shape Claude Code sends: blocked with exit 2 and a usable message, allowed and auto-claimed on
a free file, not blocked by the session's own lease, passed a `Read` straight through, and
failed open with the broker stopped.
