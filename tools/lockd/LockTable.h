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
    refreshes that owner's whole set.

    Every method takes m_lock itself, so callers never hold it - which matters because
    lock_wait polls Claim in a loop and must not sleep inside the mutex.
*/

struct Lease {
    std::string key;    // normalised - see Normalize()
    std::string owner;
    std::string reason;
    int64_t granted_ms = 0; // steady clock, not wall clock: only differences are ever used
    int64_t expires_ms = 0;
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
    */
    bool Claim(const std::string &owner, const std::vector<std::string> &keys,
               const std::string &reason, int ttl_seconds,
               std::vector<std::string> *granted_out, std::vector<Conflict> *conflicts_out);

    // Releases the named keys held by this owner, or every key it holds when `all` is set.
    // Keys held by somebody else are ignored rather than reported: release is how an agent
    // tidies up, and failing it because of a key it never held helps nobody.
    std::vector<std::string> Release(const std::string &owner,
                                     const std::vector<std::string> &keys, bool all);

    // Extends every lease this owner holds. Returns how many.
    int Refresh(const std::string &owner, int ttl_seconds);

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

    static int64_t NowMs();

private:
    // Unlocked internals - every caller below already holds m_lock.
    int ReapExpiredLocked(std::vector<Lease> *reaped_out);
    void TouchLocked(const std::string &owner, int ttl_seconds);

    // Does a claim on `a` collide with a lease on `b`? Equal keys collide, and a directory
    // claim collides with everything beneath it in either direction.
    static bool Overlaps(const std::string &a, const std::string &b);

    static int ClampTTL(int ttl_seconds);

    std::mutex m_lock;
    std::vector<Lease> m_leases;
    std::string m_root; // normalised, lowercased, trailing '/', or empty
};

#endif
