/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBossStandDown.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBossOrdering.h"

namespace
{
    DungeonBossInfo Boss(uint32 entry, uint32 idx, char const* name, uint32 mapId = 0)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = idx;
        b.name = name;
        b.mapId = mapId;
        return b;
    }

    DungeonBossInfo const* Find(std::vector<DungeonBossInfo> const& v, uint32 entry)
    {
        for (DungeonBossInfo const& b : v)
            if (b.entry == entry)
                return &b;
        return nullptr;
    }
}

// --- HasPatch -------------------------------------------------------------

TEST(BossRosterRegistryTest, HasPatchOnlyForPatchedMaps)
{
    EXPECT_TRUE(BossRosterRegistry::HasPatch(389));   // Ragefire Chasm — Taragaman last
    EXPECT_TRUE(BossRosterRegistry::HasPatch(189));   // Scarlet Monastery
    EXPECT_TRUE(BossRosterRegistry::HasPatch(109));   // Sunken Temple
    EXPECT_TRUE(BossRosterRegistry::HasPatch(209));   // ZulFarrak
    EXPECT_TRUE(BossRosterRegistry::HasPatch(90));    // Gnomeregan
    EXPECT_TRUE(BossRosterRegistry::HasPatch(230));   // Blackrock Depths
    EXPECT_TRUE(BossRosterRegistry::HasPatch(36));    // Deadmines
    EXPECT_TRUE(BossRosterRegistry::HasPatch(329));   // Stratholme
    EXPECT_TRUE(BossRosterRegistry::HasPatch(289));   // Scholomance
    EXPECT_TRUE(BossRosterRegistry::HasPatch(429));   // Dire Maul East
    EXPECT_TRUE(BossRosterRegistry::HasPatch(70));    // Uldaman — altar objectives
    EXPECT_TRUE(BossRosterRegistry::HasPatch(547));   // Slave Pens — drop objective
    EXPECT_TRUE(BossRosterRegistry::HasPatch(546));   // Underbog — drop objective
    EXPECT_TRUE(BossRosterRegistry::HasPatch(576));   // The Nexus — sphere objectives
    EXPECT_TRUE(BossRosterRegistry::HasPatch(600));   // Drak'Tharon Keep — the cast-spell-credit boss
    EXPECT_TRUE(BossRosterRegistry::HasPatch(595));   // Culling of Stratholme — objectives-only
                                                      // (the derived list is EMPTY: all four
                                                      // encounters are script TempSummons)
    EXPECT_TRUE(BossRosterRegistry::HasPatch(650));   // Trial of the Champion — objectives-only
                                                      // (every boss a summon, every credit a
                                                      // cast-spell, two bosses never die)
    EXPECT_TRUE(BossRosterRegistry::HasPatch(578));   // The Oculus — Eregos 54yd above mesh,
                                                      // Varos/Urom re-anchored, flight objectives
    EXPECT_FALSE(BossRosterRegistry::HasPatch(0));
    EXPECT_FALSE(BossRosterRegistry::HasPatch(34));   // Stockades — no patch
}

// Uldaman: the two altar travel objectives MUST sort between Grimlok (the boss
// before them) and Archaedas (the final boss, reordered above them), in the
// order keeper-altar -> Archaedas-altar -> Archaedas. The keeper altar opens the
// temple door; the Archaedas altar wakes the stoned boss; both must precede him.
TEST(BossRosterRegistryTest, UldamanAltarObjectivesSortBeforeArchaedas)
{
    std::vector<DungeonBossInfo> base = {
        Boss(4854, 6, "Grimlok", 70),
        Boss(2748, 7, "Archaedas", 70),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(70, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int grimlokIdx = -1, keeperIdx = -1, archAltarIdx = -1, archaedasIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 4854)
            grimlokIdx = i;
        else if (out[i].entry == 2748)
            archaedasIdx = i;
        else if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2)
            keeperIdx = i;
        else if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 3)
            archAltarIdx = i;
    }

    ASSERT_NE(grimlokIdx, -1);
    ASSERT_NE(keeperIdx, -1);
    ASSERT_NE(archAltarIdx, -1);
    ASSERT_NE(archaedasIdx, -1);
    EXPECT_LT(grimlokIdx, keeperIdx);
    EXPECT_LT(keeperIdx, archAltarIdx);
    EXPECT_LT(archAltarIdx, archaedasIdx);

    // Archaedas keeps his real DBC kill-bit (7) — only his clear ORDER moved.
    EXPECT_EQ(out[archaedasIdx].encounterIndex, 7u);
}

// Zul'Farrak, the rework: the ruled order is Theka -> Antu'sul -> Zum'rah -> the
// plaza -> the Pyramid -> the Altar -> the crew -> Ukorz, then the pool. The DBC
// bits (Velratha 0, Antu'sul 2, Theka 3, Zum'rah 4, Ukorz 7) do not match the
// path; the reorder stamps the ruled keys on four real bosses (kill-bits
// untouched), Velratha is folded into the pool objective (her pull is its
// event's first step) and the five objectives slot in between.
TEST(BossRosterRegistryTest, ZfFullClearOrder)
{
    namespace Z = DcZulFarrak;
    // Auto-derived list as BossSpawnIndex emits it (DBC encounterIndex order).
    std::vector<DungeonBossInfo> base = {
        Boss(7795, 0, "Hydromancer Velratha", 209),
        Boss(8127, 2, "Antu'sul", 209),
        Boss(7272, 3, "Theka the Martyr", 209),
        Boss(7271, 4, "Witch Doctor Zum'rah", 209),
        Boss(7267, 7, "Chief Ukorz Sandscalp", 209),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(209, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);
    ASSERT_EQ(out.size(), 9u);
    EXPECT_EQ(Find(out, 7795), nullptr);  // Velratha: the pool objective is her anchor

    auto pos = [&](uint32 entry)
    {
        for (int i = 0; i < (int)out.size(); ++i)
            if (out[i].entry == entry)
                return i;
        return -1;
    };
    auto objPos = [&](uint32 eventId)
    {
        for (int i = 0; i < (int)out.size(); ++i)
            if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == eventId)
                return i;
        return -1;
    };
    int const plaza = objPos(Z::EVENT_PLAZA);
    int const pyramid = objPos(Z::EVENT_PYRAMID);
    int const altar = objPos(Z::EVENT_ALTAR);
    int const crew = objPos(Z::EVENT_CREW);
    int const pool = objPos(Z::EVENT_POOL);
    ASSERT_GE(plaza, 0);
    ASSERT_GE(pyramid, 0);
    ASSERT_GE(altar, 0);
    ASSERT_GE(crew, 0);
    ASSERT_GE(pool, 0);

    std::vector<int> const order = { pos(7272), pos(8127), pos(7271), plaza, pyramid, altar,
                                     crew,      pos(7267), pool };
    for (std::size_t i = 0; i + 1 < order.size(); ++i)
        EXPECT_LT(order[i], order[i + 1]) << "step " << i;

    // Reordering must NOT disturb the real DBC kill-bits.
    EXPECT_EQ(Find(out, 7267)->encounterIndex, 7u);  // Ukorz
    EXPECT_EQ(BossOrderKey(*Find(out, 7267)), 8u);
    EXPECT_EQ(BossOrderKey(out[pool]), 9u);
}

// HANDOFF section 3.12, "then the pool not required": a run is complete at Ukorz
// with no `dc skip`. The pool objective is the map's only optional-by-design
// anchor (seeded into the skipped set at `dc on` and at a new instance); every
// other anchor is part of the bar. A map without one seeds nothing.
TEST(BossRosterRegistryTest, ZfPoolIsOptionalByDesign)
{
    namespace Z = DcZulFarrak;
    std::vector<DungeonBossInfo> base = {
        Boss(7795, 0, "Hydromancer Velratha", 209),
        Boss(7267, 7, "Chief Ukorz Sandscalp", 209),
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(209, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);
    int optional = 0;
    for (DungeonBossInfo const& b : out)
    {
        bool const isPool = b.kind == DungeonAnchorKind::Objective && b.eventId == Z::EVENT_POOL;
        EXPECT_EQ(b.optionalByDesign, isPool) << b.name;
        EXPECT_FALSE(b.skipByDesign) << b.name;  // runnable: `dc boss pool` or the zf-pool scenario
        optional += b.optionalByDesign ? 1 : 0;
    }
    EXPECT_EQ(optional, 1);
    std::vector<uint32> const seeded = BossRosterRegistry::OptionalEntries(209);
    ASSERT_EQ(seeded.size(), 1u);
    EXPECT_EQ(seeded[0], BossRosterRegistry::ObjectiveEntry(Z::OBJ_POOL));
    EXPECT_TRUE(BossRosterRegistry::OptionalEntries(36).empty());   // Deadmines
    EXPECT_TRUE(BossRosterRegistry::OptionalEntries(389).empty());  // Ragefire Chasm
}

// Each objective names what finishes it in the instance, so a reloaded or
// re-entered instance never replays a won stage: the plaza, the top and the Altar
// on the Pyramid slot (5), the crew on the End Door reading blown (its data id 2,
// value 3, saved; the crew is done by then), the pool on Gahz'rilla's slot (1).
TEST(BossRosterRegistryTest, ZfObjectivesFinishOnTheInstanceState)
{
    namespace Z = DcZulFarrak;
    std::vector<DungeonBossInfo> base = { Boss(7267, 7, "Chief Ukorz Sandscalp", 209) };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(209, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);
    int seen = 0;
    for (DungeonBossInfo const& o : out)
    {
        if (o.kind != DungeonAnchorKind::Objective)
            continue;
        ++seen;
        if (o.eventId == Z::EVENT_PLAZA || o.eventId == Z::EVENT_PYRAMID || o.eventId == Z::EVENT_ALTAR)
        {
            EXPECT_EQ(o.doneBossStateIndex, 5) << o.name;
            EXPECT_EQ(o.doneInstanceDataId, -1) << o.name;
        }
        else if (o.eventId == Z::EVENT_CREW)
        {
            EXPECT_EQ(o.doneBossStateIndex, -1);
            EXPECT_EQ(o.doneInstanceDataId, 2);
            EXPECT_EQ(o.doneInstanceDataValue, 3u);
        }
        else if (o.eventId == Z::EVENT_POOL)
        {
            EXPECT_EQ(o.doneBossStateIndex, 1);
            EXPECT_NEAR(o.x, 1645.0f, 0.01f);   // the gong's bank, Velratha's fight spot (F-1d)
            EXPECT_NEAR(o.y, 1165.5f, 0.01f);
            EXPECT_TRUE(o.optionalByDesign);
        }
        else
        {
            EXPECT_TRUE(false) << "unexpected objective " << o.name;
        }
    }
    EXPECT_EQ(seen, 5);
}
// Gnomeregan, the rework (HANDOFF section 3.12): Techbot, Fallout, Electrocutioner,
// then the Workshop Door from the inside (the full clear's detour objective, on
// the Alert System's key and sorted first), the Alert System, Pummeler,
// Thermaplugg. The DBC bits (Alert 0, Fallout 1, Techbot 2, Electrocutioner 3,
// Pummeler 4, Thermaplugg 5) would start in the hall behind the sealed door; the
// kill-bits stay the real ones. Pummeler's anchor moves out of the glass onto the
// pocket floor, inside the box his pull is accepted from, with his own bit.
TEST(BossRosterRegistryTest, GnomereganClearOrderAndPummelersFloorAnchor)
{
    namespace G = DcGnomeregan;
    std::vector<DungeonBossInfo> base = {
        Boss(G::NPC_ALERT_SYSTEM, 0, "The Mobile Alert System", 90),
        Boss(G::NPC_FALLOUT, 1, "Viscous Fallout", 90),
        Boss(G::NPC_TECHBOT, 2, "Techbot", 90),
        Boss(G::NPC_ELECTRO, 3, "Electrocutioner 6000", 90),
        Boss(G::NPC_PUMMELER, 4, "Crowd Pummeler 9-60", 90),
        Boss(G::NPC_THERMAPLUGG, 5, "Mekgineer Thermaplugg", 90),
    };
    base[4].x = G::PUMMELER_X;
    base[4].y = G::PUMMELER_Y;
    base[4].z = G::PUMMELER_Z;
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(90, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);
    ASSERT_EQ(out.size(), 7u);

    std::vector<uint32> got;
    for (DungeonBossInfo const& b : out)
        got.push_back(b.entry);
    std::vector<uint32> const want = { G::NPC_TECHBOT, G::NPC_FALLOUT, G::NPC_ELECTRO,
                                       BossRosterRegistry::ObjectiveEntry(G::OBJ_WORKSHOP_DOOR),
                                       G::NPC_ALERT_SYSTEM, G::NPC_PUMMELER, G::NPC_THERMAPLUGG };
    EXPECT_EQ(got, want);
    for (std::size_t i = 0; i + 1 < out.size(); ++i)
        EXPECT_LE(BossOrderKey(out[i]), BossOrderKey(out[i + 1])) << i;

    // The kill-bits are the real ones (slot = bit).
    EXPECT_EQ(Find(out, G::NPC_ALERT_SYSTEM)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, G::NPC_FALLOUT)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, G::NPC_TECHBOT)->encounterIndex, 2u);
    EXPECT_EQ(Find(out, G::NPC_ELECTRO)->encounterIndex, 3u);
    EXPECT_EQ(Find(out, G::NPC_THERMAPLUGG)->encounterIndex, 5u);

    DungeonBossInfo const* pummeler = Find(out, G::NPC_PUMMELER);
    ASSERT_NE(pummeler, nullptr);
    EXPECT_EQ(pummeler->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(pummeler->encounterIndex, 4u);
    EXPECT_EQ(pummeler->doneBossStateIndex, -1);
    EXPECT_EQ(BossOrderKey(*pummeler), 5u);
    EXPECT_TRUE(G::InPocketBox(pummeler->x, pummeler->y, pummeler->z));
    EXPECT_FALSE(G::InPocketBox(G::PUMMELER_X, G::PUMMELER_Y, G::PUMMELER_Z));   // his origin is in the glass
    // In his melee reach from the floor: sqrt(h^2 + 4.8^2) < 10 + 1.5 + 4/3 (D5's
    // reach 10); 4.0yd out on the pocket's center line.
    float const h = std::hypot(pummeler->x - G::PUMMELER_X, pummeler->y - G::PUMMELER_Y);
    float const dz = pummeler->z - G::PUMMELER_Z;
    EXPECT_LT(std::sqrt(h * h + dz * dz), 10.0f + 1.5f + 4.0f / 3.0f);
    EXPECT_GT(h, 3.0f);   // in front of the glass face (x -884.4 at y 364), not in it
}

// The door objective finishes on the instance reading the door open (data 10,
// value 2: the full clear's click, a human's, or the Alert System's death), read
// live, so a re-entered instance with the door open goes straight to the hall.
TEST(BossRosterRegistryTest, GnomereganDoorObjectiveFinishesOnTheDoorReadingOpen)
{
    namespace G = DcGnomeregan;
    std::vector<DungeonBossInfo> base = { Boss(G::NPC_ALERT_SYSTEM, 0, "The Mobile Alert System", 90) };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(90, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);
    int seen = 0;
    for (DungeonBossInfo const& o : out)
    {
        if (o.kind != DungeonAnchorKind::Objective)
            continue;
        ++seen;
        EXPECT_EQ(o.entry, BossRosterRegistry::ObjectiveEntry(G::OBJ_WORKSHOP_DOOR));
        EXPECT_EQ(o.eventId, G::EVENT_WORKSHOP_DOOR);
        EXPECT_EQ(BossOrderKey(o), static_cast<uint32>(G::ORDER_ALERT));
        EXPECT_EQ(o.doneBossStateIndex, -1);
        EXPECT_EQ(o.doneInstanceDataId, 10);
        EXPECT_EQ(o.doneInstanceDataValue, 2u);
        EXPECT_FALSE(o.optionalByDesign);
        EXPECT_FALSE(o.skipByDesign);
        EXPECT_EQ(AnchorDoneByInstanceValues(o, 0, G::WORKSHOP_DOOR_OPEN), DcAnchorDoneVia::InstanceData);
        EXPECT_EQ(AnchorDoneByInstanceValues(o, 0, G::WORKSHOP_DOOR_UNLOCKED), DcAnchorDoneVia::None);
        EXPECT_EQ(AnchorDoneByInstanceValues(o, 0, G::WORKSHOP_DOOR_SEALED), DcAnchorDoneVia::None);
        EXPECT_NEAR(o.x, G::DOOR_INSIDE_X, 0.01f);
        EXPECT_NEAR(o.y, G::DOOR_INSIDE_Y, 0.01f);
    }
    EXPECT_EQ(seen, 1);
    EXPECT_TRUE(BossRosterRegistry::OptionalEntries(90).empty());   // every anchor is part of the bar
}

// Hellfire Ramparts (543): the auto-derived list ends at Omor because the final
// boss's credit creature, Vazruden (17537), is a TempSummon with no DB spawn so
// BossSpawnIndex never emits it. The patch adds it explicitly at the lower
// platform with DBC kill-bit 2 (set directly — there is no base entry to inherit
// from), so the run does not stop one boss short.
// The Mechanar Gatewatchers (Gyro-Kill 19218, Iron-Hand 19710) are NOT DBC
// encounters (no instance_encounters KILL_CREATURE row), so BossSpawnIndex never
// derives them and the auto-derived base holds only the three real bosses. The
// patch must ADD them as bosses (Gyro-Kill order 1, Iron-Hand order 3, with
// Capacitus reordered to 2 between them) with instance-boss-state completion
// (doneBossStateIndex 0/1) and a no-DBC-bit encounterIndex, or the picker skips
// straight to Capacitus and the party never opens the Mo'arg doors.
TEST(BossRosterRegistryTest, MechanarAddsGatewatchersAheadOfCapacitus)
{
    // Base as BossSpawnIndex emits it — only the three DBC bosses.
    std::vector<DungeonBossInfo> base = {
        Boss(19219, 0, "Mechano-Lord Capacitus", 554),
        Boss(19221, 1, "Nethermancer Sepethrea", 554),
        Boss(19220, 2, "Pathaleon the Calculator", 554),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(554, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    DungeonBossInfo const* gyro = Find(out, 19218);
    DungeonBossInfo const* iron = Find(out, 19710);
    ASSERT_NE(gyro, nullptr) << "Gyro-Kill must be injected as a boss";
    ASSERT_NE(iron, nullptr) << "Iron-Hand must be injected as a boss";

    EXPECT_EQ(gyro->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(iron->kind, DungeonAnchorKind::Boss);
    // Completion is the instance boss-state slot, not a DBC bit.
    EXPECT_EQ(gyro->doneBossStateIndex, 0);
    EXPECT_EQ(iron->doneBossStateIndex, 1);
    // encounterIndex parked past bit 31 so the completedMask check never matches
    // a real boss's bit (default 0 would collide with DBC bit 0 = Capacitus).
    EXPECT_GE(gyro->encounterIndex, 32u);
    EXPECT_GE(iron->encounterIndex, 32u);

    // Ordered first: Gyro-Kill(1) -> Capacitus(2) -> Iron-Hand(3) -> ...
    // Capacitus sits between the Gatewatchers so the tank approaches his pit from
    // Gyro-Kill (NW) and the SE Driller pack falls on the walk down to Iron-Hand
    // (which is why the room-aggro pre-clear for 19219 was dropped).
    ASSERT_GE(out.size(), 3u);
    EXPECT_EQ(out[0].entry, 19218u);
    EXPECT_EQ(out[1].entry, 19219u);
    EXPECT_EQ(out[2].entry, 19710u);
}

TEST(BossRosterRegistryTest, HellfireRampartsAddsFinalBoss)
{
    // Auto-derived list as BossSpawnIndex emits it (only the two spawned bosses).
    std::vector<DungeonBossInfo> base = {
        Boss(17306, 0, "Watchkeeper Gargolmar", 543),
        Boss(17308, 1, "Omor the Unscarred", 543),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(543, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    DungeonBossInfo const* vaz = Find(out, 17537);
    ASSERT_NE(vaz, nullptr) << "Vazruden must be injected";
    EXPECT_EQ(vaz->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(vaz->encounterIndex, 2u);  // DBC bit 2, the final encounter
    EXPECT_EQ(vaz->inheritCompletionFrom, 0u);  // set directly, not inherited
    EXPECT_FLOAT_EQ(vaz->x, -1378.0f);
    EXPECT_FLOAT_EQ(vaz->y, 1718.0f);

    // Sorted last, after Gargolmar (0) and Omor (1).
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].entry, 17306u);
    EXPECT_EQ(out[1].entry, 17308u);
    EXPECT_EQ(out[2].entry, 17537u);
}

// Stratholme full clear-path order (issue #5). The DBC order is
//   Unforgiven 0, Hearthsinger 1, Timmy 2, Cannon 3, Malor 4, Galford 5,
//   Baroness 7, Nerub'enkan 8, Maleki 9, Barthilas 10, Baron 12
// (Balnazzar bit 6 is NOT auto-derived: its credit creature 10813 has no
// creature.sql spawn, so BossSpawnIndex drops it and the patch fills the gap
// with a Dathrohan objective). Two structural problems: the DBC runs the
// ziggurats before Barthilas though the path runs Barthilas first, and it lists
// The Unforgiven / Hearthsinger Forresten first, forcing a full circle before
// the live side. The patch stamps a contiguous 1..13 order key so the clear runs
//   Timmy(1) -> Malor(2) -> Cannon(3) -> Galford(4) -> Dathrohan/Balnazzar(5) ->
//   Unforgiven(6) -> Hearthsinger(7) -> Barthilas(8) -> Baroness(9) ->
//   Nerub'enkan(10) -> Maleki(11) -> Slaughterhouse(12) -> Baron(13)
// while every boss keeps its real DBC kill-bit (encounterIndex) untouched.
TEST(BossRosterRegistryTest, StratholmeFullClearPathOrder)
{
    std::vector<DungeonBossInfo> base = {
        Boss(10516, 0, "The Unforgiven", 329),
        Boss(10558, 1, "Hearthsinger Forresten", 329),
        Boss(10808, 2, "Timmy the Cruel", 329),
        Boss(10997, 3, "Cannon Master Willey", 329),
        Boss(11032, 4, "Malor the Zealous", 329),
        Boss(10811, 5, "Archivist Galford", 329),
        Boss(10436, 7, "Baroness Anastari", 329),
        Boss(10437, 8, "Nerub'enkan", 329),
        Boss(10438, 9, "Maleki the Pallid", 329),
        Boss(10435, 10, "Magistrate Barthilas", 329),
        Boss(10440, 12, "Baron Rivendare", 329),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(329, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int timmyIdx = -1, malorIdx = -1, cannonIdx = -1, unforgivenIdx = -1,
        hearthIdx = -1, galfordIdx = -1, dathrohanIdx = -1, barthIdx = -1,
        baronessIdx = -1, malekiIdx = -1, slaughterIdx = -1, baronIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 10808) timmyIdx = i;
        if (out[i].entry == 11032) malorIdx = i;
        if (out[i].entry == 10997) cannonIdx = i;
        if (out[i].entry == 10516) unforgivenIdx = i;
        if (out[i].entry == 10558) hearthIdx = i;
        if (out[i].entry == 10811) galfordIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 5u) dathrohanIdx = i;
        if (out[i].entry == 10435) barthIdx = i;
        if (out[i].entry == 10436) baronessIdx = i;
        if (out[i].entry == 10438) malekiIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 4u) slaughterIdx = i;
        if (out[i].entry == 10440) baronIdx = i;
    }
    ASSERT_GE(timmyIdx, 0);
    ASSERT_GE(malorIdx, 0);
    ASSERT_GE(cannonIdx, 0);
    ASSERT_GE(unforgivenIdx, 0);
    ASSERT_GE(hearthIdx, 0);
    ASSERT_GE(galfordIdx, 0);
    ASSERT_GE(dathrohanIdx, 0) << "Dathrohan (Balnazzar) objective missing";
    ASSERT_GE(barthIdx, 0);
    ASSERT_GE(baronessIdx, 0);
    ASSERT_GE(slaughterIdx, 0) << "slaughterhouse objective missing";
    ASSERT_GE(baronIdx, 0);

    // Timmy leads: he comes before The Unforgiven and Hearthsinger (issue #5).
    EXPECT_LT(timmyIdx, unforgivenIdx) << "Timmy must precede The Unforgiven";
    EXPECT_LT(timmyIdx, hearthIdx) << "Timmy must precede Hearthsinger";

    // Malor the Zealous clears before Cannon Master Willey (issue #5 follow-up).
    EXPECT_LT(timmyIdx, malorIdx) << "Timmy still leads the live side";
    EXPECT_LT(malorIdx, cannonIdx) << "Malor must precede the Cannon Master";
    EXPECT_LT(cannonIdx, galfordIdx) << "Cannon Master before Galford";

    // Live side (Timmy -> ... -> Galford -> Dathrohan/Balnazzar) before the two
    // relocated bosses, which in turn precede the dead side.
    EXPECT_LT(galfordIdx, dathrohanIdx) << "Dathrohan follows Galford on the live side";
    EXPECT_LT(dathrohanIdx, unforgivenIdx) << "live side before The Unforgiven";
    EXPECT_LT(unforgivenIdx, hearthIdx) << "Unforgiven before Hearthsinger";
    EXPECT_LT(hearthIdx, barthIdx) << "relocated bosses before the dead side";
    EXPECT_LT(barthIdx, baronessIdx) << "Barthilas must precede the ziggurats";
    EXPECT_LT(malekiIdx, slaughterIdx) << "ziggurats before the slaughterhouse";
    EXPECT_LT(slaughterIdx, baronIdx) << "slaughterhouse before Baron";

    // The contiguous 1..13 order keys.
    EXPECT_EQ(BossOrderKey(out[timmyIdx]), 1u);
    EXPECT_EQ(BossOrderKey(out[malorIdx]), 2u);
    EXPECT_EQ(BossOrderKey(out[cannonIdx]), 3u);
    EXPECT_EQ(BossOrderKey(out[galfordIdx]), 4u);
    EXPECT_EQ(out[dathrohanIdx].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(BossOrderKey(out[dathrohanIdx]), 5u);
    EXPECT_EQ(BossOrderKey(out[unforgivenIdx]), 6u);
    EXPECT_EQ(BossOrderKey(out[hearthIdx]), 7u);
    EXPECT_EQ(BossOrderKey(out[slaughterIdx]), 12u);
    EXPECT_EQ(BossOrderKey(out[baronIdx]), 13u);

    // Reordering must NOT disturb real kill-bits: completion still keys on the DBC
    // encounterIndex, only the order key moves.
    EXPECT_EQ(out[barthIdx].encounterIndex, 10u);
    EXPECT_EQ(out[barthIdx].orderOverride, 8);
    EXPECT_EQ(BossOrderKey(out[barthIdx]), 8u);
    EXPECT_EQ(out[unforgivenIdx].encounterIndex, 0u) << "Unforgiven keeps kill-bit 0";
    EXPECT_EQ(out[hearthIdx].encounterIndex, 1u) << "Hearthsinger keeps kill-bit 1";
    EXPECT_EQ(out[baronIdx].encounterIndex, 12u) << "Baron keeps kill-bit 12";
}

// Blackrock Depths: the Ring of Law objective (its own DungeonEncounter bit 3,
// credited to the spawn-less Grimstone) is injected between Houndmaster Grebmar
// (bit 2) and Pyromancer Loregrain (bit 4), carrying the arena event.
TEST(BossRosterRegistryTest, RingOfLawObjectiveSortsBetweenGrebmarAndLoregrain)
{
    std::vector<DungeonBossInfo> base = {
        Boss(9319, 2, "Houndmaster Grebmar", 230),
        Boss(9024, 4, "Pyromancer Loregrain", 230),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(230, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int grebmarIdx = -1, ringIdx = -1, loregrainIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 9319)
            grebmarIdx = i;
        // Key on the event id, not on "is an objective": map 230 carries a
        // second objective (the Shadowforge Lock, event 2) further down the list.
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 1u)
            ringIdx = i;
        if (out[i].entry == 9024)
            loregrainIdx = i;
    }
    ASSERT_GE(ringIdx, 0) << "Ring of Law objective missing";
    ASSERT_GE(grebmarIdx, 0);
    ASSERT_GE(loregrainIdx, 0);
    EXPECT_LT(grebmarIdx, ringIdx) << "Ring of Law must follow Grebmar";
    EXPECT_LT(ringIdx, loregrainIdx) << "Ring of Law must precede Loregrain";
    EXPECT_EQ(out[ringIdx].encounterIndex, 3u);
    EXPECT_EQ(out[ringIdx].eventId, 1u);
}

// Blackrock Depths: the Shadowforge Lock objective SHARES General Angerforge's
// bit (9) — it has no encounter of its own — so the objective-before-boss
// tie-break is what puts the lever after Bael'Gar (bit 8) and before Angerforge.
// Sharing a live boss's bit is only safe because NextDungeonBossValue consults
// the completion mask for Boss anchors alone; assert the ordering here so a
// change to that tie-break can't silently strand the lever behind Angerforge.
TEST(BossRosterRegistryTest, ShadowforgeLockSortsBetweenBaelGarAndAngerforge)
{
    std::vector<DungeonBossInfo> base = {
        Boss(9016, 8, "Bael'Gar", 230),
        Boss(9033, 9, "General Angerforge", 230),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(230, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int baelIdx = -1, lockIdx = -1, angerIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 9016)
            baelIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2u)
            lockIdx = i;
        if (out[i].entry == 9033)
            angerIdx = i;
    }
    ASSERT_GE(lockIdx, 0) << "Shadowforge Lock objective missing";
    ASSERT_GE(baelIdx, 0);
    ASSERT_GE(angerIdx, 0);
    EXPECT_LT(baelIdx, lockIdx) << "the lever must be pulled after Bael'Gar";
    EXPECT_LT(lockIdx, angerIdx) << "the lever must be pulled before Angerforge";
    EXPECT_EQ(out[lockIdx].encounterIndex, 9u);
    EXPECT_EQ(out[lockIdx].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(out[angerIdx].encounterIndex, 9u) << "Angerforge keeps kill-bit 9";
}

// Deadmines: the Defias Cannon objective shares Mr. Smite's bit (3); the
// objective-before-boss tie-break must order it after Gilnid (bit 2) and before
// Mr. Smite, so the tank opens the Iron Clad Door before heading to the ship.
TEST(BossRosterRegistryTest, IronCladDoorSortsBetweenGilnidAndMrSmite)
{
    std::vector<DungeonBossInfo> base = {
        Boss(1763, 2, "Gilnid", 36),
        Boss(646, 3, "Mr. Smite", 36),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(36, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int gilnidIdx = -1, doorIdx = -1, smiteIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 1763)
            gilnidIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective)
            doorIdx = i;
        if (out[i].entry == 646)
            smiteIdx = i;
    }
    ASSERT_GE(doorIdx, 0) << "Iron Clad Door objective missing";
    ASSERT_GE(gilnidIdx, 0);
    ASSERT_GE(smiteIdx, 0);
    EXPECT_LT(gilnidIdx, doorIdx) << "cannon must follow Gilnid";
    EXPECT_LT(doorIdx, smiteIdx) << "cannon must precede Mr. Smite";
    EXPECT_EQ(out[doorIdx].encounterIndex, 3u);
    EXPECT_EQ(out[doorIdx].eventId, 1u);
}

// Deadmines: Sneed (643, bit 1) has no creature row — Sneed's Shredder (642)
// ejects him on death — so BossSpawnIndex never derives him. The patch anchors
// the Shredder with Sneed's bit, between Rhahk'zor (0) and Gilnid (2).
TEST(BossRosterRegistryTest, DeadminesAnchorsSneedOnTheShredder)
{
    std::vector<DungeonBossInfo> base = {
        Boss(644, 0, "Rhahk'zor", 36),
        Boss(1763, 2, "Gilnid", 36),
        Boss(646, 3, "Mr. Smite", 36),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(36, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int rhahkIdx = -1, shredderIdx = -1, gilnidIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 644)
            rhahkIdx = i;
        if (out[i].entry == 642)
            shredderIdx = i;
        if (out[i].entry == 1763)
            gilnidIdx = i;
    }
    ASSERT_GE(shredderIdx, 0) << "Sneed's Shredder anchor missing";
    ASSERT_GE(rhahkIdx, 0);
    ASSERT_GE(gilnidIdx, 0);
    EXPECT_EQ(out[shredderIdx].kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(out[shredderIdx].encounterIndex, 1u) << "carries Sneed's DBC bit";
    EXPECT_LT(rhahkIdx, shredderIdx);
    EXPECT_LT(shredderIdx, gilnidIdx);
}

// A roster that already derives the Shredder (a fork whose encounter row credits
// 642 for bit 1) must still list it exactly once.
TEST(BossRosterRegistryTest, DeadminesShredderIsNeverListedTwice)
{
    std::vector<DungeonBossInfo> base = {
        Boss(644, 0, "Rhahk'zor", 36),
        Boss(642, 1, "Sneed's Shredder", 36),
        Boss(1763, 2, "Gilnid", 36),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(36, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_HEROIC), base);

    int shredders = 0;
    for (DungeonBossInfo const& b : out)
        if (b.entry == 642)
            ++shredders;
    EXPECT_EQ(shredders, 1);
    DungeonBossInfo const* shredder = Find(out, 642);
    ASSERT_NE(shredder, nullptr);
    EXPECT_EQ(shredder->encounterIndex, 1u);
}

// The Slave Pens (547): the post-Mennu drop-down objective shares Rokmar's bit
// (1); the objective-before-boss tie-break must order it after Mennu (bit 0) and
// before Rokmar, so the party is teleported across the navmesh break before
// boss-nav routes to Rokmar. (No boss surgery — all three bosses auto-derive.)
TEST(BossRosterRegistryTest, SlavePensDropSortsBetweenMennuAndRokmar)
{
    std::vector<DungeonBossInfo> base = {
        Boss(17941, 0, "Mennu the Betrayer", 547),
        Boss(17991, 1, "Rokmar the Crackler", 547),
        Boss(17942, 2, "Quagmirran", 547),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(547, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int mennuIdx = -1, dropIdx = -1, rokmarIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 17941)
            mennuIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective)
            dropIdx = i;
        if (out[i].entry == 17991)
            rokmarIdx = i;
    }
    ASSERT_GE(dropIdx, 0) << "drop-down objective missing";
    ASSERT_GE(mennuIdx, 0);
    ASSERT_GE(rokmarIdx, 0);
    EXPECT_LT(mennuIdx, dropIdx) << "drop must follow Mennu";
    EXPECT_LT(dropIdx, rokmarIdx) << "drop must precede Rokmar";
    EXPECT_EQ(out[dropIdx].encounterIndex, 1u);
    EXPECT_EQ(out[dropIdx].eventId, 1u);
    // The three real bosses survive untouched (no remove/re-add).
    EXPECT_NE(Find(out, 17942), nullptr) << "Quagmirran must remain";
}

// The Underbog (546): the post-Ghaz'an two-hop drop objective shares Swamplord's
// bit (2); the objective-before-boss tie-break must order it after Ghaz'an (bit 1)
// and before Swamplord, so the party is teleported down the navmesh break before
// boss-nav routes to Swamplord. (No boss surgery — all four bosses auto-derive.)
TEST(BossRosterRegistryTest, UnderbogDropSortsBetweenGhazanAndSwamplord)
{
    std::vector<DungeonBossInfo> base = {
        Boss(17770, 0, "Hungarfen", 546),
        Boss(18105, 1, "Ghaz'an", 546),
        Boss(17826, 2, "Swamplord Musel'ek", 546),
        Boss(17882, 3, "The Black Stalker", 546),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(546, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    int ghazanIdx = -1, dropIdx = -1, swamplordIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 18105)
            ghazanIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective)
            dropIdx = i;
        if (out[i].entry == 17826)
            swamplordIdx = i;
    }
    ASSERT_GE(dropIdx, 0) << "drop-down objective missing";
    ASSERT_GE(ghazanIdx, 0);
    ASSERT_GE(swamplordIdx, 0);
    EXPECT_LT(ghazanIdx, dropIdx) << "drop must follow Ghaz'an";
    EXPECT_LT(dropIdx, swamplordIdx) << "drop must precede Swamplord";
    EXPECT_EQ(out[dropIdx].encounterIndex, 2u);
    EXPECT_EQ(out[dropIdx].eventId, 1u);
    // The four real bosses survive untouched (no remove/re-add).
    EXPECT_NE(Find(out, 17882), nullptr) << "The Black Stalker must remain";
}

// Dire Maul East: the Ironbark / Conservatory Door objective (orderOverride 40,
// eventId 1) must sort after the three southern bosses (reordered 10/20/30) and
// before Alzzin the Wildshaper (reordered 50), so the tank gossips Ironbark to
// open the door before heading to Alzzin's grove. The reorder leaves the real
// DBC kill-bits (the base encounterIndex) untouched.
TEST(BossRosterRegistryTest, DireMaulEastIronbarkSortsBeforeAlzzin)
{
    std::vector<DungeonBossInfo> base = {
        Boss(11490, 0, "Zevrim Thornhoof", 429),
        Boss(13280, 1, "Hydrospawn", 429),
        Boss(14327, 2, "Lethtendris", 429),
        Boss(11492, 3, "Alzzin the Wildshaper", 429),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(429, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    // NOTE: Dire Maul shares ONE map-429 patch across wings, so Apply() also
    // appends the West pylon objectives (eventId 4-8). Identify Ironbark by his
    // eventId (1), not "any objective", which would now match a West pylon.
    int lethIdx = -1, ironbarkIdx = -1, alzzinIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 14327)
            lethIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 1)
            ironbarkIdx = i;
        if (out[i].entry == 11492)
            alzzinIdx = i;
    }
    ASSERT_GE(ironbarkIdx, 0) << "Ironbark / Conservatory Door objective missing";
    ASSERT_GE(lethIdx, 0);
    ASSERT_GE(alzzinIdx, 0);
    EXPECT_LT(lethIdx, ironbarkIdx) << "Ironbark must follow the southern bosses";
    EXPECT_LT(ironbarkIdx, alzzinIdx) << "Ironbark must precede Alzzin";
    EXPECT_EQ(out[ironbarkIdx].eventId, 1u);

    // Reorder keys: 10/20/30/objective 40/50; real kill-bits untouched.
    EXPECT_EQ(BossOrderKey(*Find(out, 14327)), 30u);
    EXPECT_EQ(BossOrderKey(out[ironbarkIdx]), 40u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11492)), 50u);
    EXPECT_EQ(Find(out, 11492)->encounterIndex, 3u) << "Alzzin DBC kill-bit intact";
}

// Dire Maul West: the kill order is fixed (Immol'thar before the friendly-until-
// his-death Prince), and Immol'thar's five Crystal Generator pylons are added as
// travel objectives — three before Tendris, two before Immol'thar. Real DBC
// kill-bits stay intact; objective encounterIndex values are synthetic highs.
TEST(BossRosterRegistryTest, DireMaulWestPylonsAndOrder)
{
    // Arbitrary DBC bit order (deliberately NOT the kill order) to prove the
    // reorder is what fixes it.
    std::vector<DungeonBossInfo> base = {
        Boss(11486, 0, "Prince Tortheldrin", 429),
        Boss(11496, 1, "Immol'thar", 429),
        Boss(11487, 2, "Magister Kalendris", 429),
        Boss(11488, 3, "Illyanna Ravenoak", 429),
        Boss(11489, 4, "Tendris Warpwood", 429),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(429, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    // All five West bosses survive with their real kill-bits untouched.
    ASSERT_NE(Find(out, 11489), nullptr);
    EXPECT_EQ(Find(out, 11489)->encounterIndex, 4u) << "Tendris DBC kill-bit intact";
    EXPECT_EQ(Find(out, 11486)->encounterIndex, 0u) << "Prince DBC kill-bit intact";

    // Reorder keys put the kill order right: Tendris < Illyanna < Kalendris <
    // Immol'thar < Prince.
    EXPECT_EQ(BossOrderKey(*Find(out, 11489)), 10u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11488)), 20u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11487)), 30u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11496)), 45u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11486)), 50u);

    // Immol'thar strictly precedes the Prince in the final order.
    int immolIdx = -1, princeIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 11496)
            immolIdx = i;
        if (out[i].entry == 11486)
            princeIdx = i;
    }
    ASSERT_GE(immolIdx, 0);
    ASSERT_GE(princeIdx, 0);
    EXPECT_LT(immolIdx, princeIdx) << "Immol'thar must die before the Prince";

    // The five pylon objectives are present, wired to events 4-8, with synthetic
    // (>= 32, so never confused with a real DBC bit) encounterIndex values.
    int pylonCount = 0;
    for (DungeonBossInfo const& b : out)
    {
        if (b.kind == DungeonAnchorKind::Objective && b.eventId >= 4 && b.eventId <= 8)
        {
            ++pylonCount;
            EXPECT_GE(b.encounterIndex, 32u)
                << "pylon objective kill-bit must be synthetic-high";
        }
    }
    EXPECT_EQ(pylonCount, 5) << "all five Crystal Generator objectives present";

    // Two pylons are placed after Kalendris (order 40/41), three before Tendris
    // (order 5/6/7) — i.e. the northern pair comes after Immol'thar's order in
    // the list? No: orderOverride 40/41 < Immol'thar's 45, so both northern
    // pylons precede the Immol'thar engage.
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(5))), 45u);
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(6))), 45u);
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(2))),
              BossOrderKey(*Find(out, 11489)))
        << "southern pylon trio routed before Tendris";

    // Barrier-skirt waypoint (OBJ 7) is sequenced strictly between the two
    // northern pylons (Gen4 OBJ5 -> waypoint -> Gen5 OBJ6) so the route around
    // Immol'thar's force field is taken between them, and it carries no event.
    DungeonBossInfo const* wp = Find(out, BossRosterRegistry::ObjectiveEntry(7));
    ASSERT_NE(wp, nullptr);
    EXPECT_EQ(wp->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(wp->eventId, 0u) << "waypoint is a pure travel anchor (no event)";
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(5))),
              BossOrderKey(*wp));
    EXPECT_LT(BossOrderKey(*wp),
              BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(6))));

    // The Warpwood entrance sweep (OBJ 8 west / OBJ 9 east) exists and is ordered
    // FIRST — before every pylon and Tendris — so the entrance room is swept on
    // the way in.
    for (uint32 obj : {8u, 9u})
    {
        DungeonBossInfo const* sweep = Find(out, BossRosterRegistry::ObjectiveEntry(obj));
        ASSERT_NE(sweep, nullptr);
        EXPECT_LT(BossOrderKey(*sweep),
                  BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(2))))
            << "entrance sweep OBJ" << obj << " precedes crystal generator 1";
        EXPECT_LT(BossOrderKey(*sweep), BossOrderKey(*Find(out, 11489)));
    }

    // REGRESSION GUARD: every objective that carries a ClearRadius (the entrance
    // pre-clear AND the five crystals) must have a MODERATE arriveRadius. Two ways
    // it has broken live, both guarded here:
    //   * too SMALL (< the ~10-17yd the tank parks short of an off-mesh GO dais)
    //     -> the tank never "arrives", thrashes in travel (deadlock). Need >= 25.
    //   * too LARGE -> the tank "arrives" far from the mobs, the ClearRadius gate
    //     finds nothing loaded and completes in 0ms (premature no-op). Keep it
    //     within ~ClearRadius + 20 so "arrived" means "among the mobs".
    for (uint32 obj : {8u, 9u, 2u, 3u, 4u, 5u, 6u})
    {
        DungeonBossInfo const* o = Find(out, BossRosterRegistry::ObjectiveEntry(obj));
        ASSERT_NE(o, nullptr);
        DungeonEvent const* e = DungeonEventRegistry::Find(429, o->eventId);
        ASSERT_NE(e, nullptr);
        for (auto const& step : e->steps)
            if (step.kind == EventStepKind::ClearRadius)
            {
                EXPECT_GE(o->arriveRadius, 25.0f)
                    << "OBJ" << obj << " arriveRadius too small -> deadlock risk";
                EXPECT_LE(o->arriveRadius, step.radius + 20.0f)
                    << "OBJ" << obj << " arriveRadius too large -> premature 0ms clear";
            }
    }
}

// Sunken Temple: the DBC bit order is NOT a valid clear order. The roster removes
// the three phase/puzzle-gated bosses (Weaver 5720, Dreamscythe 5721, Atal'alarion
// 8580) from their low bits and re-adds them — plus the statue/idol/Avatar pit
// wing — as event-bearing objective anchors. Weaver & Dreamscythe land on the
// required spine (after Jammal'an, before Eranikus); the whole pit wing lands at
// the route tail (after Eranikus).
TEST(BossRosterRegistryTest, SunkenTempleReordersPhaseGatedBosses)
{
    std::vector<DungeonBossInfo> base = {
        Boss(8580, 0, "Atal'alarion", 109),
        Boss(5721, 1, "Dreamscythe", 109),
        Boss(5720, 2, "Weaver", 109),
        Boss(5710, 3, "Jammal'an the Prophet", 109),
        Boss(5719, 4, "Morphaz", 109),
        Boss(5722, 6, "Hazzas", 109),
        Boss(5709, 8, "Shade of Eranikus", 109),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(109, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    // The three phase/puzzle-gated bosses are gone as combat bosses.
    EXPECT_EQ(Find(out, 8580), nullptr);
    EXPECT_EQ(Find(out, 5721), nullptr);
    EXPECT_EQ(Find(out, 5720), nullptr);

    // Kept auto bosses survive with their real bits.
    ASSERT_NE(Find(out, 5710), nullptr);
    EXPECT_EQ(Find(out, 5710)->encounterIndex, 3u);
    ASSERT_NE(Find(out, 5709), nullptr);

    auto pos = [&](uint32 entry)
    {
        for (int i = 0; i < (int)out.size(); ++i)
            if (out[i].entry == entry)
                return i;
        return -1;
    };
    // Locate the re-added objectives by their event ids: a forcefield ring anchor
    // (1), Weaver & Dreamscythe (10), statue 1 (2), and the Avatar (9).
    int ffPos = -1, wdPos = -1, statuePos = -1, avatarPos = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].kind != DungeonAnchorKind::Objective)
            continue;
        if (out[i].eventId == 1)
            ffPos = i;
        if (out[i].eventId == 10)
            wdPos = i;
        if (out[i].eventId == 2)
            statuePos = i;
        if (out[i].eventId == 9)
            avatarPos = i;
    }
    ASSERT_GE(ffPos, 0) << "forcefield ring anchor missing";
    ASSERT_GE(wdPos, 0) << "Weaver & Dreamscythe objective missing";
    ASSERT_GE(statuePos, 0) << "statue objective missing";
    ASSERT_GE(avatarPos, 0) << "Avatar objective missing";

    // Forcefield ring anchors come first, before Jammal'an (the gate to him).
    EXPECT_LT(ffPos, pos(5710));

    // Required spine: Weaver & Dreamscythe after Jammal'an, before Eranikus.
    EXPECT_LT(pos(5710), wdPos);
    EXPECT_LT(wdPos, pos(5709));

    // Optional pit wing (statues..Avatar) at the tail, after Eranikus.
    EXPECT_LT(pos(5709), statuePos);
    EXPECT_LT(statuePos, avatarPos);
}

// --- Apply: pass-through for unpatched maps -------------------------------

TEST(BossRosterRegistryTest, UnpatchedMapReturnsBaseUnchanged)
{
    std::vector<DungeonBossInfo> base = {
        Boss(1001, 0, "A"),
        Boss(1002, 1, "B"),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(34, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].entry, 1001u);
    EXPECT_EQ(out[1].entry, 1002u);
}

// --- Apply: SM Cathedral patch (the rework's Last Stand) -----------------

TEST(BossRosterRegistryTest, SmCathedralAnchorsTheLastStandAndWhitemanesVigil)
{
    // The auto-derived Cathedral list as BossSpawnIndex emits it on a database
    // that still credits encounter 449 to Fairbanks: Fairbanks (idx 5) and
    // Whitemane (idx 6) in her chamber, plus the Armory's Herod to prove the patch
    // only touches the entries it names.
    std::vector<DungeonBossInfo> base = {
        Boss(3975, 4, "Herod", 189),               // Armory — untouched
        Boss(4542, 5, "High Inquisitor Fairbanks", 189),
        Boss(3977, 6, "High Inquisitor Whitemane", 189),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(189, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    // Fairbanks gone, Mograine never injected (D13: never pull him).
    EXPECT_EQ(Find(out, 4542), nullptr);
    EXPECT_EQ(Find(out, 3976), nullptr);

    // Whitemane re-anchored at her vigil on the crossing, on her own bit 6.
    DungeonBossInfo const* whitemane = Find(out, 3977);
    ASSERT_NE(whitemane, nullptr);
    EXPECT_EQ(whitemane->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(whitemane->encounterIndex, 6u);
    EXPECT_NEAR(whitemane->x, 1146.0f, 0.01f);
    EXPECT_NEAR(whitemane->y, 1398.0f, 0.01f);

    // The Last Stand: an objective carrying event 2, done on slot 5 DONE, ahead of
    // Whitemane in the clear order.
    DungeonBossInfo const* stand = Find(out, BossRosterRegistry::ObjectiveEntry(1));
    ASSERT_NE(stand, nullptr);
    EXPECT_EQ(stand->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(stand->eventId, 2u);
    EXPECT_EQ(stand->doneBossStateIndex, 5);
    EXPECT_LT(BossOrderKey(*stand), BossOrderKey(*whitemane));
    ASSERT_NE(DungeonEventRegistry::Find(189, stand->eventId), nullptr);

    // Herod survives (reordered behind the Armory's new bosses, same bit).
    DungeonBossInfo const* herod = Find(out, 3975);
    ASSERT_NE(herod, nullptr);
    EXPECT_EQ(herod->encounterIndex, 4u);
}

// --- Apply: Scholomance merges Marduk & Vectus into one boss --------------

TEST(BossRosterRegistryTest, ScholomanceMergesMardukAndVectus)
{
    // The auto-derived list carries Vectus (10432) and Marduk (10433) as two
    // separate encounters. Plus an untouched Scholomance boss to prove the patch
    // only touches the entries it names.
    std::vector<DungeonBossInfo> base = {
        Boss(10506, 0, "Kirtonos the Herald", 289),  // untouched
        Boss(10432, 3, "Vectus", 289),
        Boss(10433, 4, "Marduk Blackpool", 289),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(289, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    // Both originals collapse: Marduk is gone entirely and Vectus's entry is
    // re-added as the single merged anchor.
    EXPECT_EQ(Find(out, 10433), nullptr);

    DungeonBossInfo const* merged = Find(out, 10432);
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(merged->name, "Marduk & Vectus");
    // Anchored on Vectus's spawn (the boss nearest the close room trash).
    EXPECT_GT(merged->x, 140.0f);
    EXPECT_LT(merged->x, 150.0f);
    // Inherits Vectus's own kill-bit; the authoring field is consumed.
    EXPECT_EQ(merged->encounterIndex, 3u);
    EXPECT_EQ(merged->inheritCompletionFrom, 0u);

    // Exactly one anchor remains for the pair (no duplicate 10432).
    int merged10432 = 0;
    for (DungeonBossInfo const& b : out)
        if (b.entry == 10432)
            ++merged10432;
    EXPECT_EQ(merged10432, 1);

    EXPECT_NE(Find(out, 10506), nullptr);
}

// The REAL derived Scholomance roster (DungeonEncounter.dbc bits for map 289, the
// twelve encounters BossSpawnIndex finds a spawn for): Kirtonos (bit 0, 10506) is
// summoned by the instance script when a player clicks the Brazier of the Herald,
// so he has no `creature` row and is never in the derived list. The patch leaves
// him out by design, merges the linked pair into one anchor on Vectus's bit (4),
// and keeps Gandling (bit 12) last, behind the six instructors (bits 6-11): the
// reworked instance reveals him on the sixth instructor state, so in clear order he
// is never the target while still hidden.
TEST(BossRosterRegistryTest, ScholomanceRealRosterEndsOnGandlingAndSkipsKirtonos)
{
    std::vector<DungeonBossInfo> base = {
        Boss(10503, 1, "Jandice Barov", 289),
        Boss(11622, 2, "Rattlegore", 289),
        Boss(10433, 3, "Marduk Blackpool", 289),
        Boss(10432, 4, "Vectus", 289),
        Boss(10508, 5, "Ras Frostwhisper", 289),
        Boss(10505, 6, "Instructor Malicia", 289),
        Boss(11261, 7, "Doctor Theolen Krastinov", 289),
        Boss(10901, 8, "Lorekeeper Polkelt", 289),
        Boss(10507, 9, "The Ravenian", 289),
        Boss(10504, 10, "Lord Alexei Barov", 289),
        Boss(10502, 11, "Lady Illucia Barov", 289),
        Boss(1853, 12, "Darkmaster Gandling", 289),
    };

    for (Difficulty d : { DUNGEON_DIFFICULTY_NORMAL, DUNGEON_DIFFICULTY_HEROIC })
    {
        std::vector<DungeonBossInfo> out =
            BossRosterRegistry::Apply(289, DcDiffKey::Dungeon(d), base);

        // Twelve derived encounters, the pair merged: eleven anchors.
        EXPECT_EQ(out.size(), 11u);
        EXPECT_EQ(Find(out, 10506), nullptr);   // Kirtonos: not in the run
        EXPECT_EQ(Find(out, 10433), nullptr);   // Marduk: folded into the pair

        DungeonBossInfo const* pair = Find(out, 10432);
        ASSERT_NE(pair, nullptr);
        EXPECT_EQ(pair->encounterIndex, 4u);    // Vectus's real kill-bit

        // Gandling is last, and the six instructors sit directly before him.
        ASSERT_FALSE(out.empty());
        EXPECT_EQ(out.back().entry, 1853u);
        EXPECT_EQ(out[out.size() - 2].entry, 10502u);   // Illucia, the last instructor
        for (DungeonBossInfo const& b : out)
            if (b.entry != 1853u)
                EXPECT_LT(BossOrderKey(b), BossOrderKey(out.back())) << b.entry;
    }
}

TEST(BossRosterRegistryTest, ResultStaysClearOrdered)
{
    std::vector<DungeonBossInfo> base = {
        Boss(3975, 4, "Herod", 189),
        Boss(3977, 6, "High Inquisitor Whitemane", 189),
        Boss(300940, 11, "Forgemaster Gideon Crell", 189),
        Boss(300946, 13, "Brother Aldous Thorne", 189),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(189, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    // The result is sorted by clear ORDER key (orderOverride when set, else
    // encounterIndex), NOT by raw encounterIndex — Crell's and Thorne's keys 1
    // deliberately place them (kill-bits 11 / 13) ahead of Herod and Whitemane.
    ASSERT_FALSE(out.empty());
    for (size_t i = 1; i < out.size(); ++i)
        EXPECT_LE(BossOrderKey(out[i - 1]), BossOrderKey(out[i]))
            << "roster not clear-ordered at index " << i;
}

// inheritCompletionFrom must resolve against the PRE-removal base, so removing
// the source entry and inheriting from it in the same patch both work.
TEST(BossRosterRegistryTest, InheritResolvesBeforeRemoval)
{
    // Azjol-Nerub's Hadronox is removed and re-added on her platform inheriting
    // from her own entry — the inherited index must still be the one 28921
    // carried in `base`.
    std::vector<DungeonBossInfo> base = {
        Boss(28921, 9, "Hadronox", 601),  // non-default idx
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(601, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    DungeonBossInfo const* hadronox = Find(out, 28921);
    ASSERT_NE(hadronox, nullptr);
    EXPECT_EQ(hadronox->encounterIndex, 9u);
    EXPECT_EQ(hadronox->inheritCompletionFrom, 0u);
    EXPECT_EQ(std::count_if(out.begin(), out.end(), [](DungeonBossInfo const& b) { return b.entry == 28921; }),
              1);
}

// Wailing Caverns: the Mutanus finale. The Disciple/escort OBJECTIVE (eventId 2)
// and the summoned Mutanus BOSS both key at 7 (after Verdan's bit 6); the
// objective-before-boss tie-break puts the escort FIRST, then Mutanus. Mutanus is
// a TempSummon BossSpawnIndex can't emit, so he is added with his real
// DungeonEncounter bit 7 (instance_encounters credit 3654) so his kill completes
// the dungeon.
TEST(BossRosterRegistryTest, WailingCavernsEscortObjectiveSortsBeforeMutanus)
{
    // Auto-derived static bosses (DBC encounterIndex order), bits 0-6.
    std::vector<DungeonBossInfo> base = {
        Boss(3671, 0, "Lady Anacondra", 43),
        Boss(3669, 1, "Lord Cobrahn", 43),
        Boss(3653, 2, "Kresh", 43),
        Boss(3670, 3, "Lord Pythas", 43),
        Boss(3674, 4, "Skum", 43),
        Boss(3673, 5, "Lord Serpentis", 43),
        Boss(5775, 6, "Verdan the Everliving", 43),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(43, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    DungeonBossInfo const* escort = nullptr;
    DungeonBossInfo const* drop = nullptr;
    DungeonBossInfo const* mutanus = Find(out, 3654);
    int escortIdx = -1, dropIdx = -1, mutanusIdx = -1, verdanIdx = -1;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2)
        {
            escort = &out[i];
            escortIdx = static_cast<int>(i);
        }
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 3)
        {
            drop = &out[i];
            dropIdx = static_cast<int>(i);
        }
        if (out[i].entry == 3654) mutanusIdx = static_cast<int>(i);
        if (out[i].entry == 5775) verdanIdx = static_cast<int>(i);
    }

    ASSERT_NE(escort, nullptr);
    ASSERT_NE(drop, nullptr);
    ASSERT_NE(mutanus, nullptr);

    // Mutanus carries his real kill-bit 7 and is a combat boss.
    EXPECT_EQ(mutanus->encounterIndex, 7u);
    EXPECT_EQ(mutanus->kind, DungeonAnchorKind::Boss);

    // The escort objective wires event 2 and arrives generously (among the
    // Disciple, who the gossip then closes the rest of the way to).
    EXPECT_EQ(escort->encounterIndex, 7u);
    EXPECT_FLOAT_EQ(escort->arriveRadius, 18.0f);

    // The return-fall objective wires event 3 and also keys at 7 (an objective's
    // index is an ordering hint; it latches by entry, so sharing Mutanus's bit is
    // harmless). It sorts BEFORE the escort by insertion order (stable_sort).
    EXPECT_EQ(drop->encounterIndex, 7u);
    EXPECT_EQ(drop->kind, DungeonAnchorKind::Objective);

    // Order: Verdan (6) -> hole-drop (7, obj) -> escort (7, obj) -> Mutanus (7, boss).
    EXPECT_LT(verdanIdx, dropIdx);
    EXPECT_LT(dropIdx, escortIdx);
    EXPECT_LT(escortIdx, mutanusIdx);
}

// Sethekk Halls (556): the forced-Anzu objective and its room pre-clear.
//
// REGRESSION GUARD for a live heroic failure: the tank walked to the Anzu summon
// statue, poked the summon into a completely untouched room, and the freshly
// summoned Anzu (SetInCombatWithZone) then chased the party the length of the
// hall and on into Ikiss's chamber, so they fought Anzu, Ikiss and the uncleared
// NE/NW packs together.
//
// The cause was the anchor's arriveRadius, not the sweep volume. DcRel::
// AtObjective (30) outranks DcRel::BlockingTrash (25), so crossing the arrive
// radius hands the tick to the objective action for good. At 55yd that happened
// a whole room-length from the statue: the ordinary corridor trash-clear was
// suppressed for the entire approach, and the ClearRadius gate was then judged
// (and the summon poked) from ~80yd out, where the volume's STRICT per-candidate
// reachability probe cannot confirm a route to anything and the gate answers
// "clear" over a room full of live trash.
//
// Both halves are pinned here:
//   * arriveRadius must stay SMALL — the walk in belongs to the corridor clear
//     and the pull pipeline, and the event may only take over once the tank is
//     actually at the statue. It must still exceed step 0's MoveTo radius so that
//     step completes on arrival and IsPersistentAnchoredEventActive latches the
//     trigger on, which is what frees the sweep to range the full clear radius.
//   * the ClearRadius volume must cover the WHOLE room — see the companion test
//     SethekkAnzuSweepSpansTheWholeAnteChamber, which pins it against the actual
//     map-556 spawn coordinates rather than against a radius number.
TEST(BossRosterRegistryTest, SethekkAnzuObjectiveHandsOverOnlyAtTheStatue)
{
    std::vector<DungeonBossInfo> base = {
        Boss(18472, 0, "Darkweaver Syth", 556),
        Boss(18473, 2, "Talon King Ikiss", 556),
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(556, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_HEROIC), base);

    DungeonBossInfo const* anzu = Find(out, BossRosterRegistry::ObjectiveEntry(1));
    ASSERT_NE(anzu, nullptr) << "heroic Sethekk must carry the Anzu objective anchor";
    EXPECT_EQ(anzu->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(anzu->eventId, 1u);

    DungeonEvent const* ev = DungeonEventRegistry::Find(556, anzu->eventId);
    ASSERT_NE(ev, nullptr);

    float moveRadius = -1.0f;
    float clearRadius = -1.0f;
    for (EventStep const& s : ev->steps)
    {
        if (s.kind == EventStepKind::MoveTo && moveRadius < 0.0f)
            moveRadius = s.radius;
        if (s.kind == EventStepKind::ClearRadius)
            clearRadius = s.radius;
    }
    ASSERT_GT(moveRadius, 0.0f) << "step 0 must park the tank on the anchor";
    ASSERT_GT(clearRadius, 0.0f) << "the event must carry a ClearRadius sweep";

    EXPECT_LE(anzu->arriveRadius, 15.0f)
        << "arriveRadius too large -> the objective seizes the tick a room-length "
           "out, suppressing the corridor trash-clear and letting the pre-clear be "
           "judged (and the summon poked) from outside the room";
    EXPECT_GT(anzu->arriveRadius, moveRadius)
        << "arriveRadius must exceed the MoveTo radius so step 0 completes on "
           "arrival and the persistent at-objective latch engages";

    EXPECT_GT(clearRadius, 0.0f) << "the event must carry a ClearRadius sweep";
}

// Sethekk Halls (556): the Anzu pre-clear must sweep the WHOLE ante-chamber.
//
// REGRESSION GUARD for heroic run tr-20260726-112544-3 (tank Zeeron). By then the
// vantage-point half of the earlier fix was working — the gate certified from
// botDistToCentre=0.0, and everything inside the old 60yd volume really was dead
// — but the summon still fired into a room with five elites standing in it. The
// old geometry note called them "the Ikiss-corridor packs at 73-87yd" and
// excluded them on purpose. They are not in Ikiss's corridor: Ikiss stands at
// (44.7,287), a further 45-60yd on with nothing in between. They are the last
// pack of THIS room, on the same flat floor, in a hall with no doors in it — and
// the pull pipeline never reaches them either, because the tank stops at the
// statue. So nothing cleared them before the poke; the party met them after Anzu
// died (run ...-4 shows the same as a post-Anzu pull of entry 21904, 5 observed).
//
// The fix re-centres the sweep on the ROOM instead of the statue, which sits 53yd
// from the room's south end and 87yd from its north end. This test pins the
// INTENT against the real map-556 spawn coordinates rather than a radius number:
// both ends of the room are inside the volume, and the two things that are NOT
// this room's problem — the Syth-approach pack behind the party and Ikiss ahead
// of it — stay outside.
TEST(BossRosterRegistryTest, SethekkAnzuSweepSpansTheWholeAnteChamber)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(556, 1);
    ASSERT_NE(ev, nullptr);

    EventStep const* clear = nullptr;
    for (EventStep const& s : ev->steps)
        if (s.kind == EventStepKind::ClearRadius)
            clear = &s;
    ASSERT_NE(clear, nullptr) << "the Anzu event must carry a ClearRadius sweep";

    auto covers = [&](float x, float y)
    {
        float const dx = x - clear->x;
        float const dy = y - clear->y;
        return std::sqrt(dx * dx + dy * dy) <= clear->radius;
    };

    // Both ends of the ante-chamber. South: the doorway trio (Sethekk Ravenguard
    // 18322 guid 138666). North: the landing pack at the mouth of Ikiss's chamber
    // (Sethekk Prophet 18325 guid 138688 is its far member, and the Avian Warhawk
    // 21904 guid 138757 at its near end PATROLS).
    EXPECT_TRUE(covers(-141.7f, 283.0f))
        << "sweep must reach the south-doorway trio (Ravenguards + Cobalt Serpent)";
    EXPECT_TRUE(covers(-1.2f, 289.9f))
        << "sweep must reach the north-landing pack — this is the pack that was "
           "left standing when the summon fired; it is part of the room, not of "
           "the walk on to Ikiss";
    EXPECT_TRUE(covers(-14.9f, 293.1f))
        << "sweep must reach the north landing's patrolling Avian Warhawk";

    // ...and no further. The Time-Lost Scryer (18319 guid 138648) is on the Syth
    // approach BEHIND the party, dead on the way in; Talon King Ikiss is the next
    // encounter and belongs to boss-nav, not to a trash sweep.
    EXPECT_FALSE(covers(-171.1f, 282.3f))
        << "sweep must not reach back down the Syth approach";
    EXPECT_FALSE(covers(44.7f, 287.0f))
        << "sweep must stop well short of Talon King Ikiss";

    // Margin for the two patrollers at the volume's edges: the room's far corners
    // sit ~70yd out, so a radius that only just reaches them would drop a mob that
    // happened to be mid-patrol at judging time.
    EXPECT_GE(clear->radius, 78.0f) << "no patrol margin on the room's far ends";

    // A sweep that must walk the hall and fight what it finds cannot live on the
    // 30s EventStepTimeout default: a timed-out step is Failed, and this event is
    // Optional, so Failed skips the rest of the event — dropping the summon on
    // exactly the runs where the pre-clear was needed.
    EXPECT_GE(clear->timeoutMs, 120000u)
        << "ClearRadius needs an explicit long timeout, or an uncleared room "
           "silently skips the Anzu summon";

    // The sweep leaves the tank at the room's centre; Anzu lands at the statue.
    // A MoveTo must gather the party back before the summon step pokes.
    size_t clearIdx = ev->steps.size();
    size_t settleIdx = ev->steps.size();
    size_t customIdx = ev->steps.size();
    for (size_t i = 0; i < ev->steps.size(); ++i)
    {
        if (ev->steps[i].kind == EventStepKind::ClearRadius)
            clearIdx = i;
        else if (ev->steps[i].kind == EventStepKind::MoveTo && clearIdx < i &&
                 settleIdx == ev->steps.size())
            settleIdx = i;
        else if (ev->steps[i].kind == EventStepKind::Custom &&
                 customIdx == ev->steps.size())
            customIdx = i;
    }
    ASSERT_LT(settleIdx, ev->steps.size())
        << "no MoveTo after the sweep — the party would sit through the ~40s "
           "theatrics spread down the hall wherever the last straggler died";
    EXPECT_LT(settleIdx, customIdx) << "the re-settle must precede the summon poke";
    EXPECT_NEAR(ev->steps[settleIdx].x, -88.0f, 1.0f);
    EXPECT_NEAR(ev->steps[settleIdx].y, 288.0f, 1.0f);
}

// --- Apply: Maraudon drops Rotgrip ---------------------------------------

// Rotgrip lives in the Pristine Waters lake — open water the party cannot be
// navigated to — so he is removed from the clear entirely. The rest of the
// Maraudon roster must survive the patch untouched and stay in clear order.
TEST(BossRosterRegistryTest, MaraudonDropsRotgrip)
{
    // The auto-derived Maraudon list as BossSpawnIndex emits it (DBC bits).
    std::vector<DungeonBossInfo> base = {
        Boss(13282, 0, "Noxxion", 349),
        Boss(12258, 1, "Razorlash", 349),
        Boss(12236, 2, "Lord Vyletongue", 349),
        Boss(12225, 3, "Celebras the Cursed", 349),
        Boss(12203, 4, "Landslide", 349),
        Boss(13601, 5, "Tinkerer Gizlock", 349),
        Boss(13596, 6, "Rotgrip", 349),
        Boss(12201, 7, "Princess Theradras", 349),
    };

    std::vector<DungeonBossInfo> const out =
        BossRosterRegistry::Apply(349, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    EXPECT_TRUE(BossRosterRegistry::HasPatch(349));
    EXPECT_EQ(Find(out, 13596), nullptr)
        << "Rotgrip is unreachable (open water) — he must not be in the clear list";
    ASSERT_EQ(out.size(), base.size() - 1);

    // Everything else survives, in DBC-bit order — the patch removes only.
    uint32 const expected[] = {13282, 12258, 12236, 12225, 12203, 13601, 12201};
    for (size_t i = 0; i < out.size(); ++i)
        EXPECT_EQ(out[i].entry, expected[i]) << "clear order changed at slot " << i;

    // Princess Theradras (the real end boss, one bit after Rotgrip) must keep
    // her own kill-bit: removing Rotgrip must not renumber anything.
    DungeonBossInfo const* princess = Find(out, 12201);
    ASSERT_NE(princess, nullptr);
    EXPECT_EQ(princess->encounterIndex, 7u);
}

// --- Apply: Dire Maul North drops Cho'Rush -------------------------------

// Cho'Rush the Observer carries a real DungeonEncounter row but his SmartAI
// sets faction 35 (friendly to all) 5s after spawn and never reverts, so his
// kill-bit can never be set. Left in, he is the anchor the North clear parks on
// forever (tr-20260816-061103-16 idled on him for 40 minutes after King Gordok
// died). The rest of the North roster must survive untouched and in order.
TEST(BossRosterRegistryTest, DireMaulNorthDropsChoRush)
{
    // The auto-derived North list as BossSpawnIndex emits it (DBC bits).
    std::vector<DungeonBossInfo> base = {
        Boss(14326, 1, "Guard Mol'dar", 429),
        Boss(14322, 2, "Stomper Kreeg", 429),
        Boss(14321, 3, "Guard Fengus", 429),
        Boss(14323, 4, "Guard Slip'kik", 429),
        Boss(14325, 5, "Captain Kromcrush", 429),
        Boss(14324, 6, "Cho'Rush the Observer", 429),
        Boss(11501, 7, "King Gordok", 429),
    };

    std::vector<DungeonBossInfo> const out =
        BossRosterRegistry::Apply(429, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    EXPECT_EQ(Find(out, 14324), nullptr)
        << "Cho'Rush is permanently friendly — he must not be in the clear list";

    // The North bosses that remain keep their DBC order. (Dire Maul shares ONE
    // map-429 patch across wings, so Apply() also appends the East/West
    // objectives — filter to real creatures before checking the order.)
    std::vector<uint32> kept;
    for (DungeonBossInfo const& b : out)
        if (b.kind != DungeonAnchorKind::Objective)
            kept.push_back(b.entry);

    std::vector<uint32> const expected = {14326, 14322, 14321, 14323, 14325, 11501};
    EXPECT_EQ(kept, expected) << "North clear order changed";

    // King Gordok (the real end boss, one bit after Cho'Rush) keeps his own
    // kill-bit: removing Cho'Rush must not renumber anything.
    DungeonBossInfo const* gordok = Find(out, 11501);
    ASSERT_NE(gordok, nullptr);
    EXPECT_EQ(gordok->encounterIndex, 7u);
}

// Utgarde Keep (574): the three forge objectives must sort AHEAD of all three
// bosses, in west->east->north order, and the bosses must keep their real DBC
// kill-bits. The reorder is the whole risk here — encounterIndex is what the
// completion mask is read against, so a patch that moved the bits instead of the
// order keys would silently un-complete the dungeon.
TEST(BossRosterRegistryTest, UtgardeKeepForgesSortAheadOfEveryBoss)
{
    std::vector<DungeonBossInfo> base = {
        Boss(23953, 0, "Prince Keleseth", 574),
        Boss(24201, 1, "Skarvold & Dalronn", 574),
        Boss(23954, 2, "Ingvar the Plunderer", 574),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(574, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    ASSERT_EQ(out.size(), 6u) << "3 bosses + 3 forge objectives";

    // Positions in clear order.
    int forge[3] = { -1, -1, -1 };
    int keleseth = -1, dalronn = -1, ingvar = -1;
    int objectivesSeen = 0;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].kind == DungeonAnchorKind::Objective)
        {
            ASSERT_LT(objectivesSeen, 3);
            forge[objectivesSeen++] = i;
        }
        if (out[i].entry == 23953)
            keleseth = i;
        if (out[i].entry == 24201)
            dalronn = i;
        if (out[i].entry == 23954)
            ingvar = i;
    }
    ASSERT_EQ(objectivesSeen, 3) << "three forge objectives expected";
    ASSERT_GE(keleseth, 0);
    ASSERT_GE(dalronn, 0);
    ASSERT_GE(ingvar, 0);

    // Every forge before every boss, and the bosses in unchanged relative order.
    EXPECT_LT(forge[2], keleseth) << "all three forges are done before Keleseth";
    EXPECT_LT(keleseth, dalronn);
    EXPECT_LT(dalronn, ingvar);

    // The forges themselves are in script order: forge 1 (349.6,-39.3) then
    // forge 2 (385.8,-16.2) then forge 3 (347.6,4.6). Engaging one out of order
    // makes it evade, so this sequence IS the feature.
    EXPECT_EQ(out[forge[0]].eventId, 1u);
    EXPECT_EQ(out[forge[1]].eventId, 2u);
    EXPECT_EQ(out[forge[2]].eventId, 3u);
    EXPECT_NEAR(out[forge[0]].x, 349.6f, 0.5f);
    EXPECT_NEAR(out[forge[1]].x, 385.8f, 0.5f);
    EXPECT_NEAR(out[forge[2]].x, 347.6f, 0.5f);

    // The real kill-bits survive the reorder untouched — the clear orders by
    // orderOverride, completion still keys on encounterIndex.
    EXPECT_EQ(Find(out, 23953)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 24201)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 23954)->encounterIndex, 2u);
    EXPECT_EQ(Find(out, 23953)->orderOverride, 4);
    EXPECT_EQ(Find(out, 24201)->orderOverride, 5);
    EXPECT_EQ(Find(out, 23954)->orderOverride, 6);

    // No boss was removed or re-added: they keep the auto-derived spawn coords.
    for (DungeonBossInfo const& b : out)
        if (b.kind == DungeonAnchorKind::Boss)
            EXPECT_EQ(b.inheritCompletionFrom, 0u)
                << "Utgarde Keep needs no boss surgery — the derived list is correct";
}

TEST(BossRosterRegistryTest, NexusSpheresSortBetweenOrmorokAndKeristrasza)
{
    // Derived roster, normal difficulty (DungeonEncounter.dbc bits 0-3).
    std::vector<DungeonBossInfo> base = {
        Boss(26731, 0, "Grand Magus Telestra", 576),
        Boss(26763, 1, "Anomalus", 576),
        Boss(26794, 2, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 3, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    ASSERT_EQ(out.size(), 7u) << "4 bosses + 3 sphere objectives";

    int sphere[3] = { -1, -1, -1 };
    int telestra = -1, anomalus = -1, ormorok = -1, keristrasza = -1;
    int objectivesSeen = 0;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].kind == DungeonAnchorKind::Objective)
        {
            ASSERT_LT(objectivesSeen, 3);
            sphere[objectivesSeen++] = i;
        }
        if (out[i].entry == 26731)
            telestra = i;
        if (out[i].entry == 26763)
            anomalus = i;
        if (out[i].entry == 26794)
            ormorok = i;
        if (out[i].entry == 26723)
            keristrasza = i;
    }
    ASSERT_EQ(objectivesSeen, 3) << "three sphere objectives expected";
    ASSERT_GE(telestra, 0);
    ASSERT_GE(anomalus, 0);
    ASSERT_GE(ormorok, 0);
    ASSERT_GE(keristrasza, 0);

    // A sphere is only clickable once its own boss is dead (instance_nexus clears
    // GO_FLAG_NOT_SELECTABLE in SetBossState), and all three must be clicked
    // before Keristrasza can be attacked at all.
    EXPECT_LT(telestra, anomalus);
    EXPECT_LT(anomalus, ormorok);
    EXPECT_LT(ormorok, sphere[0]) << "every orb boss dies before the first click";
    EXPECT_LT(sphere[2], keristrasza) << "all three clicks land before Keristrasza";

    // Walk order across the hub: Telestra's sphere (south), Ormorok's (north-west),
    // Anomalus' (north-east) — 81yd rather than the 98yd of any other order.
    EXPECT_EQ(out[sphere[0]].eventId, 1u);
    EXPECT_EQ(out[sphere[1]].eventId, 2u);
    EXPECT_EQ(out[sphere[2]].eventId, 3u);
    EXPECT_NEAR(out[sphere[0]].x, 281.9f, 0.5f);
    EXPECT_NEAR(out[sphere[0]].y, -25.5f, 0.5f);
    EXPECT_NEAR(out[sphere[1]].x, 281.8f, 0.5f);
    EXPECT_NEAR(out[sphere[1]].y, 15.2f, 0.5f);
    EXPECT_NEAR(out[sphere[2]].x, 322.2f, 0.5f);
    EXPECT_NEAR(out[sphere[2]].y, 14.7f, 0.5f);

    // The real kill-bits survive the reorder untouched.
    EXPECT_EQ(Find(out, 26731)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 26763)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 26794)->encounterIndex, 2u);
    EXPECT_EQ(Find(out, 26723)->encounterIndex, 3u);

    // No boss surgery — the derived list is already in travel order.
    for (DungeonBossInfo const& b : out)
        if (b.kind == DungeonAnchorKind::Boss)
            EXPECT_EQ(b.inheritCompletionFrom, 0u);
}

TEST(BossRosterRegistryTest, NexusHeroicCommanderStaysFirst)
{
    // Heroic shifts every DBC bit up by one to make room for the Frozen Commander
    // at bit 0. The HeroicOnly patch re-adds him on order key 1, which must still
    // sort ahead of the 2..8 scale the rest of the map is reordered onto.
    std::vector<DungeonBossInfo> base = {
        Boss(26796, 0, "Frozen Commander", 576),
        Boss(26731, 1, "Grand Magus Telestra", 576),
        Boss(26763, 2, "Anomalus", 576),
        Boss(26794, 3, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 4, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_HEROIC), base);

    ASSERT_EQ(out.size(), 8u) << "5 bosses + 3 sphere objectives";
    EXPECT_EQ(out.front().entry, 26796u) << "the Frozen Commander still leads the clear";
    EXPECT_EQ(out.back().entry, 26723u) << "Keristrasza is still last";
    EXPECT_EQ(Find(out, 26796)->orderOverride, 1)
        << "the Commander sits on key 1, between bit 0 and the 2..8 scale";

    // The three spheres are still the last thing before Keristrasza.
    ASSERT_GE(out.size(), 4u);
    for (size_t i = out.size() - 4; i + 1 < out.size(); ++i)
        EXPECT_EQ(out[i].kind, DungeonAnchorKind::Objective)
            << "index " << i << " should be a sphere objective";
}

// The Frozen Commander's DBC kill-bit is UNUSABLE: instance_encounters has
// PRIMARY KEY (`entry`), so encounter 519 can only credit one creature (26796),
// and an Alliance party kills the UpdateEntry'd 26798 — bit 0 never flips. The
// heroic patch must therefore re-add him reading the instance script's own
// DATA_COMMANDER_EVENT slot, with encounterIndex parked out of mask range so the
// dead bit is never consulted on EITHER faction.
TEST(BossRosterRegistryTest, NexusHeroicCommanderCompletesViaBossState)
{
    std::vector<DungeonBossInfo> base = {
        Boss(26796, 0, "Frozen Commander", 576),
        Boss(26731, 1, "Grand Magus Telestra", 576),
        Boss(26763, 2, "Anomalus", 576),
        Boss(26794, 3, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 4, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_HEROIC), base);

    DungeonBossInfo const* cmd = Find(out, 26796);
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(cmd->doneBossStateIndex, 4) << "DATA_COMMANDER_EVENT from nexus.h";
    EXPECT_GE(cmd->encounterIndex, 32u)
        << "parked out of the completed-encounter mask's range so bit 0 is never read";

    // The re-add restates the spawn coords; losing them would send the tank to
    // (0,0,0). The floor here is the lower ring (-34.9), not the hub's -16.
    EXPECT_NEAR(cmd->x, 424.5f, 0.5f);
    EXPECT_NEAR(cmd->y, 186.0f, 0.5f);
    EXPECT_NEAR(cmd->z, -34.9f, 0.5f);

    // No other Nexus anchor borrows a boss-state slot — Keristrasza in particular
    // owns heroic bit 4, the same NUMBER as DATA_COMMANDER_EVENT in the instance
    // script's unrelated index space.
    for (DungeonBossInfo const& b : out)
        if (b.entry != 26796u)
            EXPECT_EQ(b.doneBossStateIndex, -1) << "entry " << b.entry;
}

// The commander is heroic-only (spawnMask 2, DBC difficulty 1), so BossSpawnIndex
// never puts him in the normal bucket. The HeroicOnly gate must keep the patch off
// a normal run entirely — a stray re-add there would invent an anchor at a spawn
// that does not exist on that difficulty.
TEST(BossRosterRegistryTest, NexusNormalHasNoFrozenCommander)
{
    std::vector<DungeonBossInfo> base = {
        Boss(26731, 0, "Grand Magus Telestra", 576),
        Boss(26763, 1, "Anomalus", 576),
        Boss(26794, 2, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 3, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    ASSERT_EQ(out.size(), 7u) << "4 bosses + 3 sphere objectives";
    EXPECT_EQ(Find(out, 26796), nullptr) << "no Frozen Commander on normal";
    EXPECT_EQ(Find(out, 26798), nullptr) << "nor his Alliance-side entry";
    for (DungeonBossInfo const& b : out)
        EXPECT_EQ(b.doneBossStateIndex, -1) << "entry " << b.entry;
}

// Azjol-Nerub (601): Hadronox is re-anchored from her spawn ledge onto the
// platform her own script climbs to, and the two objectives must bracket her —
// the crusher/web hold BEFORE her, the drop AFTER. She is the only boss on the
// clear that is remove+re-added purely to move where the fight happens, so the
// risk is the usual one: a patch that moved her kill-bit instead of her order
// key would silently un-complete the dungeon.
TEST(BossRosterRegistryTest, AzjolNerubBracketsHadronoxWithTheWebHoldAndTheDrop)
{
    std::vector<DungeonBossInfo> base = {
        Boss(28684, 0, "Krik'thir the Gatewatcher", 601),
        Boss(28921, 1, "Hadronox", 601),
        Boss(29120, 2, "Anub'arak", 601),
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(601, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    ASSERT_EQ(out.size(), 5u) << "3 bosses + the web hold + the drop";

    int krikthir = -1, hadronox = -1, anubarak = -1, webHold = -1, drop = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 28684) krikthir = i;
        if (out[i].entry == 28921) hadronox = i;
        if (out[i].entry == 29120) anubarak = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 1) webHold = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2) drop = i;
    }
    ASSERT_GE(krikthir, 0);
    ASSERT_GE(hadronox, 0);
    ASSERT_GE(anubarak, 0);
    ASSERT_GE(webHold, 0) << "the 'web the doors' objective (eventId 1) is missing";
    ASSERT_GE(drop, 0) << "the drop objective (eventId 2) is missing";

    // Krik'thir -> web hold -> Hadronox -> drop -> Anub'arak.
    EXPECT_LT(krikthir, webHold);
    EXPECT_LT(webHold, hadronox)
        << "the swarm's off-switch has to be thrown BEFORE she is handed to boss nav";
    EXPECT_LT(hadronox, drop);
    EXPECT_LT(drop, anubarak)
        << "the lower kingdom is across a hard navmesh break — the drop must come first";

    // Hadronox now stands on the platform (her own MOVE3 destination), NOT on her
    // spawn ledge at (522.5, 544.9, 674.7) 60yd below it.
    EXPECT_NEAR(out[hadronox].x, 530.4f, 1.0f);
    EXPECT_NEAR(out[hadronox].y, 560.0f, 1.0f);
    EXPECT_GT(out[hadronox].z, 700.0f)
        << "anchoring her on the z~675 spawn ledge walks the party into the add funnel";

    // The web hold shares the platform with her; the drop sits on the pit floor.
    EXPECT_NEAR(out[webHold].x, 530.4f, 1.0f);
    EXPECT_NEAR(out[webHold].z, 733.8f, 1.0f);
    EXPECT_NEAR(out[drop].x, 522.0f, 1.0f);
    EXPECT_NEAR(out[drop].z, 648.9f, 1.0f);

    // Kill-bits untouched: the clear orders by orderOverride, completion still
    // keys on encounterIndex, and Hadronox keeps HER OWN bit across remove+re-add.
    EXPECT_EQ(Find(out, 28684)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 28921)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 29120)->encounterIndex, 2u);
}


// --- Drak'Tharon Keep (map 600) -------------------------------------------

// THE headline fix. instance_encounters rows 375/376 give The Prophet Tharon'ja
// creditType 1 (ENCOUNTER_CREDIT_CAST_SPELL, spell 61863), and
// BossSpawnIndex::Build skips every encounter that is not
// ENCOUNTER_CREDIT_KILL_CREATURE — so he is absent from the derived list, the
// candidate picker empties after King Dred, AllClearedTrigger fires, and every
// run on this map ends 3/4. The base list below is exactly what the derived
// roster produces today.
TEST(BossRosterRegistryTest, DrakTharonAddsTheCastSpellCreditBoss)
{
    std::vector<DungeonBossInfo> base = {
        Boss(26630, 0, "Trollgore", 600),
        Boss(26631, 1, "Novos the Summoner", 600),
        Boss(27483, 2, "King Dred", 600),
        // 26632 The Prophet Tharon'ja is MISSING here on purpose — that is the bug.
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(600, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    ASSERT_EQ(out.size(), 4u) << "the fourth boss must be added on normal difficulty";

    // Clear order is the travel order, which is also the DBC order — 422 + 206 +
    // 354 + 643 = 1625yd by Dijkstra over the map-600 mmtiles.
    EXPECT_EQ(out[0].entry, 26630u) << "Trollgore";
    EXPECT_EQ(out[1].entry, 26631u) << "Novos the Summoner";
    EXPECT_EQ(out[2].entry, 27483u) << "King Dred";
    EXPECT_EQ(out[3].entry, 26632u) << "The Prophet Tharon'ja";

    DungeonBossInfo const* tharon = Find(out, 26632);
    ASSERT_NE(tharon, nullptr);
    EXPECT_EQ(tharon->kind, DungeonAnchorKind::Boss);

    // Completion rides the DBC mask, exactly like the other three: JustDied casts
    // 61863, ObjectMgr stamps SPELL_ATTR0_CU_ENCOUNTER_REWARD on it, Spell::finish
    // calls Map::UpdateEncounterState and bit 3 is set.
    EXPECT_EQ(tharon->encounterIndex, 3u)
        << "his real DungeonEncounter bit — the same mask the other three use";
    EXPECT_EQ(tharon->doneBossStateIndex, -1)
        << "NOT the instance boss-state path: that index space is SHIFTED on this "
           "map (DATA_NOVOS_CRYSTALS is a door pseudo-encounter at slot 2, so Dred's "
           "DBC bit 2 is his SetBossState slot 3) and mixing sources hides a "
           "divergence";
    EXPECT_EQ(tharon->inheritCompletionFrom, 0u)
        << "nothing to inherit from — he is missing from the base list, which is "
           "the whole reason this patch exists";

    // Hand-authored coordinates: his DB spawn, which is what BossSpawnIndex would
    // have produced had the credit type let it. Column-probed at (-236.8, -675.4):
    // one walkable surface at z 131.72, a 0.23yd delta NavmeshSnap absorbs.
    EXPECT_NEAR(tharon->x, -236.83f, 0.5f);
    EXPECT_NEAR(tharon->y, -675.41f, 0.5f);
    EXPECT_NEAR(tharon->z, 131.95f, 0.5f);

    // The other three are untouched on normal — no reorder is needed, because the
    // DBC order already matches the path.
    EXPECT_EQ(Find(out, 26630)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 26631)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 27483)->encounterIndex, 2u);
    for (uint32 entry : { 26630u, 26631u, 27483u })
        EXPECT_EQ(Find(out, entry)->orderOverride, -1)
            << entry << " should not have been reordered on normal difficulty";
}

// Heroic adds one objective and nothing else: the raptor pen, swept before Dred
// so Raptor Call (59416, heroic only) has nothing left to summon.
TEST(BossRosterRegistryTest, DrakTharonHeroicSweepsTheRaptorPenBeforeDred)
{
    std::vector<DungeonBossInfo> base = {
        Boss(26630, 0, "Trollgore", 600),
        Boss(26631, 1, "Novos the Summoner", 600),
        Boss(27483, 2, "King Dred", 600),
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(600, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_HEROIC), base);

    ASSERT_EQ(out.size(), 5u) << "4 bosses + the raptor pen objective";

    int trollgore = -1, novos = -1, pen = -1, dred = -1, tharon = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 26630) trollgore = i;
        if (out[i].entry == 26631) novos = i;
        if (out[i].entry == 27483) dred = i;
        if (out[i].entry == 26632) tharon = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2) pen = i;
    }
    ASSERT_GE(pen, 0) << "the raptor-pen objective (eventId 2) is missing on heroic";

    EXPECT_LT(trollgore, novos);
    EXPECT_LT(novos, pen);
    EXPECT_LT(pen, dred)
        << "the pool has to be emptied BEFORE the pull, or Raptor Call feeds the "
           "fight one extra level-74 elite every ~30s";
    EXPECT_LT(dred, tharon);

    // The anchor sits inside the pen but off Dred's spawn (-544.87, -696.97).
    // Column-probed at (-533, -692): ONE surface, z 30.63, nothing else in the
    // column — the pen is a single floor.
    EXPECT_NEAR(out[pen].x, -533.0f, 1.0f);
    EXPECT_NEAR(out[pen].y, -692.0f, 1.0f);
    EXPECT_NEAR(out[pen].z, 30.63f, 1.0f);

    // Kill-bits survive the reorder untouched — orderOverride moves the clear
    // sequence and nothing else.
    EXPECT_EQ(Find(out, 26630)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 26631)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 27483)->encounterIndex, 2u);
    EXPECT_EQ(Find(out, 26632)->encounterIndex, 3u);
}

// The pen sweep is heroic-only, and the normal roster must not carry a stray
// objective anchor for it (boss-nav would walk the party into an empty pen).
TEST(BossRosterRegistryTest, DrakTharonNormalHasNoRaptorPenObjective)
{
    std::vector<DungeonBossInfo> base = {
        Boss(26630, 0, "Trollgore", 600),
        Boss(26631, 1, "Novos the Summoner", 600),
        Boss(27483, 2, "King Dred", 600),
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(600, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), base);

    for (DungeonBossInfo const& b : out)
        EXPECT_NE(b.kind, DungeonAnchorKind::Objective)
            << "map 600 has no normal-difficulty objectives — Raptor Call is "
               "scheduled inside if (IsHeroic())";
}

// --- instance-script completion: doneBossStateIndex / doneInstanceData --------
// The rule both the target picker and the diag snapshot read an anchor's
// instance-side completion through (AnchorDoneByInstanceValues). EncounterState
// DONE is 3.

TEST(BossRosterDoneTest, AnchorWithNeitherSignalIsNeverDoneByInstance)
{
    DungeonBossInfo b;
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 3, 3), DcAnchorDoneVia::None)
        << "an anchor that configures no slot and no data value must fall through to the mask";
}

TEST(BossRosterDoneTest, BossStateSlotReadsDoneOnlyOnDone)
{
    DungeonBossInfo b;
    b.doneBossStateIndex = 4;
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 3, 0), DcAnchorDoneVia::BossState);
    for (uint32 state : { 0u, 1u, 2u, 4u, 5u })
        EXPECT_EQ(AnchorDoneByInstanceValues(b, state, 0), DcAnchorDoneVia::None) << state;
}

// Karazhan's chess: no boss state, no kill bit — GetData(DATA_CHESS_EVENT = 9)
// reads DONE (3) once Medivh's King falls, and SPECIAL (4) during the PvP game
// that follows, which must NOT read as the PvE win.
TEST(BossRosterDoneTest, InstanceDataValueIsExact)
{
    DungeonBossInfo b;
    b.doneInstanceDataId = 9;
    b.doneInstanceDataValue = 3;
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 0, 3), DcAnchorDoneVia::InstanceData);
    for (uint32 v : { 0u, 1u, 2u, 4u })
        EXPECT_EQ(AnchorDoneByInstanceValues(b, 0, v), DcAnchorDoneVia::None) << v;
    // Unconfigured, a data value of 0 must not match the default value 0.
    DungeonBossInfo none;
    none.doneInstanceDataValue = 0;
    EXPECT_EQ(AnchorDoneByInstanceValues(none, 0, 0), DcAnchorDoneVia::None);
}

TEST(BossRosterDoneTest, InstanceDataIsReadLiveNotLatched)
{
    // The rule is a pure function of the values handed in: an unsaved encounter
    // that reads NOT_STARTED again after a reload is simply not done again.
    DungeonBossInfo b;
    b.doneInstanceDataId = 9;
    b.doneInstanceDataValue = 3;
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 0, 3), DcAnchorDoneVia::InstanceData);
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 0, 0), DcAnchorDoneVia::None);
}

TEST(BossRosterDoneTest, EitherConfiguredSignalIsEnough)
{
    DungeonBossInfo b;
    b.doneBossStateIndex = 2;
    b.doneInstanceDataId = 9;
    b.doneInstanceDataValue = 3;
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 3, 0), DcAnchorDoneVia::BossState);
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 1, 3), DcAnchorDoneVia::InstanceData);
    EXPECT_EQ(AnchorDoneByInstanceValues(b, 1, 1), DcAnchorDoneVia::None);
}

TEST(BossRosterDoneTest, RosterPatchCarriesInstanceDataThroughApply)
{
    // Apply must copy the field with the row, not rebuild rows field by field.
    BossRosterPatch patch;
    patch.mapId = 532;
    DungeonBossInfo obj;
    obj.entry = BossRosterRegistry::ObjectiveEntry(3);
    obj.kind = DungeonAnchorKind::Objective;
    obj.mapId = 532;
    obj.doneInstanceDataId = 9;
    obj.doneInstanceDataValue = 3;
    patch.add.push_back(obj);

    std::vector<DungeonBossInfo> const out = BossRosterRegistry::ApplyPatch(patch, {});
    DungeonBossInfo const* o = Find(out, obj.entry);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->doneInstanceDataId, 9);
    EXPECT_EQ(o->doneInstanceDataValue, 3u);
    EXPECT_EQ(o->doneBossStateIndex, -1);
}

// --- Ragefire Chasm (389), the rework ---------------------------------------
namespace
{
    // BossSpawnIndex's derived list: the four kill credits by DBC bit, so
    // Taragaman is second.
    std::vector<DungeonBossInfo> RagefireDerived()
    {
        namespace R = DcRagefireChasm;
        return {
            Boss(R::NPC_OGGLEFLINT, R::BIT_OGGLEFLINT, "Oggleflint", R::MAP_ID),
            Boss(R::NPC_TARAGAMAN, R::BIT_TARAGAMAN, "Taragaman the Hungerer", R::MAP_ID),
            Boss(R::NPC_JERGOSH, R::BIT_JERGOSH, "Jergosh the Invoker", R::MAP_ID),
            Boss(R::NPC_BAZZALAN, R::BIT_BAZZALAN, "Bazzalan", R::MAP_ID),
        };
    }
}

// Taragaman is dormant until the other three are dead, so every run takes him
// last; the DBC bits, which the kill mask reads, stay. Normal and heroic alike
// (the heroic bodies keep the normal entries and bits).
TEST(BossRosterRegistryTest, RagefireTaragamanIsLastOnBothDifficulties)
{
    namespace R = DcRagefireChasm;
    uint32 const ramp = BossRosterRegistry::ObjectiveEntry(R::OBJ_HALL_RAMP);
    uint32 const bank = BossRosterRegistry::ObjectiveEntry(R::OBJ_WEST_BANK);
    for (Difficulty d : { DUNGEON_DIFFICULTY_NORMAL, DUNGEON_DIFFICULTY_HEROIC })
    {
        std::vector<DungeonBossInfo> const out =
            BossRosterRegistry::Apply(R::MAP_ID, DcDiffKey::Dungeon(d), RagefireDerived());
        std::vector<uint32> order;
        for (DungeonBossInfo const& b : out)
            order.push_back(b.entry);
        EXPECT_EQ(order, (std::vector<uint32>{ R::NPC_OGGLEFLINT, R::NPC_JERGOSH, ramp, R::NPC_BAZZALAN, bank,
                                               R::NPC_TARAGAMAN }));
        for (DungeonBossInfo const& b : RagefireDerived())
        {
            DungeonBossInfo const* kept = Find(out, b.entry);
            ASSERT_NE(kept, nullptr) << b.entry;
            EXPECT_EQ(kept->encounterIndex, b.encounterIndex) << b.entry;
            EXPECT_EQ(kept->kind, DungeonAnchorKind::Boss) << b.entry;
        }
    }
}

// The picker's walk through that list: each target is finished before the next,
// the hall ramp comes before Bazzalan and the west bank between him and Taragaman,
// and the list is a prefix rule, not a distance rule (Taragaman is the nearest boss
// to Bazzalan's exit).
TEST(BossRosterRegistryTest, RagefirePickerWalksTheKillOrder)
{
    namespace R = DcRagefireChasm;
    uint32 const ramp = BossRosterRegistry::ObjectiveEntry(R::OBJ_HALL_RAMP);
    uint32 const bank = BossRosterRegistry::ObjectiveEntry(R::OBJ_WEST_BANK);
    std::vector<DungeonBossInfo> cands = BossRosterRegistry::Apply(
        R::MAP_ID, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), RagefireDerived());

    std::vector<uint32> walk;
    uint32 sticky = 0, stickyKey = 0;
    while (!cands.empty())
    {
        std::optional<DungeonBossInfo> const pick = DcBossOrdering::PickTarget(cands, sticky, stickyKey, sticky != 0);
        ASSERT_TRUE(pick.has_value());
        // Commit and hold: the same pick while it stands.
        EXPECT_EQ(DcBossOrdering::PickTarget(cands, pick->entry, BossOrderKey(*pick), true)->entry, pick->entry);
        walk.push_back(pick->entry);
        sticky = pick->entry;
        stickyKey = BossOrderKey(*pick);
        cands.erase(std::find_if(cands.begin(), cands.end(),
                                 [&](DungeonBossInfo const& b) { return b.entry == sticky; }));
    }
    EXPECT_EQ(walk, (std::vector<uint32>{ R::NPC_OGGLEFLINT, R::NPC_JERGOSH, ramp, R::NPC_BAZZALAN, bank,
                                          R::NPC_TARAGAMAN }));
}

// The west bank: a plain travel objective (no event, no hook, no gate creature),
// done on arrival or when Taragaman's own slot reads DONE.
TEST(BossRosterRegistryTest, RagefireWestBankIsAPlainTravelObjective)
{
    namespace R = DcRagefireChasm;
    std::vector<DungeonBossInfo> const out = BossRosterRegistry::Apply(
        R::MAP_ID, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_NORMAL), RagefireDerived());
    DungeonBossInfo const* bank = Find(out, BossRosterRegistry::ObjectiveEntry(R::OBJ_WEST_BANK));
    ASSERT_NE(bank, nullptr);
    EXPECT_EQ(bank->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(bank->eventId, 0u);
    EXPECT_EQ(bank->onArriveHook, 0u);
    EXPECT_EQ(bank->gateEntry, 0u);
    EXPECT_EQ(bank->doneBossStateIndex, R::SLOT_TARAGAMAN);
    EXPECT_FLOAT_EQ(bank->x, R::WEST_BANK_X);
    EXPECT_FLOAT_EQ(bank->y, R::WEST_BANK_Y);
    // Out of his aggro and of the plateau pre-clear's radius round his spawn.
    EXPECT_GT(std::hypot(bank->x + 244.74f, bank->y - 150.09f), R::PLATEAU_ROOM_RADIUS);
}

// The hall ramp: the same kind, done on arrival or when Bazzalan's slot reads DONE.
TEST(BossRosterRegistryTest, RagefireHallRampIsAPlainTravelObjective)
{
    namespace R = DcRagefireChasm;
    for (Difficulty d : { DUNGEON_DIFFICULTY_NORMAL, DUNGEON_DIFFICULTY_HEROIC })
    {
        std::vector<DungeonBossInfo> const out =
            BossRosterRegistry::Apply(R::MAP_ID, DcDiffKey::Dungeon(d), RagefireDerived());
        DungeonBossInfo const* ramp = Find(out, BossRosterRegistry::ObjectiveEntry(R::OBJ_HALL_RAMP));
        ASSERT_NE(ramp, nullptr);
        EXPECT_EQ(ramp->kind, DungeonAnchorKind::Objective);
        EXPECT_EQ(ramp->eventId, 0u);
        EXPECT_EQ(ramp->onArriveHook, 0u);
        EXPECT_EQ(ramp->gateEntry, 0u);
        EXPECT_EQ(ramp->doneBossStateIndex, R::SLOT_BAZZALAN);
        EXPECT_FLOAT_EQ(ramp->x, R::HALL_RAMP_X);
        EXPECT_FLOAT_EQ(ramp->y, R::HALL_RAMP_Y);
        EXPECT_FLOAT_EQ(ramp->arriveRadius, R::HALL_RAMP_ARRIVE_RADIUS);
    }
}

// Bazzalan's heroic chain (2s bar + 7.5s) and Taragaman's casts leave them passive with
// no victim while the fight goes on, and a boss with no victim does not hold the
// stuck-combat breaker. Their objectives' slots do, exactly while one reads IN_PROGRESS.
TEST(BossRosterRegistryTest, RagefireVictimlessHoldsAreNotTheFightsEnd)
{
    namespace R = DcRagefireChasm;
    EXPECT_FALSE(DcBossStandDown::HoldsFiveManBossFight(true, true, true, false, /*victimIsOtherMember*/ false));

    std::vector<DungeonBossInfo> const out = BossRosterRegistry::Apply(
        R::MAP_ID, DcDiffKey::Dungeon(DUNGEON_DIFFICULTY_HEROIC), RagefireDerived());
    auto const live = [&out](int32 slotInProgress)
    {
        return std::any_of(out.begin(), out.end(), [slotInProgress](DungeonBossInfo const& b)
        {
            uint32 const state = b.doneBossStateIndex == slotInProgress ? DcBossStandDown::kEncounterInProgress : 0u;
            return DcBossStandDown::ObjectiveEncounterLive(b.kind == DungeonAnchorKind::Objective,
                                                           b.doneBossStateIndex, state);
        });
    };
    EXPECT_TRUE(live(R::SLOT_BAZZALAN));
    EXPECT_TRUE(live(R::SLOT_TARAGAMAN));
    EXPECT_FALSE(live(/*nothing in progress*/ 99));
}
