/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBossOrdering.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunWing.h"
#include "TestRun/DcTestDungeonRegistry.h"

// The run wing (Blackrock Spire LBRS / UBRS, Blackrock Depths' four Finder
// sections): the wing layout data, the resolver's order and latch rules, the
// fallback at the shared portal, the wing filter and the terminal-boss rule. All
// pure — no world, no bots.

namespace
{
    constexpr uint32 BRS = 229;

    DungeonBossInfo Boss(uint32 entry, uint32 bit, float x, float y, float z, uint32 mapId = BRS)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = bit;
        b.name = std::to_string(entry);
        b.mapId = mapId;
        b.x = x;
        b.y = y;
        b.z = z;
        return b;
    }

    // What BossSpawnIndex yields for 229: the eleven encounters with a static
    // spawn (Urok, Gizrul and Solakar are summoned), in DBC order.
    std::vector<DungeonBossInfo> BrsRoster()
    {
        return {
            Boss(9196,  0,  -22.8f, -300.7f,  31.8f),   // Omokk
            Boss(9236,  1, -121.2f, -482.2f,  24.7f),   // Vosh'gajin
            Boss(9237,  2,  -17.0f, -459.1f, -18.6f),   // Voone
            Boss(10596, 3, -135.5f, -565.8f,  10.2f),   // Smolderweb
            Boss(9736,  5, -190.5f, -475.6f,  87.4f),   // Zigris
            Boss(10220, 7, -193.9f, -338.1f,  64.5f),   // Halycon
            Boss(9568,  8,  -22.6f, -486.2f,  90.8f),   // Wyrmthalak
            Boss(9816,  9,  144.4f, -258.0f,  96.4f),   // Emberseer
            Boss(10429, 11, 159.3f, -443.6f, 122.1f),   // Rend
            Boss(10430, 12, 124.2f, -563.8f, 107.4f),   // The Beast
            Boss(10363, 13,  36.5f, -286.0f, 111.0f),   // Drakkisath
        };
    }

    DungeonWingLayout const& Brs()
    {
        DungeonWingLayout const* layout = DungeonWingRegistry::Get(BRS);
        EXPECT_NE(layout, nullptr);
        return *layout;
    }
}

// ---- layout data -------------------------------------------------------------

TEST(DcRunWingTest, BlackrockSpireIsAnExplicitTwoWingLayout)
{
    DungeonWingLayout const& layout = Brs();
    EXPECT_TRUE(layout.isolated);
    EXPECT_EQ(layout.select, WingSelect::Explicit);
    EXPECT_EQ(layout.defaultWing, "lbrs");
    ASSERT_EQ(layout.wings.size(), 2u);

    DungeonWing const* lbrs = DungeonWingRegistry::FindWing(layout, "lbrs");
    DungeonWing const* ubrs = DungeonWingRegistry::FindWing(layout, "ubrs");
    ASSERT_NE(lbrs, nullptr);
    ASSERT_NE(ubrs, nullptr);
    EXPECT_EQ(lbrs->lfgDungeonId, 32u);
    EXPECT_EQ(ubrs->lfgDungeonId, 44u);
    EXPECT_EQ(lbrs->terminalBossEntry, 9568u);    // Overlord Wyrmthalak
    EXPECT_EQ(ubrs->terminalBossEntry, 10363u);   // General Drakkisath
    EXPECT_EQ(lbrs->bossEntries.size(), 9u);      // incl. Urok, Gizrul
    EXPECT_EQ(ubrs->bossEntries.size(), 9u);      // incl. Solakar, + the door, hall, altar and stadium objectives

    // The DBC bits split cleanly: 0-8 LBRS, 9-13 UBRS.
    EXPECT_EQ(lbrs->encounterMask & ubrs->encounterMask, 0u);
    EXPECT_EQ(lbrs->encounterMask | ubrs->encounterMask, 0x3FFFu);
    EXPECT_TRUE(lbrs->encounterMask & (1u << 8));
    EXPECT_TRUE(ubrs->encounterMask & (1u << 9));
}

TEST(DcRunWingTest, EveryWingEntryIsInExactlyOneWing)
{
    DungeonWingLayout const& layout = Brs();
    for (DungeonWing const& wing : layout.wings)
        for (uint32 entry : wing.bossEntries)
        {
            DungeonWing const* owner = DungeonWingRegistry::WingOf(BRS, entry);
            ASSERT_NE(owner, nullptr) << entry;
            EXPECT_EQ(owner->token, wing.token) << entry;
        }
}

TEST(DcRunWingTest, FindWingMatchesTokenOrNameIgnoringCase)
{
    DungeonWingLayout const& layout = Brs();
    ASSERT_NE(DungeonWingRegistry::FindWing(layout, "UBRS"), nullptr);
    EXPECT_EQ(DungeonWingRegistry::FindWing(layout, "UBRS")->token, "ubrs");
    ASSERT_NE(DungeonWingRegistry::FindWing(layout, "blackrock spire (lower)"), nullptr);
    EXPECT_EQ(DungeonWingRegistry::FindWing(layout, "blackrock spire (lower)")->token, "lbrs");
    EXPECT_EQ(DungeonWingRegistry::FindWing(layout, "brs"), nullptr);
    EXPECT_EQ(DungeonWingRegistry::FindWing(layout, ""), nullptr);
}

TEST(DcRunWingTest, LfgDungeonPicksTheWing)
{
    ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(BRS, 32), nullptr);
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRS, 32)->token, "lbrs");
    ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(BRS, 44), nullptr);
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRS, 44)->token, "ubrs");
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRS, 0), nullptr);
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRS, 99), nullptr);
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(36, 32), nullptr);   // not a split map
}

TEST(DcRunWingTest, DireMaulAndScarletMonasteryStayProximity)
{
    for (uint32 map : { 429u, 189u })
    {
        DungeonWingLayout const* layout = DungeonWingRegistry::Get(map);
        ASSERT_NE(layout, nullptr) << map;
        EXPECT_TRUE(layout->isolated) << map;
        EXPECT_EQ(layout->select, WingSelect::Proximity) << map;
    }
}

TEST(DcRunWingTest, MaraudonLfgWingsAndWholeDungeonDefault)
{
    // The dungeon finder queues Maraudon's three wings separately; a Pristine
    // Waters group lands in the inner waters, so its run must start at Landslide
    // rather than walk back out to Noxxion (bit 0).
    constexpr uint32 MARA = 349;
    DungeonWingLayout const* layout = DungeonWingRegistry::Get(MARA);
    ASSERT_NE(layout, nullptr);
    EXPECT_TRUE(layout->isolated);
    EXPECT_EQ(layout->select, WingSelect::Explicit);

    ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(MARA, 26), nullptr);
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(MARA, 26)->token, "mara-orange");
    ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(MARA, 272), nullptr);
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(MARA, 272)->token, "mara-purple");
    DungeonWing const* pw = DungeonWingRegistry::WingForLfgDungeon(MARA, 273);
    ASSERT_NE(pw, nullptr);
    EXPECT_EQ(pw->token, "mara-pw");

    // The roster after the Rotgrip patch, in DBC-bit order.
    std::vector<DungeonBossInfo> const roster = {
        Boss(13282, 0, 0, 0, 0, MARA), Boss(12258, 1, 0, 0, 0, MARA),
        Boss(12236, 2, 0, 0, 0, MARA), Boss(12225, 3, 0, 0, 0, MARA),
        Boss(12203, 4, 0, 0, 0, MARA), Boss(13601, 5, 0, 0, 0, MARA),
        Boss(12201, 7, 0, 0, 0, MARA),
    };
    std::vector<DungeonBossInfo> const pwList = DcRunWing::FilterToWing(roster, *pw);
    ASSERT_EQ(pwList.size(), 3u);
    EXPECT_EQ(pwList[0].entry, 12203u);   // Landslide first
    EXPECT_EQ(pwList[1].entry, 13601u);   // Tinkerer Gizlock
    EXPECT_EQ(pwList[2].entry, 12201u);   // Princess Theradras

    // No choice made: the default wing is the whole dungeon, every boss kept.
    EXPECT_EQ(DcRunWing::ResolveFallback(*layout, nullptr), "maraudon");
    DungeonWing const* whole = DungeonWingRegistry::FindWing(*layout, "maraudon");
    ASSERT_NE(whole, nullptr);
    EXPECT_EQ(DcRunWing::FilterToWing(roster, *whole).size(), roster.size());

    // Status labels still name each boss's region, not the whole-dungeon wing.
    EXPECT_EQ(DungeonWingRegistry::WingOf(MARA, 12203)->token, "mara-pw");
    EXPECT_EQ(DungeonWingRegistry::WingOf(MARA, 13282)->token, "mara-orange");
    EXPECT_EQ(DungeonWingRegistry::WingOf(MARA, 12225)->token, "mara-purple");
}

TEST(DcRunWingTest, EveryIsolatedWingTokenHasACatalogueRowOnItsMap)
{
    // Rez recovery regroups a wiped party at the row whose token is the run's
    // wing token, and the test harness picks the run wing by the row's token —
    // so the two vocabularies must be one.
    for (uint32 map : { 229u, 230u, 349u, 429u, 189u })
    {
        DungeonWingLayout const* layout = DungeonWingRegistry::Get(map);
        ASSERT_NE(layout, nullptr);
        for (DungeonWing const& wing : layout->wings)
        {
            ASSERT_FALSE(wing.token.empty()) << wing.name;
            DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(wing.token);
            ASSERT_NE(row, nullptr) << wing.token;
            EXPECT_EQ(row->mapId, map) << wing.token;
        }
    }
}

// ---- resolver order + latch ------------------------------------------------------

TEST(DcRunWingTest, HigherSourcesWinAndLowerOnesNeverFlipALatch)
{
    using DcRunWing::Latch;
    using DcRunWing::ShouldReplace;
    using DcRunWing::Source;

    Latch empty;
    EXPECT_FALSE(DcRunWing::Holds(empty, 7));
    EXPECT_TRUE(ShouldReplace(empty, 7, Source::Fallback));
    EXPECT_FALSE(ShouldReplace(empty, 7, Source::None));

    Latch const fallback{7, "lbrs", Source::Fallback};
    EXPECT_TRUE(DcRunWing::Holds(fallback, 7));
    EXPECT_TRUE(ShouldReplace(fallback, 7, Source::Fallback));   // same source may refresh
    EXPECT_TRUE(ShouldReplace(fallback, 7, Source::Lfg));
    EXPECT_TRUE(ShouldReplace(fallback, 7, Source::Explicit));

    Latch const lfg{7, "ubrs", Source::Lfg};
    EXPECT_FALSE(ShouldReplace(lfg, 7, Source::Fallback));        // walking can't flip it
    EXPECT_TRUE(ShouldReplace(lfg, 7, Source::Explicit));

    Latch const chosen{7, "ubrs", Source::Explicit};
    EXPECT_FALSE(ShouldReplace(chosen, 7, Source::Fallback));
    EXPECT_FALSE(ShouldReplace(chosen, 7, Source::Lfg));
    EXPECT_TRUE(ShouldReplace(chosen, 7, Source::Explicit));      // `.dc wing` changes it

    // A new instance starts clean: the old latch no longer holds.
    EXPECT_FALSE(DcRunWing::Holds(chosen, 8));
    EXPECT_TRUE(ShouldReplace(chosen, 8, Source::Fallback));
}

TEST(DcRunWingTest, FallbackAtThePortalIsLbrs)
{
    // The portal lies in no wing region, so a bare `.dc on` there clears LBRS —
    // though the nearest bosses to it are UBRS's (the whole reason for Explicit).
    char const* region = DcNavPenaltyRegistry::WingRegionAt(BRS, 78.51f, -225.04f, 49.84f);
    EXPECT_EQ(region, nullptr);
    EXPECT_EQ(DcRunWing::ResolveFallback(Brs(), region), "lbrs");

    std::vector<DungeonBossInfo> const roster = BrsRoster();
    std::size_t const nearest = DcRunWing::PickByProximity(Brs(), roster, 78.51f, -225.04f, 49.84f);
    ASSERT_LT(nearest, Brs().wings.size());
    EXPECT_EQ(Brs().wings[nearest].token, "ubrs") << "proximity would pick UBRS at the portal";
}

TEST(DcRunWingTest, FallbackInsideUbrsIsUbrs)
{
    char const* region = DcNavPenaltyRegistry::WingRegionAt(BRS, 144.4f, -258.0f, 96.4f);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(DcRunWing::ResolveFallback(Brs(), region), "ubrs");
    // At the Dragonspine Door (shared hall) it is still the default.
    EXPECT_EQ(DcRunWing::ResolveFallback(
                  Brs(), DcNavPenaltyRegistry::WingRegionAt(BRS, 109.1f, -320.4f, 65.5f)),
              "lbrs");
    // A region tag that names no wing falls back to the default too.
    EXPECT_EQ(DcRunWing::ResolveFallback(Brs(), "bogus"), "lbrs");
}

// ---- the filter ------------------------------------------------------------------

TEST(DcRunWingTest, WingFilterKeepsOnlyTheWingsBosses)
{
    std::vector<DungeonBossInfo> const roster = BrsRoster();
    std::vector<DungeonBossInfo> const lower =
        DcRunWing::FilterToWing(roster, *DungeonWingRegistry::FindWing(Brs(), "lbrs"));
    std::vector<DungeonBossInfo> const upper =
        DcRunWing::FilterToWing(roster, *DungeonWingRegistry::FindWing(Brs(), "ubrs"));
    ASSERT_EQ(lower.size(), 7u);
    ASSERT_EQ(upper.size(), 4u);
    EXPECT_EQ(lower.front().entry, 9196u);   // Omokk, order kept
    EXPECT_EQ(lower.back().entry, 9568u);    // Wyrmthalak last
    EXPECT_EQ(upper.front().entry, 9816u);   // Emberseer next for a UBRS run
    EXPECT_EQ(upper.back().entry, 10363u);

    // An Explicit wing is never widened back to the whole map.
    std::vector<DungeonBossInfo> const onlyUpper(roster.begin() + 7, roster.end());
    EXPECT_TRUE(DcRunWing::FilterToWing(onlyUpper, *DungeonWingRegistry::FindWing(Brs(), "lbrs")).empty());
}

TEST(DcRunWingTest, DireMaulProximityPickIsUnchanged)
{
    DungeonWingLayout const* dm = DungeonWingRegistry::Get(429);
    ASSERT_NE(dm, nullptr);
    std::vector<DungeonBossInfo> const roster = {
        Boss(11490, 0,   60.0f, -200.0f, -2.0f, 429),   // Zevrim (East)
        Boss(11489, 5,  -60.0f,  500.0f, -3.0f, 429),   // Tendris (West)
        Boss(14326, 9,  400.0f,   20.0f, -2.0f, 429),   // Mol'dar (North)
    };
    std::size_t const east = DcRunWing::PickByProximity(*dm, roster, 44.45f, -154.82f, -2.71f);
    ASSERT_LT(east, dm->wings.size());
    EXPECT_EQ(dm->wings[east].token, "dm-east");
    std::size_t const north = DcRunWing::PickByProximity(*dm, roster, 380.0f, 0.0f, -2.0f);
    ASSERT_LT(north, dm->wings.size());
    EXPECT_EQ(dm->wings[north].token, "dm-north");
    // No registered boss in the list -> no pick.
    EXPECT_EQ(DcRunWing::PickByProximity(*dm, { Boss(1, 0, 0, 0, 0, 429) }, 0, 0, 0), dm->wings.size());
}

// ---- terminal boss ---------------------------------------------------------------

TEST(DcRunWingTest, WyrmthalakDownCompletesLbrs)
{
    DungeonWing const& lbrs = *DungeonWingRegistry::FindWing(Brs(), "lbrs");
    std::vector<DungeonBossInfo> const lower = DcRunWing::FilterToWing(BrsRoster(), lbrs);

    EXPECT_FALSE(DcRunWing::TerminalDone(lbrs, lower, 0u, false));
    // Everything but Wyrmthalak down: not complete.
    EXPECT_FALSE(DcRunWing::TerminalDone(lbrs, lower, 0x0FFu, false));
    // His bit alone completes the wing, even with Omokk skipped/unreached.
    EXPECT_TRUE(DcRunWing::TerminalDone(lbrs, lower, 1u << 8, false));
    // A fresh corpse whose bit hasn't flipped yet counts too.
    EXPECT_TRUE(DcRunWing::TerminalDone(lbrs, lower, 0u, true));
    // UBRS bits never complete LBRS.
    EXPECT_FALSE(DcRunWing::TerminalDone(lbrs, lower, 0x3E00u, false));
}

TEST(DcRunWingTest, TerminalRuleNeedsATerminalInTheList)
{
    DungeonWing const& ubrs = *DungeonWingRegistry::FindWing(Brs(), "ubrs");
    std::vector<DungeonBossInfo> const lower =
        DcRunWing::FilterToWing(BrsRoster(), *DungeonWingRegistry::FindWing(Brs(), "lbrs"));
    // Drakkisath is not in an LBRS list -> never "done" from it.
    EXPECT_FALSE(DcRunWing::TerminalDone(ubrs, lower, 1u << 13, true));

    DungeonWing none;
    none.bossEntries = { 9568 };
    EXPECT_FALSE(DcRunWing::TerminalDone(none, lower, 1u << 8, true));
}

// ---- Blackrock Depths: the four Finder sections (map 230) ----------------------

namespace
{
    constexpr uint32 BRD = 230;
    namespace B = DcBlackrockDepths;

    // What BossSpawnIndex yields for 230 on the reworked server: the encounters
    // with a static spawn AND a kill-creature credit row, in DBC order. The
    // Ring of Law's Grimstone is summoned, and the credit rows of Fineous
    // Darkvire (234), Ribbly Screwspigot (240) and Plugger Spazzring (241) are
    // deleted (they give no credit), so none of them is here. `withFineous` /
    // `withBarCredit` is a world database that still has row 234 / rows 240 and
    // 241; `withStilgiss` false is one where the warders' credit (233) is no
    // longer a creature kill.
    std::vector<DungeonBossInfo> BrdSpawnIndex(bool withFineous = false, bool withStilgiss = true,
                                               bool withBarCredit = false)
    {
        std::vector<DungeonBossInfo> v = {
            Boss(9018,  0,  310.6f, -146.3f, -70.3f, BRD),   // Gerstahn
            Boss(9025,  1,  615.5f, -267.4f, -83.6f, BRD),   // Roccor
            Boss(9319,  2,  607.7f, -174.8f, -84.5f, BRD),   // Grebmar
            Boss(9024,  4,  530.2f, -243.9f, -43.0f, BRD),   // Loregrain
            Boss(9017,  5,  893.5f, -267.1f, -71.9f, BRD),   // Incendius
        };
        if (withStilgiss)
            v.push_back(Boss(9041, 6, 823.4f, -342.3f, -50.1f, BRD));   // Stilgiss
        if (withFineous)
            v.push_back(Boss(9056, 7, 963.3f, -343.7f, -71.7f, BRD));   // Darkvire
        for (DungeonBossInfo const& b : {
                 Boss(9016,  8,  702.4f,  184.5f, -72.0f, BRD),   // Bael'Gar
                 Boss(9033,  9,  652.4f,   21.4f, -60.0f, BRD),   // Angerforge
                 Boss(8983, 10,  846.8f,   16.3f, -53.6f, BRD),   // Argelmach
                 Boss(9537, 11,  878.1f, -153.1f, -49.8f, BRD),   // Hurley
                 Boss(9502, 12,  869.0f, -225.0f, -43.7f, BRD),   // Phalanx
             })
            v.push_back(b);
        if (withBarCredit)
        {
            v.push_back(Boss(9543, 13, 878.5f, -167.7f, -49.7f, BRD));   // Ribbly
            v.push_back(Boss(9499, 14, 888.5f, -177.9f, -43.0f, BRD));   // Plugger
        }
        for (DungeonBossInfo const& b : {
                 Boss(9156, 15, 1009.8f, -239.0f, -61.3f, BRD),   // Flamelash
                 Boss(9035, 16, 1215.1f, -220.7f, -85.6f, BRD),   // Anger'rel (The Seven)
                 Boss(9938, 17, 1380.7f, -659.3f, -92.0f, BRD),   // Magmus
                 Boss(9019, 18, 1380.2f, -831.6f, -87.6f, BRD),   // Thaurissan
             })
            v.push_back(b);
        return v;
    }

    // The live list: the spawn index through the map-230 roster patch, which adds
    // the objectives (the Ring of Law, the Vault, the Shadowforge Lock, the bar's
    // three gates, the Tomb, the Lyceum's two braziers) and re-adds Stilgiss,
    // Fineous, Ribbly and Plugger on their own keys.
    std::vector<DungeonBossInfo> BrdRoster(bool withFineous = false, bool withStilgiss = true,
                                           bool withBarCredit = false)
    {
        return BossRosterRegistry::Apply(BRD, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL),
                                         BrdSpawnIndex(withFineous, withStilgiss, withBarCredit));
    }

    DungeonWingLayout const& Brd()
    {
        DungeonWingLayout const* layout = DungeonWingRegistry::Get(BRD);
        EXPECT_NE(layout, nullptr);
        return *layout;
    }

    std::vector<DungeonBossInfo> BrdWing(char const* token, std::vector<DungeonBossInfo> const& roster)
    {
        DungeonWing const* wing = DungeonWingRegistry::FindWing(Brd(), token);
        EXPECT_NE(wing, nullptr) << token;
        return wing ? DcRunWing::FilterToWing(roster, *wing) : std::vector<DungeonBossInfo>{};
    }

    constexpr float BRD_PORTAL_X = 456.93f;
    constexpr float BRD_PORTAL_Y = 34.09f;
    constexpr float BRD_PORTAL_Z = -68.09f;
}

TEST(DcRunWingTest, BlackrockDepthsIsAnExplicitSectionLayout)
{
    DungeonWingLayout const& layout = Brd();
    EXPECT_TRUE(layout.isolated);
    EXPECT_EQ(layout.select, WingSelect::Explicit);
    EXPECT_EQ(layout.defaultWing, "brd-s1");
    ASSERT_EQ(layout.wings.size(), 4u);

    DungeonWing const* s1 = DungeonWingRegistry::FindWing(layout, "brd-s1");
    DungeonWing const* s2 = DungeonWingRegistry::FindWing(layout, "brd-s2");
    DungeonWing const* s3 = DungeonWingRegistry::FindWing(layout, "brd-s3");
    DungeonWing const* s4 = DungeonWingRegistry::FindWing(layout, "brd-s4");
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    ASSERT_NE(s3, nullptr);
    ASSERT_NE(s4, nullptr);
    EXPECT_EQ(DungeonWingRegistry::FindWing(layout, "brd-db"), nullptr) << "the stock wing is retired";
    EXPECT_EQ(DungeonWingRegistry::FindWing(layout, "brd-uc"), nullptr) << "the stock Upper City is retired";

    // Section 1 ends at the Ring of Law: an objective, so no terminal boss (the
    // terminal rule reads Boss anchors only) — the wing ends when its list is
    // cleared, and the Ring is last in it. Section 2 ends at Bael'Gar.
    EXPECT_EQ(s1->terminalBossEntry, 0u);
    EXPECT_EQ(s2->terminalBossEntry, 9016u);   // Bael'Gar
    EXPECT_EQ(s3->terminalBossEntry, 8983u);   // Golem Lord Argelmach
    EXPECT_EQ(s4->terminalBossEntry, 9019u);   // Emperor Dagran Thaurissan

    // The DBC bits: section 1 is 0-3 (the Ring is bit 3); section 2 is 4, 5, 6
    // and 8 — NOT Fineous's 7, which no longer sets (no credit row).
    EXPECT_EQ(s1->encounterMask, 0x0000Fu);
    EXPECT_EQ(s2->encounterMask, 0x00170u);
    EXPECT_FALSE(s2->encounterMask & (1u << 7)) << "Fineous gives no credit";
    EXPECT_TRUE(s1->encounterMask & (1u << 3));    // the Ring
    EXPECT_TRUE(s2->encounterMask & (1u << 8));    // Bael'Gar
    // Section 3 is 9-10; section 4 is 11, 12 and 15-18 — NOT Ribbly's 13 or
    // Plugger's 14, which no longer set either.
    EXPECT_EQ(s3->encounterMask, 0x00600u);
    EXPECT_EQ(s4->encounterMask, 0x79800u);
    EXPECT_FALSE(s4->encounterMask & (1u << 13)) << "Ribbly gives no credit";
    EXPECT_FALSE(s4->encounterMask & (1u << 14)) << "Plugger gives no credit";
    EXPECT_TRUE(s3->encounterMask & (1u << 10));   // Argelmach
    EXPECT_TRUE(s4->encounterMask & (1u << 18));   // the Emperor
    uint32 seen = 0;
    for (DungeonWing const* w : { s1, s2, s3, s4 })
    {
        EXPECT_EQ(seen & w->encounterMask, 0u) << w->token << " shares a bit";
        seen |= w->encounterMask;
    }
    EXPECT_EQ(seen, 0x7FFFFu & ~((1u << 7) | (1u << 13) | (1u << 14)));

    // Every wing entry has one owner.
    for (DungeonWing const& wing : layout.wings)
        for (uint32 entry : wing.bossEntries)
        {
            DungeonWing const* owner = DungeonWingRegistry::WingOf(BRD, entry);
            ASSERT_NE(owner, nullptr) << entry;
            EXPECT_EQ(owner->token, wing.token) << entry;
        }

    // The panel tags a row with the part in parentheses.
    EXPECT_EQ(DungeonWingRegistry::WingName(BRD, 9033), "Blackrock Depths (Manufactory)");
    EXPECT_EQ(DungeonWingRegistry::WingName(BRD, 9019), "Blackrock Depths (Imperial Seat)");
    EXPECT_EQ(DungeonWingRegistry::WingName(BRD, 9018), "Blackrock Depths (Detention Block)");
    EXPECT_EQ(DungeonWingRegistry::WingName(BRD, 9016), "Blackrock Depths (Shadowforge City)");
    ASSERT_NE(DungeonWingRegistry::FindWing(layout, "blackrock depths (shadowforge city)"), nullptr);
}

TEST(DcRunWingTest, BlackrockDepthsLfgDungeonPicksTheSection)
{
    // Normal and heroic Finder entries of the same section pick the same wing.
    for (uint32 lfg : { 30u, 310u })
    {
        ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg), nullptr) << lfg;
        EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg)->token, "brd-s1") << lfg;
    }
    for (uint32 lfg : { 308u, 311u })
    {
        ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg), nullptr) << lfg;
        EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg)->token, "brd-s2") << lfg;
    }
    for (uint32 lfg : { 309u, 312u })
    {
        ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg), nullptr) << lfg;
        EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg)->token, "brd-s3") << lfg;
    }
    // 276 is the stock "Upper City" row, its start moved to S4b.
    for (uint32 lfg : { 276u, 313u })
    {
        ASSERT_NE(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg), nullptr) << lfg;
        EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRD, lfg)->token, "brd-s4") << lfg;
    }
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRD, 32), nullptr);    // LBRS's id
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(BRD, 0), nullptr);
    // A wing with no heroic entry never matches id 0 through its zero field.
    EXPECT_EQ(DungeonWingRegistry::WingForLfgDungeon(229, 0), nullptr);
}

TEST(DcRunWingTest, EveryBlackrockDepthsRosterRowIsInExactlyOneWing)
{
    // The whole live list — bosses AND every objective — splits 4 / 6 / 3 / 14
    // with nothing dropped: a row in no wing would never be cleared by any run.
    // The same with the bar's credit rows (240 / 241) still in the world DB.
    for (bool barCredit : { false, true })
    {
        std::vector<DungeonBossInfo> const roster = BrdRoster(false, true, barCredit);
        ASSERT_EQ(roster.size(), 27u) << barCredit;
        std::size_t const s1 = BrdWing("brd-s1", roster).size();
        std::size_t const s2 = BrdWing("brd-s2", roster).size();
        std::size_t const s3 = BrdWing("brd-s3", roster).size();
        std::size_t const s4 = BrdWing("brd-s4", roster).size();
        EXPECT_EQ(s1, 4u);
        EXPECT_EQ(s2, 6u);
        EXPECT_EQ(s3, 3u);
        EXPECT_EQ(s4, 14u);
        EXPECT_EQ(s1 + s2 + s3 + s4, roster.size());
        for (DungeonBossInfo const& b : roster)
            EXPECT_NE(DungeonWingRegistry::WingOf(BRD, b.entry), nullptr) << b.name;
    }
}

TEST(DcRunWingTest, DetentionBlockRunsGerstahnToTheRingOfLaw)
{
    std::vector<DungeonBossInfo> const s1 = BrdWing("brd-s1", BrdRoster());
    std::vector<uint32> const want = { 9018, 9025, 9319, BossRosterRegistry::ObjectiveEntry(1) };
    ASSERT_EQ(s1.size(), want.size());
    for (std::size_t i = 0; i < want.size(); ++i)
        EXPECT_EQ(s1[i].entry, want[i]) << "slot " << i << " is " << s1[i].name;

    // The Ring is the section's end: last, carrying event 1, and done once the
    // instance reads TYPE_RING_OF_LAW DONE (a re-entered instance whose Ring is
    // saved as won never replays it).
    DungeonBossInfo const& ring = s1.back();
    EXPECT_EQ(ring.kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(ring.eventId, 1u);
    EXPECT_EQ(ring.encounterIndex, 3u);
    EXPECT_EQ(ring.doneInstanceDataId, static_cast<int32>(B::TYPE_RING_OF_LAW));
    EXPECT_EQ(ring.doneInstanceDataValue, B::STATE_DONE);
    EXPECT_EQ(AnchorDoneByInstanceValues(ring, 0, B::STATE_DONE), DcAnchorDoneVia::InstanceData);
    EXPECT_EQ(AnchorDoneByInstanceValues(ring, 0, B::STATE_IN_PROGRESS), DcAnchorDoneVia::None);
    EXPECT_EQ(AnchorDoneByInstanceValues(ring, 0, B::STATE_NOT_STARTED), DcAnchorDoneVia::None);
}

TEST(DcRunWingTest, ShadowforgeCityRunsLoregrainToBaelGarThroughTheVault)
{
    // Loregrain, Stilgiss, the Vault, Fineous, Incendius, Bael'Gar: Stilgiss,
    // the Vault and Fineous before Incendius, not in DBC order.
    auto const check = [](std::vector<DungeonBossInfo> const& s2, char const* what)
    {
        std::vector<uint32> const want = { 9024, 9041, BossRosterRegistry::ObjectiveEntry(B::OBJ_VAULT),
                                           9056, 9017, 9016 };
        ASSERT_EQ(s2.size(), want.size()) << what;
        for (std::size_t i = 0; i < want.size(); ++i)
            EXPECT_EQ(s2[i].entry, want[i]) << what << ": slot " << i << " is " << s2[i].name;

        // The reorder moves the path, never the kill bits.
        EXPECT_EQ(s2[1].encounterIndex, 6u) << what << ": Stilgiss keeps bit 6";
        EXPECT_EQ(s2[1].kind, DungeonAnchorKind::Boss) << what;
        EXPECT_EQ(s2[4].encounterIndex, 5u) << what << ": Incendius keeps bit 5";
        EXPECT_EQ(s2[5].encounterIndex, 8u) << what << ": Bael'Gar keeps bit 8";

        // The Vault: an objective carrying event 9 on no real bit, done on
        // TYPE_VAULT DONE.
        DungeonBossInfo const& vault = s2[2];
        EXPECT_EQ(vault.kind, DungeonAnchorKind::Objective) << what;
        EXPECT_EQ(vault.eventId, B::EVENT_VAULT) << what;
        EXPECT_GE(vault.encounterIndex, 19u) << what << ": the Vault must not sit on a DBC bit";
        EXPECT_EQ(vault.doneInstanceDataId, static_cast<int32>(B::TYPE_VAULT)) << what;
        EXPECT_EQ(vault.doneInstanceDataValue, B::STATE_DONE) << what;

        // Fineous: a boss on the rework's slot 7, with no mask bit to clear him.
        DungeonBossInfo const& fineous = s2[3];
        EXPECT_EQ(fineous.kind, DungeonAnchorKind::Boss) << what;
        EXPECT_EQ(fineous.doneBossStateIndex, B::SLOT_FINEOUS) << what;
        EXPECT_GE(fineous.encounterIndex, 32u) << what << ": no completion bit for Fineous";
        EXPECT_EQ(AnchorDoneByInstanceValues(fineous, B::STATE_DONE, 0), DcAnchorDoneVia::BossState) << what;
        EXPECT_EQ(AnchorDoneByInstanceValues(fineous, B::STATE_IN_PROGRESS, 0), DcAnchorDoneVia::None) << what;

        for (DungeonBossInfo const& b : s2)
        {
            EXPECT_NE(b.eventId, 1u) << what << ": the Ring of Law belongs to section 1";
            EXPECT_NE(b.eventId, 2u) << what << ": the Shadowforge Lock belongs to section 3";
        }
    };

    check(BrdWing("brd-s2", BrdRoster()), "the reworked world");
    // The same route whether the world database still lists Fineous (row 234)
    // or no longer lists Stilgiss (233 no longer a creature kill).
    check(BrdWing("brd-s2", BrdRoster(/*withFineous*/ true)), "row 234 still present");
    check(BrdWing("brd-s2", BrdRoster(/*withFineous*/ false, /*withStilgiss*/ false)), "233 a spell credit");
}

TEST(DcRunWingTest, TheVaultIsTakenBeforeFineousAfterStilgiss)
{
    // The Vault shares Fineous's order key; the picker takes the objective
    // first, and on its completion finishes the same-key sibling (Fineous)
    // before advancing to Incendius.
    std::vector<DungeonBossInfo> const s2 = BrdWing("brd-s2", BrdRoster());
    ASSERT_EQ(s2.size(), 6u);
    DungeonBossInfo const& stilgiss = s2[1];
    DungeonBossInfo const& vault = s2[2];
    DungeonBossInfo const& fineous = s2[3];
    EXPECT_EQ(BossOrderKey(vault), BossOrderKey(fineous));
    EXPECT_LT(BossOrderKey(stilgiss), BossOrderKey(vault));

    // Stilgiss down: the next pick is the Vault.
    std::vector<DungeonBossInfo> cands(s2.begin() + 2, s2.end());
    std::optional<DungeonBossInfo> next =
        DcBossOrdering::PickTarget(cands, 0, BossOrderKey(stilgiss), /*haveStickyIndex*/ true);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->entry, vault.entry);

    // The Vault done (or skipped): Fineous, not Incendius.
    cands.erase(cands.begin());
    next = DcBossOrdering::PickTarget(cands, 0, BossOrderKey(vault), /*haveStickyIndex*/ true);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->entry, 9056u);
}

TEST(DcRunWingTest, ManufactoryRunsTheShadowforgeLockToArgelmach)
{
    // Section 3 lands 8yd from the lever and pulls it itself, then Angerforge,
    // then Argelmach — an ordinary boss row (no mass release in the rework).
    std::vector<DungeonBossInfo> const s3 = BrdWing("brd-s3", BrdRoster());
    ASSERT_EQ(s3.size(), 3u);
    EXPECT_EQ(s3[0].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(s3[0].eventId, 2u) << "a section 3 run opens at the Shadowforge Lock";
    EXPECT_EQ(s3[1].entry, 9033u) << "then General Angerforge";
    EXPECT_EQ(s3[2].entry, 8983u) << "then Golem Lord Argelmach";
    EXPECT_EQ(s3[2].kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(s3[2].encounterIndex, 10u);
    EXPECT_LT(std::hypot(s3[0].x - B::S3_START_X, s3[0].y - B::S3_START_Y), 10.0f)
        << "the lever is the section's first stop, 8yd from its start";
}

// Section 4 in clear order. Each scripted gate is an objective just ahead of the
// boss it unlocks, and the Grim Guzzler is cleared keg -> Hurley -> Ribbly ->
// Phalanx's challenge -> Phalanx -> Plugger: Plugger's death turns the bar's 68
// patrons hostile, so he goes last, after Phalanx's death has opened the Bar
// Door the party leaves by.
TEST(DcRunWingTest, ImperialSeatClearsEachScriptedGateBeforeItsBoss)
{
    auto const ObjectiveEntry = [](uint32 seq) { return BossRosterRegistry::ObjectiveEntry(seq); };
    std::vector<uint32> const want = {
        ObjectiveEntry(B::OBJ_HURLEY_KEGS), 9537,
        ObjectiveEntry(B::OBJ_RIBBLY), 9543,
        ObjectiveEntry(B::OBJ_PHALANX_CHALLENGE), 9502,
        9499,                                                // Plugger, last in the bar
        9156,                                                // Flamelash
        ObjectiveEntry(B::OBJ_TOMB_OF_SEVEN), 9035,          // the Seven, then Anger'rel's row
        ObjectiveEntry(B::OBJ_LYCEUM_SOUTH),                 // the Lyceum, south brazier
        ObjectiveEntry(B::OBJ_LYCEUM_NORTH),                 // then north
        9938, 9019,                                          // Magmus, Thaurissan
    };
    // The same route whether or not the world DB still has the bar's credit rows.
    for (bool barCredit : { false, true })
    {
        std::vector<DungeonBossInfo> const s4 = BrdWing("brd-s4", BrdRoster(false, true, barCredit));
        ASSERT_EQ(s4.size(), want.size()) << barCredit;
        for (std::size_t i = 0; i < want.size(); ++i)
            EXPECT_EQ(s4[i].entry, want[i]) << barCredit << ": slot " << i << " is " << s4[i].name;

        // Each gate carries its event, and the reorder moves the path, never
        // the kill bits.
        for (DungeonBossInfo const& b : s4)
        {
            if (b.entry == ObjectiveEntry(B::OBJ_HURLEY_KEGS))
                EXPECT_EQ(b.eventId, B::EVENT_HURLEY_KEGS);
            if (b.entry == ObjectiveEntry(B::OBJ_RIBBLY))
                EXPECT_EQ(b.eventId, B::EVENT_RIBBLY);
            if (b.entry == ObjectiveEntry(B::OBJ_PHALANX_CHALLENGE))
                EXPECT_EQ(b.eventId, B::EVENT_PHALANX_CHALLENGE);
            if (b.entry == ObjectiveEntry(B::OBJ_TOMB_OF_SEVEN))
                EXPECT_EQ(b.eventId, B::EVENT_TOMB_OF_SEVEN);
            if (b.entry == 9502)
                EXPECT_EQ(b.encounterIndex, 12u) << "Phalanx keeps kill-bit 12";
            if (b.entry == 9537)
                EXPECT_EQ(b.encounterIndex, 11u) << "Hurley keeps kill-bit 11";
        }
    }
}

// Ribbly and Plugger give no credit on the reworked server: each is a boss row on
// the rework's tracking slot, with no mask bit — and NEVER on bit 0, Gerstahn's,
// which an inherit from a missing spawn-index row used to leave Plugger on (the
// picker then read him dead wherever Gerstahn was).
TEST(DcRunWingTest, RibblyAndPluggerAreDoneOnTheirTrackingSlots)
{
    for (bool barCredit : { false, true })
    {
        std::vector<DungeonBossInfo> const s4 = BrdWing("brd-s4", BrdRoster(false, true, barCredit));
        for (auto const& [entry, slot] : { std::pair<uint32, int32>{ 9543u, B::SLOT_RIBBLY },
                                           std::pair<uint32, int32>{ 9499u, B::SLOT_PLUGGER } })
        {
            auto const it = std::find_if(s4.begin(), s4.end(),
                                         [entry = entry](DungeonBossInfo const& b) { return b.entry == entry; });
            ASSERT_NE(it, s4.end()) << entry << " " << barCredit;
            EXPECT_EQ(it->kind, DungeonAnchorKind::Boss) << entry;
            EXPECT_EQ(it->doneBossStateIndex, slot) << entry;
            EXPECT_GE(it->encounterIndex, 32u) << entry << ": no completion bit (bit 0 is Gerstahn's)";
            EXPECT_EQ(AnchorDoneByInstanceValues(*it, B::STATE_DONE, 0), DcAnchorDoneVia::BossState) << entry;
            EXPECT_EQ(AnchorDoneByInstanceValues(*it, B::STATE_NOT_STARTED, 0), DcAnchorDoneVia::None) << entry;
        }
    }
    // Plugger is still fought past the Bar Door, out of the patrons' reach.
    std::vector<DungeonBossInfo> const s4 = BrdWing("brd-s4", BrdRoster());
    auto const plugger = std::find_if(s4.begin(), s4.end(), [](DungeonBossInfo const& b) { return b.entry == 9499; });
    ASSERT_NE(plugger, s4.end());
    EXPECT_FLOAT_EQ(plugger->x, B::PLUGGER_CAMP_X);
    EXPECT_FLOAT_EQ(plugger->y, B::PLUGGER_CAMP_Y);
}

TEST(DcRunWingTest, FallbackAtTheBrdPortalIsTheDetentionBlock)
{
    // BRD has no wing regions, so a bare `.dc on` anywhere clears the Detention
    // Block — though the nearest bosses to the portal are the Manufactory's.
    char const* region = DcNavPenaltyRegistry::WingRegionAt(BRD, BRD_PORTAL_X, BRD_PORTAL_Y, BRD_PORTAL_Z);
    EXPECT_EQ(region, nullptr);
    EXPECT_EQ(DcRunWing::ResolveFallback(Brd(), region), "brd-s1");

    std::size_t const nearest =
        DcRunWing::PickByProximity(Brd(), BrdRoster(), BRD_PORTAL_X, BRD_PORTAL_Y, BRD_PORTAL_Z);
    ASSERT_LT(nearest, Brd().wings.size());
    EXPECT_EQ(Brd().wings[nearest].token, "brd-s3") << "proximity would pick the Manufactory at the portal";
}

TEST(DcRunWingTest, EachSectionEndsOnItsOwnLastBoss)
{
    std::vector<DungeonBossInfo> const roster = BrdRoster();
    DungeonWing const& s1 = *DungeonWingRegistry::FindWing(Brd(), "brd-s1");
    DungeonWing const& s2 = *DungeonWingRegistry::FindWing(Brd(), "brd-s2");
    DungeonWing const& s3 = *DungeonWingRegistry::FindWing(Brd(), "brd-s3");
    DungeonWing const& s4 = *DungeonWingRegistry::FindWing(Brd(), "brd-s4");
    std::vector<DungeonBossInfo> const s1List = DcRunWing::FilterToWing(roster, s1);
    std::vector<DungeonBossInfo> const s2List = DcRunWing::FilterToWing(roster, s2);
    std::vector<DungeonBossInfo> const s3List = DcRunWing::FilterToWing(roster, s3);
    std::vector<DungeonBossInfo> const s4List = DcRunWing::FilterToWing(roster, s4);

    // Section 1 has no terminal boss: the list clearing (the Ring last) ends it.
    EXPECT_FALSE(DcRunWing::TerminalDone(s1, s1List, 0x7FFFFu, true));

    EXPECT_FALSE(DcRunWing::TerminalDone(s2, s2List, 0x0FFu & ~(1u << 8), false));
    EXPECT_TRUE(DcRunWing::TerminalDone(s2, s2List, 1u << 8, false));
    EXPECT_TRUE(DcRunWing::TerminalDone(s2, s2List, 0u, /*terminalCorpse*/ true));
    // Later sections' kills never complete section 2.
    EXPECT_FALSE(DcRunWing::TerminalDone(s2, s2List, 0x7FE00u, false));

    // Section 3 ends on Argelmach (bit 10), not on Angerforge.
    EXPECT_FALSE(DcRunWing::TerminalDone(s3, s3List, 1u << 9, false));
    EXPECT_TRUE(DcRunWing::TerminalDone(s3, s3List, 1u << 10, false));
    EXPECT_TRUE(DcRunWing::TerminalDone(s3, s3List, 0u, /*terminalCorpse*/ true));

    // Section 4 ends on the Emperor (bit 18).
    EXPECT_FALSE(DcRunWing::TerminalDone(s4, s4List, 0x3FFFFu, false));
    EXPECT_TRUE(DcRunWing::TerminalDone(s4, s4List, 1u << 18, false));
    // Argelmach is not in a section 4 list, nor the Emperor in a section 3 one.
    EXPECT_FALSE(DcRunWing::TerminalDone(s4, s3List, 1u << 10, true));
    EXPECT_FALSE(DcRunWing::TerminalDone(s3, s4List, 1u << 18, true));
}
