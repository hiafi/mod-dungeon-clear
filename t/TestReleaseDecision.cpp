/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cstring>

#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"
#include "DcReleaseDecision.h"

using namespace DcReleaseDecision;

namespace
{
    // A bot that lay exactly its 30 seconds, first release, party standing idle.
    Body DueBody()
    {
        Body b;
        b.isBot = true;
        b.deadMs = 30000;
        return b;
    }

    Scene IdleScene()
    {
        Scene s;
        s.anyAlive = true;
        return s;
    }

    Walker WalkerAt(float dist, std::uint32_t walkMs)
    {
        Walker w;
        w.dist = dist;
        w.spread = 25.0f;
        w.walkMs = walkMs;
        return w;
    }
}

// ---- where it applies -----------------------------------------------------------

TEST(DcReleaseDecisionTest, ModeGatesTheRun)
{
    EXPECT_FALSE(Applies(0, true, false));
    EXPECT_FALSE(Applies(0, false, false));
    EXPECT_TRUE(Applies(1, true, false));
    EXPECT_FALSE(Applies(1, false, false));
    EXPECT_TRUE(Applies(2, true, false));
    EXPECT_TRUE(Applies(2, false, false));
    EXPECT_FALSE(Applies(3, true, false));  // an unknown mode is off
}

// Review M3: raids have their own recovery; no mode reaches them.
TEST(DcReleaseDecisionTest, NeverInARaid)
{
    for (std::uint32_t mode = 0; mode < 4; ++mode)
    {
        EXPECT_FALSE(Applies(mode, true, /*isRaid*/ true)) << mode;
        EXPECT_FALSE(Applies(mode, false, /*isRaid*/ true)) << mode;
    }
}

TEST(DcReleaseDecisionTest, RegistryDefaultsMatchTheRuling)
{
    // 30s dead, 300s walk, two releases, test runs only; and the kernel's own
    // defaults agree with the conf's.
    Rules const r;
    unsigned seen = 0;
    for (DcSettingDef const& d : kDcSettings)
    {
        double want = -1;
        if (!std::strcmp(d.key, "ReleaseWalkBack"))
            want = 1;
        else if (!std::strcmp(d.key, "ReleaseAfterSecs"))
            want = r.releaseAfterMs / 1000;
        else if (!std::strcmp(d.key, "ReleaseWalkLimitSecs"))
            want = r.walkLimitMs / 1000;
        else if (!std::strcmp(d.key, "ReleaseMaxPerBot"))
            want = r.maxReleases;
        else
            continue;
        ++seen;
        EXPECT_EQ(d.defVal, want) << d.key;
        EXPECT_FALSE(d.playerFacing) << d.key;
    }
    EXPECT_EQ(seen, 4u);
    EXPECT_EQ(r.walkLimitMs, 300000u);
}

// ---- the checkpoint has to lie on the run's own ground (review M2) ----------------

namespace
{
    // The rows of dungeon_checkpoint_location (order 0) at the time of writing.
    std::vector<Spot> const Maraudon{{1019.69f, -458.31f, -43.43f, 0.0f}};
    std::vector<Spot> const Karazhan{{-11100.0f, -2003.98f, 49.8927f, 0.0f}};
    std::vector<Spot> const Ragefire{{3.81f, -14.82f, -17.84f, 0.0f}, {-154.8f, 70.2f, -21.1f, 10.0f}};
    std::vector<Spot> const Spire{{78.5083f, -225.044f, 49.839f, 0.0f}, {98.25f, -300.75f, 63.1f, 25.0f}};
    std::vector<Spot> const Depths{{456.929f, 34.0923f, -68.0896f, 0.0f}, {544.0f, -181.3f, -36.0f, 12.0f},
                                   {607.2f, -47.8f, -59.8f, 25.0f}, {843.8f, -167.8f, -49.7f, 25.0f}};
    std::vector<Spot> const Monastery{{1688.99f, 1053.48f, 18.6775f, 0.0f}, {255.35f, -209.09f, 18.68f, 0.0f},
                                      {1610.83f, -323.43f, 18.67f, 0.0f}, {855.68f, 1321.50f, 18.67f, 0.0f},
                                      {204.0f, -430.0f, 19.3f, 15.0f}};
}

TEST(DcReleaseDecisionTest, AWingThatStartsElsewhereRefuses)
{
    EXPECT_TRUE(HasOwnSpot(Maraudon, {1019.69f, -458.31f, -43.43f}));   // maraudon, mara-orange
    EXPECT_FALSE(HasOwnSpot(Maraudon, {752.91f, -616.53f, -33.11f}));   // mara-purple
    EXPECT_FALSE(HasOwnSpot(Maraudon, {495.70f, 17.34f, -96.31f}));     // mara-pw
    EXPECT_FALSE(RevivesInRegion(Maraudon, {752.91f, -616.53f, -33.11f}, {760.0f, -600.0f, -33.0f}));
}

TEST(DcReleaseDecisionTest, AScenarioInsideTheMapRefuses)
{
    EXPECT_FALSE(HasOwnSpot(Karazhan, {-11075.0f, -1900.0f, 220.7f}));
}

TEST(DcReleaseDecisionTest, AnEntranceRunReleasesAtItsReachedSpotsToo)
{
    // Ragefire's return spot cannot be used before the run has been there.
    Point const start{3.81f, -14.82f, -17.84f};
    EXPECT_TRUE(HasOwnSpot(Ragefire, start));
    EXPECT_TRUE(RevivesInRegion(Ragefire, start, {-244.7f, 150.1f, -18.8f}));  // Taragaman
    EXPECT_TRUE(RevivesInRegion(Ragefire, start, {-20.0f, -30.0f, -18.0f}));
}

TEST(DcReleaseDecisionTest, AWingRunOnAReachedSpotIsItsOwn)
{
    Point const ubrs{105.0f, -320.0f, 65.5f};
    EXPECT_TRUE(HasOwnSpot(Spire, ubrs));
    EXPECT_TRUE(RevivesInRegion(Spire, ubrs, {140.0f, -320.0f, 70.0f}));
    Point const brdS2{544.0f, -181.3f, -36.0f};
    EXPECT_TRUE(HasOwnSpot(Depths, brdS2));
    EXPECT_TRUE(RevivesInRegion(Depths, brdS2, {600.0f, -200.0f, -50.0f}));
    // ...but a death nearer to the other section's always usable entrance refuses.
    EXPECT_FALSE(RevivesInRegion(Depths, brdS2, {460.0f, 20.0f, -68.0f}));
}

TEST(DcReleaseDecisionTest, MonasteryWingsKeepApart)
{
    Point const library{255.35f, -209.09f, 18.68f};
    EXPECT_TRUE(RevivesInRegion(Monastery, library, {200.0f, -440.0f, 19.0f}));
    EXPECT_FALSE(RevivesInRegion(Monastery, library, {1620.0f, -330.0f, 18.7f}));
}

TEST(DcReleaseDecisionTest, NoSpotNoRelease)
{
    EXPECT_FALSE(HasOwnSpot({}, {0.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(RevivesInRegion({}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}));
}

// ---- the release ----------------------------------------------------------------

TEST(DcReleaseDecisionTest, ReleasesAtThirtySecondsNotBefore)
{
    Rules const r;
    Body b = DueBody();
    b.deadMs = 29999;
    EXPECT_FALSE(ReleaseNow(r, IdleScene(), b));
    EXPECT_TRUE(WillRelease(r, b));
    b.deadMs = 30000;
    EXPECT_TRUE(ReleaseNow(r, IdleScene(), b));
}

TEST(DcReleaseDecisionTest, NeverAHumansCharacter)
{
    Rules const r;
    Body b = DueBody();
    b.isBot = false;
    EXPECT_FALSE(ReleaseNow(r, IdleScene(), b));
    EXPECT_FALSE(WillRelease(r, b));  // so nothing waits for it either
}

TEST(DcReleaseDecisionTest, NeverInAFightABlockedEncounterOrAWipe)
{
    Rules const r;
    Body const b = DueBody();
    Scene s = IdleScene();
    s.partyEngaged = true;
    EXPECT_FALSE(ReleaseNow(r, s, b));
    s = IdleScene();
    s.rezBlocked = true;
    EXPECT_FALSE(ReleaseNow(r, s, b));
    s = IdleScene();
    s.anyAlive = false;
    EXPECT_FALSE(ReleaseNow(r, s, b));
}

// Review minor 2: the gap between two swings is not the end of a fight.
TEST(DcReleaseDecisionTest, AFlaggedPartyHasToStayQuietFirst)
{
    Rules const r;
    Body const b = DueBody();
    Scene s = IdleScene();
    s.anyFlagged = true;
    s.quietMs = r.quietGraceMs - 1;
    EXPECT_FALSE(ReleaseNow(r, s, b));
    EXPECT_TRUE(WillRelease(r, b));  // the run still waits for it
    s.quietMs = r.quietGraceMs;      // a flag with no fight behind it
    EXPECT_TRUE(ReleaseNow(r, s, b));
    s.anyFlagged = false;
    s.quietMs = 0;
    EXPECT_TRUE(ReleaseNow(r, s, b));
    EXPECT_GT(kOverdueMs, r.quietGraceMs + kRezHoldMaxMs);
}

TEST(DcReleaseDecisionTest, AGhostOrABannedBodyIsNotWaitedFor)
{
    Rules const r;
    Body b = DueBody();
    b.isGhost = true;
    EXPECT_FALSE(WillRelease(r, b));
    b = DueBody();
    b.canRelease = false;
    EXPECT_FALSE(WillRelease(r, b));
    EXPECT_FALSE(ReleaseNow(r, IdleScene(), b));
}

TEST(DcReleaseDecisionTest, ARezUnderwayFinishesFirstButOnlySoLong)
{
    Rules const r;
    Body b = DueBody();
    b.rezUnderway = true;
    EXPECT_FALSE(ReleaseNow(r, IdleScene(), b));
    EXPECT_TRUE(WillRelease(r, b));
    b.deadMs = 30000 + kRezHoldMaxMs - 1;
    EXPECT_FALSE(ReleaseNow(r, IdleScene(), b));
    b.deadMs = 30000 + kRezHoldMaxMs;  // an offer nobody ever accepts
    EXPECT_TRUE(ReleaseNow(r, IdleScene(), b));
    // The cast broke off: due at once.
    b.deadMs = 34000;
    b.rezUnderway = false;
    EXPECT_TRUE(ReleaseNow(r, IdleScene(), b));
}

TEST(DcReleaseDecisionTest, TwoReleasesPerBot)
{
    Rules const r;
    Body b = DueBody();
    b.releases = 1;
    EXPECT_TRUE(ReleaseNow(r, IdleScene(), b));
    b.releases = 2;
    EXPECT_FALSE(ReleaseNow(r, IdleScene(), b));
    EXPECT_FALSE(WillRelease(r, b));  // and the run gets its old verdict back
}

TEST(DcReleaseDecisionTest, AnOverdueBodyStopsHoldingTheRun)
{
    Rules const r;
    Body b = DueBody();
    b.deadMs = 30000 + kOverdueMs - 1;
    EXPECT_TRUE(WillRelease(r, b));
    b.deadMs = 30000 + kOverdueMs;
    EXPECT_FALSE(WillRelease(r, b));
    // The overdue window has to outlast the longest rez hold.
    EXPECT_GT(kOverdueMs, kRezHoldMaxMs);
}

// ---- the clocks -----------------------------------------------------------------

TEST(DcReleaseDecisionTest, AFightFreezesTheClockAndKeepsIt)
{
    std::uint32_t clock = 0;
    for (int i = 0; i < 20; ++i)
        clock = Accrue(clock, 1000, false);
    EXPECT_EQ(clock, 20000u);
    for (int i = 0; i < 300; ++i)
        clock = Accrue(clock, 1000, /*frozen*/ true);
    EXPECT_EQ(clock, 20000u);  // not restarted, not charged
    for (int i = 0; i < 10; ++i)
        clock = Accrue(clock, 1000, false);
    EXPECT_EQ(clock, 30000u);
}

TEST(DcReleaseDecisionTest, AnUnobservedGapIsNotCharged)
{
    EXPECT_EQ(Accrue(5000, 600000, false), 6000u);  // a ten-minute pause
    EXPECT_EQ(Accrue(5000, 0, false), 5000u);       // several evaluations in one tick
}

// ---- the walk -------------------------------------------------------------------

TEST(DcReleaseDecisionTest, WalksUntilTheSpreadGate)
{
    Rules const r;
    EXPECT_EQ(DecideWalk(r, WalkerAt(350.0f, 0)), Walk::Walking);
    EXPECT_EQ(DecideWalk(r, WalkerAt(25.1f, 60000)), Walk::Walking);
    EXPECT_EQ(DecideWalk(r, WalkerAt(25.0f, 60000)), Walk::Arrived);
    // Arriving on the last tick is arriving.
    EXPECT_EQ(DecideWalk(r, WalkerAt(10.0f, 300000)), Walk::Arrived);
}

TEST(DcReleaseDecisionTest, TheWalkHasItsOwnLimit)
{
    Rules const r;
    EXPECT_EQ(DecideWalk(r, WalkerAt(40.0f, 299999)), Walk::Walking);
    EXPECT_EQ(DecideWalk(r, WalkerAt(40.0f, 300000)), Walk::TimedOut);
    // Ragefire's longest way back (1075yd at 7yd/s) with room for a fight.
    EXPECT_GT(r.walkLimitMs, 1075u * 1000u / 7u * 3u / 2u);
}

TEST(DcReleaseDecisionTest, DyingOnTheWayEndsTheWalkNotTheRun)
{
    Rules const r;
    Walker w = WalkerAt(200.0f, 300000);
    w.alive = false;
    EXPECT_EQ(DecideWalk(r, w), Walk::Died);
}

TEST(DcReleaseDecisionTest, NobodyLeftToWalkToIsBeingBack)
{
    // The tank releases, and the last member standing dies meanwhile: the tank is
    // where the others will come to.
    Rules const r;
    Walker w = WalkerAt(900.0f, 1000);
    w.hasRally = false;
    EXPECT_EQ(DecideWalk(r, w), Walk::Arrived);
}

TEST(DcReleaseDecisionTest, TheWalkFoldsIntoTheRezVerdict)
{
    using DcRezDecision::Outcome;
    using DcRezDecision::Reason;
    DcRezDecision::Result r;  // None / NoDeaths
    FoldWalk(r, false, false, false);
    EXPECT_EQ(r.outcome, Outcome::None);
    FoldWalk(r, false, false, true);
    EXPECT_EQ(r.outcome, Outcome::Hold);
    EXPECT_EQ(r.reason, Reason::WalkingBack);

    r = {};
    r.outcome = Outcome::Hold;
    r.reason = Reason::Recovering;  // a body is down as well: the rez verdict leads...
    FoldWalk(r, false, false, true);
    EXPECT_EQ(r.reason, Reason::Recovering);
    FoldWalk(r, false, true, true);  // ...until the walk is over its limit
    EXPECT_EQ(r.outcome, Outcome::Disable);
    EXPECT_EQ(r.reason, Reason::WalkTimedOut);
    FoldWalk(r, true, true, true);
    EXPECT_EQ(r.reason, Reason::ReleaseFailed);

    r = {};
    r.outcome = Outcome::Regroup;
    r.reason = Reason::Wipe;
    FoldWalk(r, true, true, true);
    EXPECT_EQ(r.outcome, Outcome::Regroup);
    EXPECT_EQ(r.reason, Reason::Wipe);
}

TEST(DcReleaseDecisionTest, NamesWhyNobodyRaisedIt)
{
    EXPECT_STREQ(WhyNoRez(false, true, true), "no living member can resurrect");
    EXPECT_STREQ(WhyNoRez(true, true, false), "the body lies among enemies");
    EXPECT_STREQ(WhyNoRez(true, false, true), "the resurrection never landed");
    EXPECT_STREQ(WhyNoRez(true, false, false), "not raised in time");
}
