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

    STDIO IS DELIBERATELY NOT STARTED, AND STDIN BELONGS TO THE OPERATOR. MCPServer
    supports an stdio transport and registering the broker over it would be a mistake worth
    naming: an stdio server is spawned by its client, so every agent would get its own
    private broker with its own private lock table and all of them would grant everything.
    One process, one HTTP port, many clients is the only arrangement that means anything
    here.

    That left stdin free, and it is now spoken for: the broker runs in the FOREGROUND by
    default with an interactive console on it - see the console section below. Nothing in
    this program may ever call MCPServer::Get()->Start(); the stdio reader and the console
    would race for every line typed, and the one that lost would be the person.
*/

#include "MCPServer.h"  // first - winsock2.h before windows.h, see its own note
#include "LockTable.h"
#include "Debug.h"

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static Debugger *debug = new Debugger("lockd", DEBUG_ALL);

static const int DEFAULT_PORT = 8766; // NOT 8765 - that is the running app's, one at a time

static LockTable *g_table = nullptr;

//Only ever read as a difference against NowMs(), for the console's `status`. Set in main once
//the table exists, so "uptime" means what an operator would expect it to: how long this broker
//has been holding leases, not how long the process has been parsing arguments.
static int64_t g_started_ms = 0;

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

// The lease table as a terminal shows it. Split out of CmdList so the interactive console
// prints the identical thing from the identical code - the two sources of the same table
// drifting apart would be a small bug that wastes a real amount of somebody's attention.
// Takes the wire shape (what lock_list returns) rather than std::vector<Lease>, because
// that is what the CLI client has and the console can produce it in one call.
static void PrintLockTable(const json &locks, int port) {
    if (locks.empty()) {
        printf("No locks held (127.0.0.1:%d).\n", port);
        return;
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
}

static int CmdList(int port) {
    json locks;
    if (!FetchLocks(port, &locks)) {
        return 1;
    }
    PrintLockTable(locks, port);
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
// Interactive console
//
// The broker runs in the FOREGROUND by default - `./lockd`, or the exe double-clicked from
// an Explorer window - and reads commands from stdin on the main thread. That is why the
// note at the top of this file is emphatic about MCPServer's stdio transport staying off.
//
// It exists because the operator commands were already here and in the wrong place. To look
// at the table, the person watching the broker had to open a SECOND shell and run a second
// copy of the exe against the window already in front of them; and --release-all - the one
// button deliberately withheld from agents, since an agent able to wipe the table could undo
// everyone else's protection in a single call - was the most awkward of the lot to reach.
// The console puts them where the operator already is.
//
// The commands are the CLI's, by the same names, printing through the same PrintLockTable.
// They call the LockTable directly rather than looping back through HTTP: the table is right
// there behind its own mutex, and a broker that talked to itself over a socket would have one
// more way to fail than it needs.
//---------------------------------------------------------------------------------------

// Is somebody already answering on this port? Used twice: once before binding, where a second
// broker is the failure worth catching early, and once by `status`, where "the port I asked
// for" and "the port I am answering on" are not the same claim and only the second is useful.
static bool ProbePort(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false; // can't tell - say no and let the bind have its go rather than refuse to run
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bool f_open = connect(sock, (sockaddr *)&addr, sizeof(addr)) != SOCKET_ERROR;
    closesocket(sock);
    //Safe to hand the refcount back even though the server is about to want sockets: TCPServer
    //calls WSAStartup itself when it starts listening.
    WSACleanup();
    return f_open;
}

//Mute is per-Debugger-handle and reversible rather than one global switch. The global that
//already exists, Debugger::enable_console, is all-or-nothing and would take the errors with
//it - and an error is the one thing worth interrupting a muted console for. Each handle's
//level is saved on the way in, so unmute restores what that handle actually had, which is not
//necessarily DEBUG_ALL: blanket-restoring would make a deliberately quiet subsystem chatty for
//the first time in its life.
static std::map<std::string, int> g_saved_levels;
static bool f_muted = false;

static void SetMuted(bool f_mute) {
    if (f_mute == f_muted) {
        return;
    }
    std::map<std::string, Debugger *> *handles = Debugger::GetHandles();
    if (f_mute) {
        g_saved_levels.clear();
        for (const std::pair<const std::string, Debugger *> &kv : *handles) {
            if (!kv.second) {
                continue;
            }
            g_saved_levels[kv.first] = kv.second->level;
            kv.second->SetLevel(DEBUG_ERROR);
        }
    } else {
        for (const std::pair<const std::string, Debugger *> &kv : *handles) {
            std::map<std::string, int>::const_iterator saved = g_saved_levels.find(kv.first);
            if (!kv.second || saved == g_saved_levels.end()) {
                continue;
            }
            //Assigned, NOT SetLevel(): Debugger::SetLevel stores its argument minus one, so
            //feeding it a value read back out of `level` would lower the bar by one on every
            //mute/unmute cycle until everything printed again.
            kv.second->level = saved->second;
        }
        g_saved_levels.clear();
    }
    f_muted = f_mute;
}

// Owners are session ids and the table shows only their first group (ShortOwner), so the string
// an operator has in front of them to type is NOT the string Break matches on. Accepting any
// unambiguous prefix is what makes `release 8923730d` do what it plainly means; without it the
// command silently reports "no locks held by ..." for an owner visibly holding four.
static bool ResolveOwner(const std::string &typed, std::string *out) {
    std::vector<Lease> leases = g_table->List();
    std::vector<std::string> matches;
    for (const Lease &lease : leases) {
        if (lease.owner == typed) {
            *out = typed; // an exact hit wins outright, prefix of something else or not
            return true;
        }
        if (lease.owner.compare(0, typed.size(), typed) == 0 &&
            std::find(matches.begin(), matches.end(), lease.owner) == matches.end()) {
            matches.push_back(lease.owner);
        }
    }
    if (matches.empty()) {
        printf("No locks held by '%s'.\n", typed.c_str());
        return false;
    }
    if (matches.size() > 1) {
        printf("'%s' matches %d owners - be more specific:\n", typed.c_str(), (int)matches.size());
        for (const std::string &owner : matches) {
            printf("  %s\n", owner.c_str());
        }
        return false;
    }
    *out = matches[0];
    return true;
}

static void ConsoleList(int port) {
    PrintLockTable(LeasesToJson(g_table->List()), port);
}

static void ConsoleStatus(int port, const std::string &root) {
    printf("  endpoint    http://127.0.0.1:%d/mcp%s\n", port,
           ProbePort(port) ? "" : "   -- NOT LISTENING");
    printf("  repo root   %s\n",
           root.empty() ? "(none - absolute paths are kept as they arrive)" : root.c_str());
    printf("  uptime      %s\n",
           FormatDuration((LockTable::NowMs() - g_started_ms) / 1000).c_str());
    printf("  leases      %d\n", (int)g_table->List().size());
    printf("  logging     %s\n", f_muted ? "muted - errors only" : "full");
}

static void ConsoleRelease(const std::string &typed) {
    std::string owner;
    if (!ResolveOwner(typed, &owner)) {
        return; // ResolveOwner has already said why
    }
    std::vector<std::string> broken = g_table->Break(std::vector<std::string>(), owner);
    printf("Released %d lock(s) held by %s:\n", (int)broken.size(), owner.c_str());
    for (const std::string &key : broken) {
        printf("  %s\n", key.c_str());
    }
}

static void ConsoleBreak(const std::string &path) {
    //Normalised here because Break matches keys as given, and every key in the table went
    //through Normalize on the way in. Typing the path with backslashes, or in the case Explorer
    //shows it, would otherwise match nothing and look like the lock was already gone.
    std::vector<std::string> keys;
    keys.push_back(g_table->Normalize(path));
    std::vector<std::string> broken = g_table->Break(keys, std::string());
    if (broken.empty()) {
        printf("Nothing held on '%s'.\n", keys[0].c_str());
        return;
    }
    for (const std::string &key : broken) {
        printf("Broke %s\n", key.c_str());
    }
}

static void ConsoleReleaseAll() {
    std::vector<Lease> leases = g_table->List();
    if (leases.empty()) {
        printf("Nothing held - nothing to release.\n");
        return;
    }
    //Printed BEFORE the break, with each owner and reason, because this throws away other
    //agents' state: the log of what was taken is the only way to tell afterwards what was
    //interrupted. They are not lost for long - an agent still working re-claims on its next
    //write through the PreToolUse hook.
    printf("Releasing %d lock(s):\n", (int)leases.size());
    std::vector<std::string> keys;
    for (const Lease &lease : leases) {
        std::string reason = Truncate(lease.reason, 64);
        printf("  %-40s %s%s%s\n", lease.key.c_str(), ShortOwner(lease.owner).c_str(),
               reason.empty() ? "" : " - ", reason.c_str());
        keys.push_back(lease.key);
    }
    printf("\nCleared %d.\n", (int)g_table->Break(keys, std::string()).size());
}

static void ConsoleHelp() {
    printf("  list, l           the lock table (a bare Enter does the same)\n"
           "  status            endpoint, repo root, uptime, lease count, logging\n"
           "  release <owner>   release every lock one owner holds; any unambiguous\n"
           "                    prefix works, so the short id in the table will do\n"
           "  release-all       release every lock there is - the panic button\n"
           "  break <path>      force-release one path\n"
           "  mute, unmute      suppress engine logging below error level\n"
           "  help, ?           this\n"
           "  quit, exit        stop the broker, dropping every lease\n");
}

// Returns false at EOF, which is not an error: the broker may have been started with stdin
// closed or redirected (`./lockd </dev/null &`). The caller then stops reading and goes on
// serving, rather than spinning on it - which a naive while(fgets(...)) would do at 100% of a
// core, in the one process nobody is watching.
static bool ReadConsoleLine(std::string *out) {
    out->clear();
    char chunk[512];
    for (;;) {
        if (!fgets(chunk, sizeof(chunk), stdin)) {
            return false;
        }
        *out += chunk;
        if (!out->empty() && out->back() == '\n') {
            break; // otherwise fgets stopped on a full buffer, not a line end
        }
    }
    while (!out->empty() && strchr(" \t\r\n", out->back())) {
        out->pop_back();
    }
    size_t start = out->find_first_not_of(" \t");
    *out = (start == std::string::npos) ? std::string() : out->substr(start);
    return true;
}

// True when the operator asked to quit, false at end of input. The two must not be the same
// answer: quit stops the broker, EOF only means nobody is typing.
static bool RunConsole(int port, const std::string &root) {
    printf("lockd on 127.0.0.1:%d - 'help' for commands, 'quit' to stop the broker.\n", port);
    for (;;) {
        printf("lockd> ");
        fflush(stdout); // a prompt is not a whole line, so nothing else will flush it

        std::string line;
        if (!ReadConsoleLine(&line)) {
            printf("\n");
            return false;
        }
        //A bare Enter re-prints the table. It is the thing wanted most often by a long way -
        //this console exists mainly to be watched - and costs one line to make free.
        if (line.empty()) {
            ConsoleList(port);
            continue;
        }

        //The argument is the whole rest of the line rather than one token, because paths and
        //owners can contain spaces and quoting rules at a prompt are a thing to get wrong
        //rather than a feature. No command here takes two arguments.
        std::string cmd = line;
        std::string arg;
        size_t space = line.find_first_of(" \t");
        if (space != std::string::npos) {
            cmd = line.substr(0, space);
            size_t arg_start = line.find_first_not_of(" \t", space);
            if (arg_start != std::string::npos) {
                arg = line.substr(arg_start);
            }
        }
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "list" || cmd == "l" || cmd == "ls") {
            ConsoleList(port);
        } else if (cmd == "status" || cmd == "st") {
            ConsoleStatus(port, root);
        } else if (cmd == "release") {
            if (arg.empty()) {
                printf("release needs an owner - 'list' shows who holds what, and the short "
                       "id it prints is enough.\n");
            } else {
                ConsoleRelease(arg);
            }
        } else if (cmd == "release-all") {
            ConsoleReleaseAll();
        } else if (cmd == "break") {
            if (arg.empty()) {
                printf("break needs a path.\n");
            } else {
                ConsoleBreak(arg);
            }
        } else if (cmd == "mute") {
            SetMuted(true);
            printf("Muted - errors only. 'unmute' restores.\n");
        } else if (cmd == "unmute") {
            SetMuted(false);
            printf("Unmuted.\n");
        } else if (cmd == "help" || cmd == "?" || cmd == "h") {
            ConsoleHelp();
        } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            //Said, not asked. Stopping the broker drops every lease - the crude version of
            //release-all - but an operator typing 'quit' at their own console means it, and a
            //confirmation prompt on the way out of a dev tool earns nothing.
            int held = (int)g_table->List().size();
            if (held > 0) {
                printf("Stopping - %d lease(s) dropped. Agents re-claim on their next write.\n",
                       held);
            }
            return true;
        } else {
            printf("Unknown command '%s' - 'help' lists them.\n", cmd.c_str());
        }
    }
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

// Reap on a timer as well as on access, so that an expiry in an otherwise idle broker is logged
// when it happens rather than surfacing at the next unrelated call. On its own thread since the
// main one now reads the console, and detached because it has no shutdown to coordinate - the
// process exiting is its shutdown, and the table it is reaping dies with it.
static void ReaperLoop() {
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

// A window opened by double-clicking the exe closes the instant main() returns, taking whatever
// went wrong with it. Only worth doing where there is a console to hold open; at EOF fgets
// returns immediately, so a redirected stdin cannot wedge a startup failure here.
static void WaitForExitKey(bool f_console) {
    if (!f_console) {
        return;
    }
    //The message this is holding the window open FOR went to stderr, and the prompt goes to
    //stdout - two streams whose buffering differs the moment either is redirected. Flushed here
    //so the reason always precedes the prompt rather than sometimes following it.
    fflush(stderr);
    printf("\nPress Enter to close.\n");
    fflush(stdout);
    char discard[8];
    fgets(discard, sizeof(discard), stdin);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    std::string root;

    // Which of the client commands was asked for, if any. Resolved after the whole argument
    // list is read rather than acted on where it is seen, so that --port may follow it.
    enum Mode { MODE_SERVE, MODE_LIST, MODE_RELEASE_ALL, MODE_RELEASE };
    Mode mode = MODE_SERVE;
    std::string release_owner;

    //On by DEFAULT, which is the whole point of it: the broker is meant to be a window you can
    //see and type into. --no-console is for the older arrangement, `./lockd 2>lockd.log &`,
    //where nothing is going to be typed and stdin may not even be attached.
    bool f_console = true;

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
        } else if (arg == "--no-console") {
            f_console = false;
        } else if (arg == "--help" || arg == "-h") {
            printf("lockd - advisory file-lock broker over MCP for this repo.\n"
                   "\n"
                   "Run the broker (no arguments). It runs in the foreground with an\n"
                   "interactive console on stdin - type 'help' at its prompt:\n"
                   "  --port <n>        listen port (default %d; NOT 8765, that is the app's)\n"
                   "  --root <dir>      repo root absolute paths are made relative to\n"
                   "                    (default: three levels above the exe)\n"
                   "  --no-console      serve without reading stdin, for running it\n"
                   "                    backgrounded with its log redirected\n"
                   "\n"
                   "Talk to a broker that is already running (these do not start one):\n"
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

    //Asked BEFORE binding, because the failure it catches is the quiet one. StartHttp logs a
    //bind error and carries on with the transport off, which used to leave a silent and useless
    //process; with a console it would leave something worse - a prompt answering every command
    //from an empty table that no agent can see, while the real broker runs in another window.
    //Two double-clicks on the exe is all it takes, and is now the likeliest way to start it.
    if (ProbePort(port)) {
        fprintf(stderr, "lockd: something is already listening on 127.0.0.1:%d - a broker is\n"
                        "       already running. Use that one; this process would serve nobody.\n",
                port);
        WaitForExitKey(f_console);
        return 1;
    }

    debug->Info("lockd starting - repo root: %s\n", root.empty() ? "(none)" : root.c_str());

    g_table = new LockTable(root);
    g_started_ms = LockTable::NowMs();
    RegisterTools();

    // HTTP only, on purpose - see the note at the top of this file about stdio, which is now
    // doubly true: stdin is the console's.
    MCPServer::Get()->StartHttp(port);

    debug->Info("Leases are in memory only: restarting lockd clears every lock, which is "
                "the intended way out of a wedged table.\n");

    std::thread(ReaperLoop).detach();

    if (f_console && RunConsole(port, root)) {
        return 0; // 'quit' - the leases go with the process, as they always have
    }

    //Either --no-console, or the console read EOF. Neither is a reason to stop: the broker's
    //job does not depend on anyone typing at it, and an agent mid-task is depending on it.
    if (f_console) {
        debug->Info("Console input closed - serving without it. Stop with Ctrl-C.\n");
    }
    for (;;) {
        Sleep(60000);
    }
}
