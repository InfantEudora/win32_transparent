/*
    lockd - a standalone MCP server that arbitrates who is editing what in this repo.

    Several agents working in one checkout clash on files. Worktrees are the usual answer
    and are not the one wanted here; this is the other one, an advisory lock broker that
    agents claim paths from before they edit and release when they are done. See
    docs/lock_broker.md for the protocol, the failure modes and the PreToolUse hook that
    turns "advisory" into "enforced".

    WHY THIS IS A TOOL AND NOT AN APP. Every app in this repo already embeds
    core/MCPServer, so the temptation is to hang the broker off one of them. That would be
    wrong twice over: the apps all bind port 8765 and only one may run at a time, while
    the broker has to outlive every app start, stop and relink; and an app build writes
    into the shared build/core tree, so building the deconfliction tool would race exactly
    the builds it exists to deconflict. Hence tools/, tools.mk, its own object tree, and
    its own port.

    STDIO IS DELIBERATELY NOT STARTED. MCPServer supports it, and registering this over
    stdio would be a mistake worth naming: an stdio server is spawned by its client, so
    every agent would get its own private broker with its own private lock table and all
    of them would grant everything. One process, one HTTP port, many clients is the only
    arrangement that means anything here.
*/

#include "MCPServer.h"  // first - winsock2.h before windows.h, see its own note
#include "LockTable.h"
#include "Debug.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static Debugger *debug = new Debugger("lockd", DEBUG_ALL);

static const int DEFAULT_PORT = 8766; // NOT 8765 - that is the running app's, one at a time

static LockTable *g_table = nullptr;

/*
    There used to be a priority-101 static constructor here that read GetCommandLineA() and
    cleared Debugger::enable_console, so that --list did not print three lines of engine log
    above its table. It was needed because core's Debugger announced itself from its own
    constructor, before main() could have any say.

    core/Debug.cpp no longer emits that line at all, so the trick is gone and nothing replaced
    it. Recorded here only because "why is --list quiet?" is a reasonable question: it is quiet
    because nothing logs until something has something to say.
*/

//---------------------------------------------------------------------------------------
// Argument helpers.
//
// The whole repo builds with -fno-exceptions and JSON_NOEXCEPTION, which makes a wrong
// type in a tool call fatal rather than reportable: json::get<std::string>() on a number
// calls std::abort() and takes the broker - and every lease in it - down with it. So
// everything here is is_string()/value() and never at() or a bare get().
//---------------------------------------------------------------------------------------

static json ErrorResult(const std::string &message) {
    return json{ { "error", message } };
}

static bool ReadOwner(const json &args, std::string *out, json *error_out) {
    const json &owner = args.contains("owner") ? args["owner"] : json();
    if (!owner.is_string() || owner.get<std::string>().empty()) {
        *error_out = ErrorResult("'owner' is required and must be a non-empty string - use a "
                                 "stable id for this agent, e.g. its session id.");
        return false;
    }
    *out = owner.get<std::string>();
    return true;
}

// Accepts either a bare string or an array of them, because both spellings will be tried
// and refusing one of them buys nothing. Normalises and de-duplicates; a claim listing the
// same file twice is one lease, not a self-conflict.
static bool ReadKeys(const json &args, const char *field, std::vector<std::string> *out,
                     json *error_out) {
    if (!args.contains(field)) {
        return true; // absent is not an error here - callers decide whether it is required
    }
    const json &value = args[field];
    std::vector<std::string> raw;
    if (value.is_string()) {
        raw.push_back(value.get<std::string>());
    } else if (value.is_array()) {
        for (const json &entry : value) {
            if (!entry.is_string()) {
                *error_out = ErrorResult(std::string("'") + field + "' must contain strings only.");
                return false;
            }
            raw.push_back(entry.get<std::string>());
        }
    } else {
        *error_out = ErrorResult(std::string("'") + field + "' must be a string or an array of strings.");
        return false;
    }

    for (const std::string &entry : raw) {
        std::string key = g_table->Normalize(entry);
        if (key.empty()) {
            continue;
        }
        if (std::find(out->begin(), out->end(), key) == out->end()) {
            out->push_back(key);
        }
    }
    return true;
}

static int ReadTTL(const json &args) {
    const json &ttl = args.contains("ttl_seconds") ? args["ttl_seconds"] : json();
    return ttl.is_number_integer() ? ttl.get<int>() : 0; // 0 -> the table's default
}

static std::string ReadReason(const json &args) {
    const json &reason = args.contains("reason") ? args["reason"] : json();
    return reason.is_string() ? reason.get<std::string>() : std::string();
}

static json ConflictsToJson(const std::vector<Conflict> &conflicts) {
    json out = json::array();
    for (const Conflict &c : conflicts) {
        json entry = {
            { "requested", c.requested },
            { "held_by", c.owner },
            { "held_for_s", c.held_for_s },
            { "expires_in_s", c.expires_in_s }
        };
        if (c.held != c.requested) {
            // Only worth saying when they differ - that is the directory-claim case, and
            // without it "apps/tetris/main.cpp is taken" is baffling to whoever asked.
            entry["covered_by"] = c.held;
        }
        if (!c.reason.empty()) {
            entry["reason"] = c.reason;
        }
        out.push_back(entry);
    }
    return out;
}

static json LeasesToJson(const std::vector<Lease> &leases) {
    int64_t now = LockTable::NowMs();
    json out = json::array();
    for (const Lease &lease : leases) {
        json entry = {
            { "path", lease.key },
            { "owner", lease.owner },
            { "held_for_s", (now - lease.granted_ms) / 1000 },
            { "expires_in_s", (lease.expires_ms - now) / 1000 }
        };
        if (!lease.reason.empty()) {
            entry["reason"] = lease.reason;
        }
        out.push_back(entry);
    }
    return out;
}

//---------------------------------------------------------------------------------------
// Tools
//
// The descriptions below are the ONLY documentation the other agents will ever read, so
// they carry the workflow rather than just naming the arguments.
//---------------------------------------------------------------------------------------

static json ToolClaim(const json &args) {
    std::string owner;
    json error;
    if (!ReadOwner(args, &owner, &error)) {
        return error;
    }
    std::vector<std::string> keys;
    if (!ReadKeys(args, "paths", &keys, &error)) {
        return error;
    }
    if (keys.empty()) {
        return ErrorResult("'paths' is required and must name at least one path.");
    }

    std::vector<std::string> granted;
    std::vector<Conflict> conflicts;
    bool ok = g_table->Claim(owner, keys, ReadReason(args), ReadTTL(args), &granted, &conflicts);

    if (!ok) {
        debug->Info("DENIED %s wanted %d path(s), %d conflict(s)\n", owner.c_str(),
                    (int)keys.size(), (int)conflicts.size());
        return json{
            { "granted", false },
            { "conflicts", ConflictsToJson(conflicts) },
            { "hint", "Nothing was claimed - a claim is all-or-nothing. Work on something "
                      "else and retry, or use lock_wait for a short bounded wait." }
        };
    }

    std::vector<Lease> mine = g_table->List();
    int64_t expires_in = 0;
    for (const Lease &lease : mine) {
        if (lease.owner == owner) {
            expires_in = (lease.expires_ms - LockTable::NowMs()) / 1000;
            break;
        }
    }
    debug->Info("GRANTED %s <- %d path(s)\n", owner.c_str(), (int)granted.size());
    return json{
        { "granted", true },
        { "paths", granted },
        { "expires_in_s", expires_in },
        { "note", "Re-read these files now. A lock reserves the right to edit, it does not "
                  "make a read you did earlier current." }
    };
}

static json ToolRelease(const json &args) {
    std::string owner;
    json error;
    if (!ReadOwner(args, &owner, &error)) {
        return error;
    }
    std::vector<std::string> keys;
    if (!ReadKeys(args, "paths", &keys, &error)) {
        return error;
    }
    bool all = keys.empty();
    std::vector<std::string> released = g_table->Release(owner, keys, all);
    debug->Info("RELEASED %s -> %d path(s)%s\n", owner.c_str(), (int)released.size(),
                all ? " (all)" : "");
    return json{ { "released", released }, { "count", (int)released.size() } };
}

static json ToolList(const json & /*args*/) {
    std::vector<Lease> leases = g_table->List();
    return json{ { "locks", LeasesToJson(leases) }, { "count", (int)leases.size() } };
}

static json ToolRefresh(const json &args) {
    std::string owner;
    json error;
    if (!ReadOwner(args, &owner, &error)) {
        return error;
    }
    int ttl = ReadTTL(args);
    int count = g_table->Refresh(owner, ttl);
    return json{
        { "refreshed", count },
        { "expires_in_s", ttl > 0 ? ttl : LockTable::TTL_DEFAULT_S }
    };
}

static json ToolBreak(const json &args) {
    json error;
    std::vector<std::string> keys;
    if (!ReadKeys(args, "paths", &keys, &error)) {
        return error;
    }
    const json &owner_arg = args.contains("owner") ? args["owner"] : json();
    std::string owner = owner_arg.is_string() ? owner_arg.get<std::string>() : std::string();
    if (keys.empty() && owner.empty()) {
        return ErrorResult("lock_break needs 'paths', 'owner', or both - it will not clear "
                           "the whole table by accident.");
    }
    std::vector<std::string> broken = g_table->Break(keys, owner);
    debug->Warn("BROKEN %d lease(s) by hand\n", (int)broken.size());
    return json{ { "broken", broken }, { "count", (int)broken.size() } };
}

static json ToolWait(const json &args) {
    std::string owner;
    json error;
    if (!ReadOwner(args, &owner, &error)) {
        return error;
    }
    std::vector<std::string> keys;
    if (!ReadKeys(args, "paths", &keys, &error)) {
        return error;
    }
    if (keys.empty()) {
        return ErrorResult("'paths' is required and must name at least one path.");
    }

    const json &timeout_arg = args.contains("timeout_seconds") ? args["timeout_seconds"] : json();
    int timeout_s = timeout_arg.is_number_integer() ? timeout_arg.get<int>() : 20;
    // Capped hard. An agent parked inside a tool call is doing nothing at all, which is
    // strictly worse than the same agent going away and coming back - the tool exists for
    // the case where the holder is seconds from finishing, not as a queue.
    if (timeout_s < 1) timeout_s = 1;
    if (timeout_s > 30) timeout_s = 30;

    std::string reason = ReadReason(args);
    int ttl = ReadTTL(args);

    int64_t deadline = LockTable::NowMs() + (int64_t)timeout_s * 1000;
    std::vector<Conflict> conflicts;
    for (;;) {
        std::vector<std::string> granted;
        conflicts.clear();
        if (g_table->Claim(owner, keys, reason, ttl, &granted, &conflicts)) {
            debug->Info("GRANTED (after wait) %s <- %d path(s)\n", owner.c_str(), (int)granted.size());
            return json{
                { "granted", true },
                { "paths", granted },
                { "note", "Re-read these files now - they may have changed while you waited." }
            };
        }
        if (LockTable::NowMs() >= deadline) {
            break;
        }
        Sleep(250);
    }
    debug->Info("TIMED OUT %s after %ds\n", owner.c_str(), timeout_s);
    return json{
        { "granted", false },
        { "waited_s", timeout_s },
        { "conflicts", ConflictsToJson(conflicts) },
        { "hint", "Still held. Do something else and come back - do not spin on this." }
    };
}

static void RegisterTools() {
    // Reused by every tool that takes them, so the wording an agent reads is the same
    // wherever it reads it.
    json paths_schema = {
        { "type", "array" },
        { "items", { { "type", "string" } } },
        { "description",
          "Repo-relative paths (absolute paths under the repo root are accepted and "
          "converted). A trailing slash claims a whole directory and everything below it. "
          "A name starting with '#' claims a non-file resource; the ones this repo has are "
          "'#build' (the shared build/core tree - build one app at a time) and '#port:8765' "
          "(the port a running app's MCP server binds, one app at a time)." }
    };
    json owner_schema = {
        { "type", "string" },
        { "description", "Stable id for the claiming agent - its session id is ideal. "
                         "Everything you hold is keyed on this, so use the same value all session." }
    };
    json ttl_schema = {
        { "type", "integer" },
        { "description", "Lease length in seconds (default 900, clamped to 30..3600). Every "
                         "call you make refreshes all of your leases, so the default is "
                         "generous enough for ordinary work." }
    };

    MCPServer::Get()->RegisterTool(
        "lock_claim",
        "Claim exclusive edit rights over one or more paths before you edit them. "
        "ALL-OR-NOTHING: if any path is held by someone else nothing is claimed, and the "
        "reply names who holds what and for how long - so claim everything the task needs "
        "in ONE call rather than accumulating paths, which is what stops two agents "
        "deadlocking on each other. On refusal, go do other work and retry; do not edit "
        "anyway. Re-read files after a claim is granted: the lock reserves the right to "
        "edit, it does not refresh a read you did before you had it. Release with "
        "lock_release when you are done - do not sit on locks across a whole session.",
        json{
            { "type", "object" },
            { "properties", {
                { "owner", owner_schema },
                { "paths", paths_schema },
                { "reason", { { "type", "string" },
                              { "description", "One line on what you are doing, shown to "
                                               "whoever is refused because of you." } } },
                { "ttl_seconds", ttl_schema }
            } },
            { "required", json::array({ "owner", "paths" }) }
        },
        ToolClaim);

    MCPServer::Get()->RegisterTool(
        "lock_release",
        "Release paths you hold. With no 'paths', releases everything you hold - which is "
        "what to call when a task is finished or abandoned.",
        json{
            { "type", "object" },
            { "properties", {
                { "owner", owner_schema },
                { "paths", paths_schema }
            } },
            { "required", json::array({ "owner" }) }
        },
        ToolRelease);

    MCPServer::Get()->RegisterTool(
        "lock_list",
        "Every lease currently held, with owner, stated reason, age and time left. Worth a "
        "look before planning work that touches shared files.",
        json{ { "type", "object" }, { "properties", json::object() } },
        ToolList);

    MCPServer::Get()->RegisterTool(
        "lock_refresh",
        "Extend every lease you hold. Rarely needed explicitly - any call carrying your "
        "owner id already refreshes them - but useful during a long edit with no other "
        "lock traffic.",
        json{
            { "type", "object" },
            { "properties", { { "owner", owner_schema }, { "ttl_seconds", ttl_schema } } },
            { "required", json::array({ "owner" }) }
        },
        ToolRefresh);

    MCPServer::Get()->RegisterTool(
        "lock_wait",
        "Claim paths, waiting up to timeout_seconds (default 20, max 30) for them to come "
        "free. Only worth using when the holder looks about to finish; otherwise take the "
        "refusal from lock_claim and do other work, because an agent sitting in this call "
        "is doing nothing at all.",
        json{
            { "type", "object" },
            { "properties", {
                { "owner", owner_schema },
                { "paths", paths_schema },
                { "reason", { { "type", "string" } } },
                { "ttl_seconds", ttl_schema },
                { "timeout_seconds", { { "type", "integer" },
                                       { "description", "1..30, default 20." } } }
            } },
            { "required", json::array({ "owner", "paths" }) }
        },
        ToolWait);

    MCPServer::Get()->RegisterTool(
        "lock_break",
        "Force-release leases regardless of owner, by path and/or by owner - at least one "
        "of the two is required. For a lease whose agent is gone and whose TTL has not run "
        "out yet. Not for taking a file off an agent that is still working - check "
        "lock_list first.",
        json{
            { "type", "object" },
            { "properties", {
                { "paths", paths_schema },
                { "owner", { { "type", "string" },
                             { "description", "Break every lease held by this owner." } } }
            } }
        },
        ToolBreak);
}

//---------------------------------------------------------------------------------------
// CLI client mode
//
// `lockd --list` and `lockd --release-all` do NOT start a server - they talk to the one
// already running and exit. That distinction matters more than it looks: a second server
// would fail to bind the port and then sit there answering nothing, which is precisely the
// confusion the 8765 rule exists to prevent (docs/mcp_server.md).
//
// This exists because the useful view of the lock table belongs in a terminal, not in an
// agent's tool output. When something has gone wrong, the person sorting it out wants to
// see who holds what without asking an agent to ask the broker.
//
// NOTE that there is deliberately no `lock_clear` MCP tool to go with --release-all. The
// panic button is the operator's; an agent that could wipe every lease in the table could
// undo everyone else's protection with one call, which is a strictly worse failure than
// the collisions this prevents. --release-all is composed client-side out of lock_list and
// lock_break, so it needs no such tool to exist.
//---------------------------------------------------------------------------------------

// One request, one response, connection closed - the shape MCPServer's HTTP transport
// speaks. No keep-alive to manage because there is never a second request.
static bool PostJson(int port, const std::string &body, std::string *out, std::string *error) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        *error = "WSAStartup failed";
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        *error = "socket() failed";
        return false;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    //The literal, never "localhost" - the server binds AF_INET only, and a name that
    //resolves to ::1 first costs ~2s per call for nothing. See docs/mcp_server.md.
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        *error = "not reachable on 127.0.0.1:" + std::to_string(port) + " - is lockd running?";
        return false;
    }

    std::string request = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: "
                          "application/json\r\nContent-Length: ";
    request += std::to_string(body.size());
    request += "\r\nConnection: close\r\n\r\n";
    request += body;
    if (send(sock, request.c_str(), (int)request.size(), 0) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        *error = "send() failed";
        return false;
    }

    std::string response;
    char chunk[4096];
    for (;;) {
        int got = recv(sock, chunk, sizeof(chunk), 0);
        if (got <= 0) {
            break; // Connection: close, so the peer closing IS the end of the response
        }
        response.append(chunk, got);
    }
    closesocket(sock);
    WSACleanup();

    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        *error = "malformed response (no header terminator)";
        return false;
    }
    *out = response.substr(headerEnd + 4);
    return true;
}

// Unwraps the two layers an MCP tool result comes in: the JSON-RPC envelope, and the
// content[0].text block the handler's own JSON was serialised into.
static bool CallTool(int port, const std::string &name, const json &arguments,
                     json *result, std::string *error) {
    json request = {
        { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "tools/call" },
        { "params", { { "name", name }, { "arguments", arguments } } }
    };

    std::string body;
    if (!PostJson(port, request.dump(), &body, error)) {
        return false;
    }

    json response = json::parse(body, nullptr, false);
    if (response.is_discarded()) {
        *error = "unparsable response";
        return false;
    }
    if (response.contains("error")) {
        *error = response["error"].value("message", "unknown JSON-RPC error");
        return false;
    }
    if (!response.contains("result") || !response["result"].contains("content") ||
        !response["result"]["content"].is_array() || response["result"]["content"].empty()) {
        *error = "response had no tool content";
        return false;
    }
    const json &first = response["result"]["content"][0];
    if (!first.contains("text") || !first["text"].is_string()) {
        *error = "tool content was not text";
        return false;
    }
    *result = json::parse(first["text"].get<std::string>(), nullptr, false);
    if (result->is_discarded()) {
        *error = "tool returned unparsable JSON";
        return false;
    }
    if (result->contains("error")) {
        *error = (*result)["error"].is_string() ? (*result)["error"].get<std::string>()
                                                 : result->dump();
        return false;
    }
    return true;
}

static std::string FormatDuration(int64_t seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    char buf[32];
    if (seconds < 60) {
        snprintf(buf, sizeof(buf), "%llds", (long long)seconds);
    } else if (seconds < 3600) {
        snprintf(buf, sizeof(buf), "%lldm%02llds", (long long)(seconds / 60),
                 (long long)(seconds % 60));
    } else {
        snprintf(buf, sizeof(buf), "%lldh%02lldm", (long long)(seconds / 3600),
                 (long long)((seconds % 3600) / 60));
    }
    return buf;
}

static std::string Truncate(const std::string &text, size_t width) {
    if (text.size() <= width) {
        return text;
    }
    if (width <= 2) {
        return text.substr(0, width);
    }
    return text.substr(0, width - 2) + "..";
}

// Owners are usually Claude Code session ids - 36-character UUIDs whose first group already
// distinguishes them from anything else in the table. Truncating those generically would spend
// the column on "8923730d-d1.." when "8923730d" says the same thing and lines up.
static std::string ShortOwner(const std::string &owner) {
    if (owner.size() >= 36 && owner[8] == '-' && owner[13] == '-') {
        return owner.substr(0, 8);
    }
    return Truncate(owner, 14);
}

static bool FetchLocks(int port, json *locks) {
    json result;
    std::string error;
    if (!CallTool(port, "lock_list", json::object(), &result, &error)) {
        fprintf(stderr, "lockd: %s\n", error.c_str());
        return false;
    }
    if (!result.contains("locks") || !result["locks"].is_array()) {
        fprintf(stderr, "lockd: unexpected lock_list result\n");
        return false;
    }
    *locks = result["locks"];
    return true;
}

static int CmdList(int port) {
    json locks;
    if (!FetchLocks(port, &locks)) {
        return 1;
    }
    if (locks.empty()) {
        printf("No locks held (127.0.0.1:%d).\n", port);
        return 0;
    }

    //Width from the data rather than a fixed guess: most keys here are short repo-relative
    //paths, and a column sized for the longest possible one would be mostly blank.
    size_t path_w = 4, owner_w = 5;
    for (const json &lock : locks) {
        path_w = (std::max)(path_w, Truncate(lock.value("path", ""), 52).size());
        owner_w = (std::max)(owner_w, ShortOwner(lock.value("owner", "")).size());
    }

    printf("%d lock(s) on 127.0.0.1:%d\n\n", (int)locks.size(), port);
    printf("  %-*s  %-*s  %-7s  %-7s  %s\n", (int)path_w, "PATH", (int)owner_w, "OWNER",
           "AGE", "LEFT", "REASON");
    for (const json &lock : locks) {
        //Reason is truncated and deliberately last: an agent writing a paragraph about what it
        //is doing is a good thing, but one such row should not wrap the whole table out of
        //alignment. The full text is a lock_list away.
        printf("  %-*s  %-*s  %-7s  %-7s  %s\n",
               (int)path_w, Truncate(lock.value("path", ""), 52).c_str(),
               (int)owner_w, ShortOwner(lock.value("owner", "")).c_str(),
               FormatDuration(lock.value("held_for_s", (int64_t)0)).c_str(),
               FormatDuration(lock.value("expires_in_s", (int64_t)0)).c_str(),
               Truncate(lock.value("reason", ""), 64).c_str());
    }
    return 0;
}

static int CmdReleaseAll(int port) {
    json locks;
    if (!FetchLocks(port, &locks)) {
        return 1;
    }
    if (locks.empty()) {
        printf("Nothing held - nothing to release.\n");
        return 0;
    }

    //Printed BEFORE the break, and with the owner and reason of each, because this throws
    //away other agents' state: the log of what was taken is the only way to tell afterwards
    //what was interrupted. They are not lost for long - an agent still working re-claims on
    //its next write through the PreToolUse hook.
    printf("Releasing %d lock(s) on 127.0.0.1:%d:\n", (int)locks.size(), port);
    json paths = json::array();
    for (const json &lock : locks) {
        std::string reason = Truncate(lock.value("reason", ""), 64);
        printf("  %-40s %s%s%s\n", lock.value("path", "").c_str(),
               ShortOwner(lock.value("owner", "")).c_str(),
               reason.empty() ? "" : " - ", reason.c_str());
        paths.push_back(lock.value("path", ""));
    }

    json result;
    std::string error;
    if (!CallTool(port, "lock_break", json{ { "paths", paths } }, &result, &error)) {
        fprintf(stderr, "lockd: %s\n", error.c_str());
        return 1;
    }
    printf("\nCleared %d.\n", result.value("count", 0));
    return 0;
}

static int CmdRelease(int port, const std::string &owner) {
    json result;
    std::string error;
    if (!CallTool(port, "lock_break", json{ { "owner", owner } }, &result, &error)) {
        fprintf(stderr, "lockd: %s\n", error.c_str());
        return 1;
    }
    int count = result.value("count", 0);
    if (count == 0) {
        printf("No locks held by '%s'.\n", owner.c_str());
        return 0;
    }
    printf("Released %d lock(s) held by '%s':\n", count, owner.c_str());
    if (result.contains("broken") && result["broken"].is_array()) {
        for (const json &path : result["broken"]) {
            printf("  %s\n", path.is_string() ? path.get<std::string>().c_str() : "?");
        }
    }
    return 0;
}

//---------------------------------------------------------------------------------------
// Repo root
//
// Needed so that "core/Application.cpp" from an agent and the absolute path a PreToolUse
// hook is handed by Claude Code normalise to the same key. The exe sits at
// <root>/tools/lockd/build/lockd.exe, so the root is three levels up from its folder.
//---------------------------------------------------------------------------------------
static std::string DefaultRepoRoot() {
    char exe_path[MAX_PATH] = { 0 };
    if (!GetModuleFileNameA(NULL, exe_path, MAX_PATH)) {
        return "";
    }
    std::string dir(exe_path);
    size_t slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) {
        return "";
    }
    dir.erase(slash);
    dir += "\\..\\..\\..";

    char full[MAX_PATH] = { 0 };
    if (!GetFullPathNameA(dir.c_str(), MAX_PATH, full, NULL)) {
        return "";
    }
    return std::string(full);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    std::string root;

    // Which of the client commands was asked for, if any. Resolved after the whole argument
    // list is read rather than acted on where it is seen, so that --port may follow it.
    enum Mode { MODE_SERVE, MODE_LIST, MODE_RELEASE_ALL, MODE_RELEASE };
    Mode mode = MODE_SERVE;
    std::string release_owner;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--port") && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if ((arg == "--root") && i + 1 < argc) {
            root = argv[++i];
        } else if (arg == "--list" || arg == "-l") {
            mode = MODE_LIST;
        } else if (arg == "--release-all") {
            mode = MODE_RELEASE_ALL;
        } else if ((arg == "--release") && i + 1 < argc) {
            mode = MODE_RELEASE;
            release_owner = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printf("lockd - advisory file-lock broker over MCP for this repo.\n"
                   "\n"
                   "Run the broker (no arguments):\n"
                   "  --port <n>        listen port (default %d; NOT 8765, that is the app's)\n"
                   "  --root <dir>      repo root absolute paths are made relative to\n"
                   "                    (default: three levels above the exe)\n"
                   "\n"
                   "Talk to a broker that is already running:\n"
                   "  --list, -l        print the lock table\n"
                   "  --release <owner> release every lock held by one owner\n"
                   "  --release-all     release every lock there is (the panic button)\n"
                   "\n"
                   "Registered for agents by .mcp.json at the repo root.\n"
                   "See docs/lock_broker.md.\n",
                   DEFAULT_PORT);
            return 0;
        } else {
            fprintf(stderr, "lockd: unrecognised argument '%s' (try --help)\n", arg.c_str());
            return 1;
        }
    }

    //Client commands talk to the running broker and exit. Starting a server here would bind
    //nothing (the port is taken) and then answer nothing, which looks like working.
    if (mode == MODE_LIST) {
        return CmdList(port);
    }
    if (mode == MODE_RELEASE_ALL) {
        return CmdReleaseAll(port);
    }
    if (mode == MODE_RELEASE) {
        return CmdRelease(port, release_owner);
    }

    if (root.empty()) {
        root = DefaultRepoRoot();
    }
    debug->Info("lockd starting - repo root: %s\n", root.empty() ? "(none)" : root.c_str());

    g_table = new LockTable(root);
    RegisterTools();

    // HTTP only, on purpose - see the note at the top of this file about stdio.
    MCPServer::Get()->StartHttp(port);

    debug->Info("Leases are in memory only: restarting lockd clears every lock, which is "
                "the intended way out of a wedged table.\n");

    // Reap on a timer as well as on access, so that an expiry in an otherwise idle broker
    // is logged when it happens rather than surfacing at the next unrelated call.
    for (;;) {
        Sleep(5000);
        std::vector<Lease> reaped;
        g_table->ReapExpired(&reaped);
        for (const Lease &lease : reaped) {
            debug->Warn("EXPIRED %s held by %s (%s)\n", lease.key.c_str(), lease.owner.c_str(),
                        lease.reason.empty() ? "no reason given" : lease.reason.c_str());
        }
    }
}
