#include "LockTable.h"

#include <algorithm>
#include <cctype>
#include <chrono>

LockTable::LockTable(const std::string &repo_root) {
    if (repo_root.empty()) {
        return;
    }
    // Run the root through the same pipeline the keys go through, minus the root-stripping
    // step itself, so the prefix comparison in Normalize() is comparing like with like.
    std::string r;
    for (char c : repo_root) {
        char ch = (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
        if (ch == '/' && !r.empty() && r.back() == '/') {
            continue;
        }
        r += ch;
    }
    if (!r.empty() && r.back() != '/') {
        r += '/';
    }
    m_root = r;
}

int64_t LockTable::NowMs() {
    // Steady, not system: the table only ever measures elapsed time, and a wall clock that
    // steps (NTP, DST) would expire or extend live leases for no reason.
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int LockTable::ClampTTL(int ttl_seconds) {
    if (ttl_seconds <= 0) {
        return TTL_DEFAULT_S;
    }
    if (ttl_seconds < TTL_MIN_S) {
        return TTL_MIN_S;
    }
    if (ttl_seconds > TTL_MAX_S) {
        return TTL_MAX_S;
    }
    return ttl_seconds;
}

std::string LockTable::Normalize(const std::string &raw) const {
    std::string s;
    s.reserve(raw.size());
    for (char c : raw) {
        char ch = (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
        // Collapse runs of slashes, but only after the first character: a UNC path's
        // leading "//" is meaningless here anyway since the leading slash goes below.
        if (ch == '/' && !s.empty() && s.back() == '/') {
            continue;
        }
        s += ch;
    }

    // A named resource is not a path and must not be mangled like one.
    if (!s.empty() && s[0] == '#') {
        return s;
    }

    while (s.compare(0, 2, "./") == 0) {
        s.erase(0, 2);
    }
    if (!m_root.empty() && s.size() >= m_root.size() && s.compare(0, m_root.size(), m_root) == 0) {
        s.erase(0, m_root.size());
    }
    while (!s.empty() && s[0] == '/') {
        s.erase(0, 1);
    }
    return s;
}

bool LockTable::Overlaps(const std::string &a, const std::string &b) {
    if (a == b) {
        return true;
    }
    // A directory claim (trailing slash) covers everything below it. Checked both ways
    // round so that claiming apps/tetris/ is refused while somebody holds one file in it,
    // as well as the other way round - a one-directional check would let a folder-wide
    // claim sail straight past the file locks it is about to trample.
    if (!a.empty() && a.back() == '/' && b.compare(0, a.size(), a) == 0) {
        return true;
    }
    if (!b.empty() && b.back() == '/' && a.compare(0, b.size(), b) == 0) {
        return true;
    }
    return false;
}

void LockTable::RecordLostLocked(const Lease &lease, const char *how, int64_t now) {
    // An auto lease running out is the normal end of one - the hook never releases, so
    // every one of them ends this way. Telling the owner would be a message on every
    // session every five minutes, and the one worth reading would drown in them.
    if (lease.f_auto) {
        return;
    }
    LostLease lost;
    lost.key = lease.key;
    lost.owner = lease.owner;
    lost.reason = lease.reason;
    lost.how = how;
    lost.lost_ms = now;
    m_lost.push_back(lost);
}

int LockTable::ReapExpiredLocked(std::vector<Lease> *reaped_out) {
    int64_t now = NowMs();
    int count = 0;
    for (size_t i = 0; i < m_leases.size();) {
        if (m_leases[i].expires_ms <= now) {
            if (reaped_out) {
                reaped_out->push_back(m_leases[i]);
            }
            RecordLostLocked(m_leases[i], "expired", now);
            m_leases.erase(m_leases.begin() + i);
            count++;
        } else {
            i++;
        }
    }
    for (size_t i = 0; i < m_lost.size();) {
        if (now - m_lost[i].lost_ms > (int64_t)LOST_KEEP_S * 1000) {
            m_lost.erase(m_lost.begin() + i);
        } else {
            i++;
        }
    }
    return count;
}

int LockTable::ReapExpired(std::vector<Lease> *reaped_out) {
    std::lock_guard<std::mutex> guard(m_lock);
    return ReapExpiredLocked(reaped_out);
}

// Each lease by its OWN length, never by the length the current call happens to carry - see
// the note at the top of LockTable.h for what the other way round cost.
void LockTable::TouchLocked(const std::string &owner, bool f_include_auto) {
    int64_t now = NowMs();
    for (Lease &lease : m_leases) {
        if (lease.owner == owner && (f_include_auto || !lease.f_auto)) {
            lease.expires_ms = now + (int64_t)lease.ttl_s * 1000;
        }
    }
}

bool LockTable::Claim(const std::string &owner, const std::vector<std::string> &keys,
                      const std::string &reason, int ttl_seconds, bool f_auto,
                      std::vector<std::string> *granted_out, std::vector<Conflict> *conflicts_out) {
    std::lock_guard<std::mutex> guard(m_lock);
    ReapExpiredLocked(nullptr);

    int64_t now = NowMs();
    int ttl = ClampTTL(ttl_seconds);

    // Survey everything first and commit nothing until the whole set is known to be free.
    for (const std::string &key : keys) {
        for (const Lease &lease : m_leases) {
            if (lease.owner == owner || !Overlaps(key, lease.key)) {
                continue;
            }
            if (conflicts_out) {
                Conflict c;
                c.requested = key;
                c.held = lease.key;
                c.owner = lease.owner;
                c.reason = lease.reason;
                c.held_for_s = (now - lease.granted_ms) / 1000;
                c.expires_in_s = (lease.expires_ms - now) / 1000;
                conflicts_out->push_back(c);
            }
        }
    }
    if (conflicts_out && !conflicts_out->empty()) {
        // A refused claim still means this owner is alive and working, so its existing
        // leases are extended rather than left to expire while it waits for someone else.
        TouchLocked(owner, true);
        return false;
    }

    for (const std::string &key : keys) {
        Lease *existing = nullptr;
        for (Lease &lease : m_leases) {
            if (lease.owner == owner && lease.key == key) {
                existing = &lease;
                break;
            }
        }
        if (existing) {
            // An explicit claim restates the lease on its own terms and upgrades an auto
            // one; an auto claim only renews, so it cannot shrink what was asked for.
            if (!f_auto) {
                existing->ttl_s = ttl;
                existing->f_auto = false;
                if (!reason.empty()) {
                    existing->reason = reason;
                }
            }
        } else {
            Lease lease;
            lease.key = key;
            lease.owner = owner;
            lease.reason = reason;
            lease.granted_ms = now;
            lease.ttl_s = ttl;
            lease.f_auto = f_auto;
            m_leases.push_back(lease);
        }
        if (granted_out) {
            granted_out->push_back(key);
        }
    }
    // Everything this owner holds is renewed, not just what it just claimed: an agent
    // part-way through a task is alive, and its older leases are the ones most likely to
    // be nearest expiry. Each by its own length - this is also what sets expires_ms on
    // the leases created above.
    TouchLocked(owner, true);
    return true;
}

std::vector<std::string> LockTable::Release(const std::string &owner,
                                            const std::vector<std::string> &keys, bool all) {
    std::lock_guard<std::mutex> guard(m_lock);
    ReapExpiredLocked(nullptr);

    std::vector<std::string> released;
    for (size_t i = 0; i < m_leases.size();) {
        bool mine = (m_leases[i].owner == owner);
        bool named = all || std::find(keys.begin(), keys.end(), m_leases[i].key) != keys.end();
        if (mine && named) {
            released.push_back(m_leases[i].key);
            m_leases.erase(m_leases.begin() + i);
        } else {
            i++;
        }
    }
    return released;
}

int LockTable::Refresh(const std::string &owner, int ttl_seconds) {
    std::lock_guard<std::mutex> guard(m_lock);
    ReapExpiredLocked(nullptr);

    int count = 0;
    for (Lease &lease : m_leases) {
        if (lease.owner == owner) {
            if (ttl_seconds > 0 && !lease.f_auto) {
                lease.ttl_s = ClampTTL(ttl_seconds);
            }
            count++;
        }
    }
    TouchLocked(owner, true);
    return count;
}

int LockTable::Heartbeat(const std::string &owner) {
    std::lock_guard<std::mutex> guard(m_lock);
    ReapExpiredLocked(nullptr);

    int count = 0;
    for (const Lease &lease : m_leases) {
        if (lease.owner == owner && !lease.f_auto) {
            count++;
        }
    }
    TouchLocked(owner, false);
    return count;
}

std::vector<LostLease> LockTable::TakeLost(const std::string &owner,
                                           std::vector<std::string> *holders_out) {
    std::lock_guard<std::mutex> guard(m_lock);
    ReapExpiredLocked(nullptr);

    std::vector<LostLease> taken;
    for (size_t i = 0; i < m_lost.size();) {
        if (m_lost[i].owner != owner) {
            i++;
            continue;
        }
        if (holders_out) {
            // Who has it now is the half of the news that decides what to do next: free
            // means re-claim and carry on, held means stop and go do something else.
            std::string holder;
            for (const Lease &lease : m_leases) {
                if (Overlaps(m_lost[i].key, lease.key)) {
                    holder = lease.owner;
                    break;
                }
            }
            holders_out->push_back(holder);
        }
        taken.push_back(m_lost[i]);
        m_lost.erase(m_lost.begin() + i);
    }
    return taken;
}

std::vector<Lease> LockTable::List() {
    std::lock_guard<std::mutex> guard(m_lock);
    ReapExpiredLocked(nullptr);
    return m_leases;
}

std::vector<std::string> LockTable::Break(const std::vector<std::string> &keys,
                                          const std::string &owner) {
    std::lock_guard<std::mutex> guard(m_lock);
    ReapExpiredLocked(nullptr);

    int64_t now = NowMs();
    std::vector<std::string> broken;
    for (size_t i = 0; i < m_leases.size();) {
        bool by_key = std::find(keys.begin(), keys.end(), m_leases[i].key) != keys.end();
        bool by_owner = !owner.empty() && m_leases[i].owner == owner;
        if (by_key || by_owner) {
            // The owner of a broken lease is by definition still thinking it holds it -
            // that is what breaking is for - so it is the case most in need of telling.
            RecordLostLocked(m_leases[i], "broken", now);
            broken.push_back(m_leases[i].key);
            m_leases.erase(m_leases.begin() + i);
        } else {
            i++;
        }
    }
    return broken;
}
