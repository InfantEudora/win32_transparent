#ifndef _LOCKTABLE_H_
#define _LOCKTABLE_H_

#include <string>
#include <vector>
#include <mutex>
#include <stdint.h>

/*
    The lease table behind lockd - see docs/lock_broker.md.

    Kept apart from the MCP plumbing in lockd.cpp because it is the only part with any
    behaviour worth reasoning about on its own: normalisation, overlap, expiry. lockd.cpp
    is then just argument checking and JSON.

    LEASES, NOT LOCKS. Every entry carries an expiry and is reaped on the next access
    (ReapExpired, called at the top of every public method). Agents die mid-task - a
    crash, a closed tab, a context window running out - and a lock with no expiry means
    the first agent that dies wedges a file until someone clears it by hand, which is
    exactly the moment nobody wants to be debugging the deconfliction tool. An owner that
    is still working says so by calling anything at all; every call that carries an owner
    refreshes that owner's whole set, EACH LEASE BY ITS OWN LENGTH.

    That last part is a fix, and the bug it fixes is worth remembering. A refresh used to
    move every lease to the TTL of whichever call did the refreshing, so an agent that
    claimed #build for an hour and then edited one file - the hook claims for 300s - had
    quietly cut its #build to five minutes. The next long build lost it, somebody else took
    it, and the first agent went on building with no idea. Seen twice on 2026-09-30.

    A lease that goes anyway - expired, or broken by hand - leaves a LostLease behind, which
    the owner is handed on its next call (TakeLost). Losing a lease is survivable; losing
    one and carrying on as if it were still held is the failure, and it is silent unless
    somebody says so. See docs/lock_broker.md section 7.

    Every method takes m_lock itself, so callers never hold it - which matters because
    lock_wait polls Claim in a loop and must not sleep inside the mutex.
*/

struct Lease {
    std::string key;    // normalised - see Normalize()
    std::string owner;
    std::string reason;
    int64_t granted_ms = 0; // steady clock, not wall clock: only differences are ever used
    int64_t expires_ms = 0;
    int ttl_s = 0;          // this lease's own length, which every renewal restores
    bool f_auto = false;    // taken implicitly by the PreToolUse hook on a write, not asked for
    bool f_keep_while_waiting = false; // a file lease EndTurn leaves alone - see there
};

// A lease that ended without its owner releasing it. Kept until the owner next calls, or
// until LOST_KEEP_S passes - by then the owner is gone and there is nobody left to tell.
struct LostLease {
    std::string key;
    std::string owner;
    std::string reason;
    std::string how;        // "expired" or "broken"
    int64_t lost_ms = 0;
};

// What an owner's turn end did to its explicit leases, kept for its next turn - see EndTurn.
struct TurnEnd {
    std::string owner;
    int64_t ended_ms = 0;
    std::vector<Lease> released; // explicit file leases handed back so others could take them
    std::vector<Lease> kept;     // explicit leases it went on holding: every #name, and keep_while_waiting files
};

// One reason a claim was refused. `requested` is what the caller asked for and `held` is
// what is actually leased; the two differ when a directory claim covers a file claim or
// the other way round, and reporting both is what makes "apps/tetris/ is held" legible to
// an agent that asked for apps/tetris/main.cpp.
struct Conflict {
    std::string requested;
    std::string held;
    std::string owner;
    std::string reason;
    int64_t held_for_s = 0;
    int64_t expires_in_s = 0;
};

class LockTable {
public:
    // repo_root is stripped off absolute paths so that an agent claiming
    // "core/Application.cpp" and a PreToolUse hook reporting the absolute path Claude Code
    // gave it are talking about the same key. Passing "" disables that step.
    explicit LockTable(const std::string &repo_root);

    /*
        Whole-repo canonical form for one key, so that two spellings of the same file
        cannot become two independent leases - which would look exactly like the broker
        silently doing nothing:

          - backslashes to forward slashes, runs of slashes collapsed
          - lowercased, because Windows filenames are case-insensitive
          - "./" prefix and any leading slash removed
          - an absolute path under repo_root made relative to it
          - a trailing slash PRESERVED, because that is what marks a directory claim

        A key beginning with '#' is a named non-file resource (#build, #port:8765) and is
        only lowercased. See docs/lock_broker.md.
    */
    std::string Normalize(const std::string &raw) const;

    /*
        All-or-nothing: either every key is leased to owner, or none is and conflicts_out
        describes why. Atomicity is the point rather than a nicety - a partial claim leaves
        an agent holding some of what it needs and waiting for the rest, which is how two
        agents that cannot see each other deadlock. Claim everything up front and there is
        nothing to hold while waiting.

        Keys this owner already holds are compatible with itself and are simply re-leased,
        so a re-claim after a refresh is not an error.

        ttl_seconds is clamped to [TTL_MIN_S, TTL_MAX_S]; pass 0 for TTL_DEFAULT_S.

        f_auto marks the hook's implicit claim on a write. It never downgrades a lease the
        owner asked for explicitly: editing a file you claimed for an hour must not turn
        that claim into a five-minute auto one. f_keep_while_waiting is likewise set by an
        explicit claim only, and an auto one leaves it as it was.
    */
    bool Claim(const std::string &owner, const std::vector<std::string> &keys,
               const std::string &reason, int ttl_seconds, bool f_auto,
               bool f_keep_while_waiting,
               std::vector<std::string> *granted_out, std::vector<Conflict> *conflicts_out);

    // Releases the named keys held by this owner, or every key it holds when `all` is set.
    // Keys held by somebody else are ignored rather than reported: release is how an agent
    // tidies up, and failing it because of a key it never held helps nobody.
    std::vector<std::string> Release(const std::string &owner,
                                     const std::vector<std::string> &keys, bool all);

    // Extends every lease this owner holds. A ttl_seconds > 0 becomes the new length of
    // each explicit lease; 0 renews each by its own. Returns how many.
    int Refresh(const std::string &owner, int ttl_seconds);

    /*
        "This owner is still working." Sent by the hook before every tool call, so that an
        explicit claim lives as long as its agent is active rather than as long as its agent
        happens to keep making lock calls - builds and replays make none. Renews explicit
        leases only: an auto lease is "I just wrote this file", and renewing those on every
        Read would have a session sit on each file it ever touched until it went quiet.
        Returns how many explicit leases the owner holds.
    */
    int Heartbeat(const std::string &owner);

    // Hands over, and forgets, every lease this owner lost since it last asked, with the
    // current holder of each key filled into holders_out (empty string where free).
    std::vector<LostLease> TakeLost(const std::string &owner, std::vector<std::string> *holders_out);

    /*
        "This owner has stopped and is waiting." Sent by the Stop hook when an agent ends its
        turn - to wait for a subagent, a background command, another window or the person.
        Its FILE leases go, auto and explicit alike, so that a finished change does not sit
        on a file for as long as the wait lasts. That was unbounded while waiting on a
        subagent: a subagent's tool calls carry its parent's session id, so its heartbeats
        kept the parent's leases alive the whole time.

        Files are safe to hand back because nothing can be written on the strength of a
        stale lease: the hook re-claims a file on the next write and refuses it if somebody
        else has it, and Claude Code refuses to write a file that changed since it was read.
        Named resources (#build, #port:*) are NOT released - they usually stand for a process
        that outlives the turn, a background build or a running app, and nothing re-claims
        them on their next use. Nor is a file lease claimed with f_keep_while_waiting, which
        is for a background command that writes the file itself.

        Records a TurnEnd for TakeTurnEnd when the owner held any explicit lease; auto ones
        go silently, for the same reason their expiry does. The released keys are returned
        in released_out, auto ones included, and what stayed in kept_out.
    */
    void EndTurn(const std::string &owner, std::vector<std::string> *released_out,
                 std::vector<std::string> *kept_out);

    // Hands over, and forgets, the owner's last TurnEnd, trimmed to what is still true: a
    // released key it has since claimed again is dropped, and so is a kept one it no longer
    // holds. False when nothing is left to say.
    bool TakeTurnEnd(const std::string &owner, TurnEnd *out);

    std::vector<Lease> List();

    // The manual override, for a lease whose owner is gone and whose TTL has not run out
    // yet. Either keys, or every key belonging to one owner, or both.
    std::vector<std::string> Break(const std::vector<std::string> &keys, const std::string &owner);

    // Drops expired leases. Called at the top of every public method; also called on a
    // timer by main() so that an idle broker still logs an expiry when it happens rather
    // than at the next unrelated call.
    int ReapExpired(std::vector<Lease> *reaped_out);

    static const int TTL_DEFAULT_S = 900;
    static const int TTL_MIN_S = 30;
    static const int TTL_MAX_S = 3600;
    static const int LOST_KEEP_S = 3600;
    // Longer than LOST_KEEP_S because a waiting agent comes back: a turn ends on a question
    // to the person far more often than on a dead session, and the answer can be a day off.
    static const int TURN_END_KEEP_S = 24 * 3600;

    static int64_t NowMs();

private:
    // Unlocked internals - every caller below already holds m_lock.
    int ReapExpiredLocked(std::vector<Lease> *reaped_out);
    void TouchLocked(const std::string &owner, bool f_include_auto);
    void RecordLostLocked(const Lease &lease, const char *how, int64_t now);

    // Does a claim on `a` collide with a lease on `b`? Equal keys collide, and a directory
    // claim collides with everything beneath it in either direction.
    static bool Overlaps(const std::string &a, const std::string &b);

    static int ClampTTL(int ttl_seconds);

    std::mutex m_lock;
    std::vector<Lease> m_leases;
    std::vector<LostLease> m_lost;
    std::vector<TurnEnd> m_turn_ends; // at most one per owner
    std::string m_root; // normalised, lowercased, trailing '/', or empty
};

#endif
