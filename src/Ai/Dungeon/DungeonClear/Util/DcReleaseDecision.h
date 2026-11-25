/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCRELEASEDECISION_H
#define _PLAYERBOT_DCRELEASEDECISION_H

#include <cstdint>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Util/DcRezDecision.h"

// Pure decision kernel for "release and walk back": a bot nobody raised releases its
// spirit the way a client does, is revived at the instance's checkpoint (whatever
// module answers the release; mod-dungeon-checkpoint here) and walks back to the
// party, which waits for it. It is the fallback UNDER the party resurrection of
// DcRezDecision: the body waits ReleaseAfterSecs of out-of-combat time for a rez first,
// a rez being cast on ANY body finishes (bounded), and one that lands gives the others
// their full wait again. A rezzer still drinking after that loses the body to it.
//
// Every wait this adds to a run is bounded:
//   * the body's clock only freezes while the party fights or the instance refuses
//     resurrection (the rez clock's own freezes);
//   * past ReleaseAfterSecs + kOverdueMs a body that still has not released (a
//     resurrection ban, an AI that does not tick) is no longer waited for;
//   * the walk has its own limit, frozen by a fight only;
//   * ReleaseMaxPerBot caps the releases of one bot in one run.
// A body past its bounds gets DcRezDecision's ordinary verdict back. A walk past its
// limit does not: that bot is alive and out of reach, and the run ends on it by name.
//
// Engine-free and header-only, like DcRezDecision. The clocks live on the run owner's
// DcRunState::release and are advanced by DcReleaseRecovery.

namespace DcReleaseDecision
{
    // DungeonClear.ReleaseWalkBack.
    enum class Mode : std::uint32_t
    {
        Off      = 0,
        TestRuns = 1,  // `.dc test` runs only (default)
        AllRuns  = 2   // every bot run
    };

    // Never on a raid map: raid recovery is parallel and has a budget of its own.
    inline bool Applies(std::uint32_t mode, bool isTestRun, bool isRaid)
    {
        if (isRaid)
            return false;
        if (mode == static_cast<std::uint32_t>(Mode::TestRuns))
            return isTestRun;
        return mode == static_cast<std::uint32_t>(Mode::AllRuns);
    }

    // Where a release lands: one respawn spot of the map's first checkpoint. reach 0 =
    // always usable, > 0 = only once a living member has come that near it.
    struct Spot
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float reach = 0.0f;
    };

    struct Point
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    inline float DistSq(Spot const& s, Point const& p)
    {
        float const dx = s.x - p.x, dy = s.y - p.y, dz = s.z - p.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // A run's OWN spot: the one it started on (a reached-only one is then reached).
    inline constexpr float kOwnSpotYd = 30.0f;
    inline bool IsOwnSpot(Spot const& s, Point const& start)
    {
        float const r = s.reach > 0.0f ? s.reach : kOwnSpotYd;
        return DistSq(s, start) <= r * r;
    }

    inline bool HasOwnSpot(std::vector<Spot> const& spots, Point const& start)
    {
        for (Spot const& s : spots)
            if (IsOwnSpot(s, start))
                return true;
        return false;
    }

    // Is the spot a death at `death` revives at (the nearest usable one) provably on the
    // run's own ground? A reached spot is, because the run has been there, and so is a
    // boss's, because the run killed it. That leaves the always usable spots of other
    // wings: none of them may be nearer than the run's own. Refuses when unsure.
    inline bool RevivesInRegion(std::vector<Spot> const& spots, Point const& start, Point const& death)
    {
        float own = -1.0f, foreign = -1.0f;
        for (Spot const& s : spots)
        {
            float const d = DistSq(s, death);
            if (IsOwnSpot(s, start))
                own = own < 0.0f || d < own ? d : own;
            else if (s.reach <= 0.0f)
                foreign = foreign < 0.0f || d < foreign ? d : foreign;
        }
        return own >= 0.0f && (foreign < 0.0f || own <= foreign);
    }

    struct Rules
    {
        std::uint32_t releaseAfterMs = 30000;  // ReleaseAfterSecs * 1000
        std::uint32_t walkLimitMs = 300000;    // ReleaseWalkLimitSecs * 1000
        std::uint32_t maxReleases = 2;         // ReleaseMaxPerBot
        std::uint32_t quietGraceMs = 12000;    // the NoRezzer floor's quiet grace
    };

    // How long past its due time a body may wait for a rez under way on it (the
    // longest party resurrect is a 10s cast, plus the rezzer's next tick to accept
    // the offer), and how long it may be overdue before the run stops waiting.
    inline constexpr std::uint32_t kRezHoldMaxMs = 20000;
    inline constexpr std::uint32_t kOverdueMs = 60000;

    // One dead group member. Clocks are accrued milliseconds, not stamps.
    struct Body
    {
        bool          isBot = false;        // only bots release, never a human's character (or self-bot)
        bool          isGhost = false;      // already released
        bool          canRelease = true;    // in the run's instance and region, no resurrection ban
        bool          rezUnderway = false;  // a resurrect is being cast on any body, or offered
        std::uint32_t deadMs = 0;           // dead with the clock running
        std::uint32_t releases = 0;         // releases already spent this run
    };

    // Will this body release by itself, so the run may hold for it?
    inline bool WillRelease(Rules const& r, Body const& b)
    {
        return b.isBot && !b.isGhost && b.canRelease && b.releases < r.maxReleases &&
               b.deadMs < r.releaseAfterMs + kOverdueMs;
    }

    struct Scene
    {
        bool anyAlive = false;      // somebody to walk back to; false = full wipe, ends the run as ever
        bool partyEngaged = false;  // a fight (engagement, not the combat flag; see DcRezDecision.h)
        bool rezBlocked = false;    // the instance reports an encounter in progress
        bool anyFlagged = false;    // a living member still carries the combat flag
        std::uint32_t quietMs = 0;  // how long nobody has been engaged
    };

    // Release now?
    inline bool ReleaseNow(Rules const& r, Scene const& s, Body const& b)
    {
        if (!s.anyAlive || s.partyEngaged || s.rezBlocked || !WillRelease(r, b))
            return false;
        if (b.deadMs < r.releaseAfterMs)
            return false;
        // "Not engaged" is also the gap between two swings, or a boss under the floor:
        // while somebody is flagged the quiet has to last.
        if (s.anyFlagged && s.quietMs < r.quietGraceMs)
            return false;
        return !b.rezUnderway || b.deadMs >= r.releaseAfterMs + kRezHoldMaxMs;
    }

    // Advance one clock. A frozen tick adds nothing; a gap (a pause, a tick nobody
    // evaluated) adds at most one second, so time nobody was counting is not charged.
    inline std::uint32_t Accrue(std::uint32_t clockMs, std::uint32_t elapsedMs, bool frozen)
    {
        constexpr std::uint32_t maxStepMs = 1000;
        if (frozen)
            return clockMs;
        return clockMs + (elapsedMs < maxStepMs ? elapsedMs : maxStepMs);
    }

    enum class Walk
    {
        Walking,
        Arrived,   // inside the spread gate, or nobody left to walk to
        Died,      // fell on the way; the body's own clock starts over
        TimedOut   // over the walk limit; the run ends
    };

    struct Walker
    {
        bool          alive = true;
        bool          hasRally = true;  // somebody to walk to
        float         dist = 0.0f;      // 3D distance to it
        float         spread = 0.0f;    // the spread gate's radius
        std::uint32_t walkMs = 0;
    };

    inline Walk DecideWalk(Rules const& r, Walker const& w)
    {
        if (!w.alive)
            return Walk::Died;
        if (!w.hasRally || w.dist <= w.spread)
            return Walk::Arrived;
        if (r.walkLimitMs != 0 && w.walkMs >= r.walkLimitMs)
            return Walk::TimedOut;
        return Walk::Walking;
    }

    // Folds the walk into the rez verdict: the end of the run once a release failed or a
    // walk timed out, a hold while somebody walks and nobody is down. A wipe stands.
    inline void FoldWalk(DcRezDecision::Result& verdict, bool failed, bool timedOut, bool walking)
    {
        using DcRezDecision::Outcome;
        using DcRezDecision::Reason;
        if (verdict.reason == Reason::Wipe)
            return;
        if (failed || timedOut)
        {
            verdict.outcome = Outcome::Disable;
            verdict.reason = failed ? Reason::ReleaseFailed : Reason::WalkTimedOut;
        }
        else if (walking && verdict.reason == Reason::NoDeaths)
        {
            verdict.outcome = Outcome::Hold;
            verdict.reason = Reason::WalkingBack;
        }
    }

    // Why nobody raised the body, for the log and the record.
    inline char const* WhyNoRez(bool livingRezzer, bool corpseUnsafe, bool rezUnderway)
    {
        if (!livingRezzer)
            return "no living member can resurrect";
        if (corpseUnsafe)
            return "the body lies among enemies";
        return rezUnderway ? "the resurrection never landed" : "not raised in time";
    }
}

#endif  // _PLAYERBOT_DCRELEASEDECISION_H
