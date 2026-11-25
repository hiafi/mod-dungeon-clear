/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include <algorithm>
#include <cmath>
#include <vector>

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Data/RoomAggroRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"

// --- Find -----------------------------------------------------------------

TEST(RoomAggroRegistryTest, FindsFlaggedBoss)
{
    RoomAggroBoss const* b = RoomAggroRegistry::Find(557, 18341);  // Pandemonius
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->bossEntry, 18341u);
    EXPECT_FLOAT_EQ(b->radius, 70.0f);
}

// Pandemonius mirrors boss_pandemonius::PullRoom(): the three room-add entries
// AND the ROOM_EXIT(-145) < Y < ROOM_ENTERANCE(-50) corridor. The Y band keeps
// the room-clear off the adds standing behind him toward Tavarok (Y <= -145),
// which the script never force-pulls.
TEST(RoomAggroRegistryTest, PandemoniusHasRoomAddWhitelistAndYBand)
{
    RoomAggroBoss const* b = RoomAggroRegistry::Find(557, 18341);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->memberEntries,
              (std::vector<uint32>{18309, 18311, 18313}));
    ASSERT_TRUE(b->hasYBand);
    EXPECT_FLOAT_EQ(b->minY, -145.0f);
    EXPECT_FLOAT_EQ(b->maxY, -50.0f);

    // An add in the corridor is room trash; the same add behind the boss
    // (Y <= -145) or in front of the entrance (Y >= -50) is outside the pull.
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(*b, -97.0f));   // mid-room
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -160.0f)); // behind, toward Tavarok
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -30.0f));  // in front of the room
    // Strict bounds, matching the script's < / > guards.
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -145.0f));
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -50.0f));
}

// A boss with no Y band (every row but Pandemonius) admits any world Y.
TEST(RoomAggroRegistryTest, NoYBandAdmitsAnyWorldY)
{
    RoomAggroBoss boss{555, 18667, 100.0f, {}};  // Blackheart — no band
    ASSERT_FALSE(boss.hasYBand);
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(boss, 0.0f));
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(boss, -9999.0f));
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(boss, 9999.0f));
}

TEST(RoomAggroRegistryTest, UnflaggedBossReturnsNull)
{
    // Right map, wrong entry.
    EXPECT_EQ(RoomAggroRegistry::Find(557, 99999), nullptr);
    // Right entry, wrong map (registry is keyed by both).
    EXPECT_EQ(RoomAggroRegistry::Find(0, 18341), nullptr);
}

TEST(RoomAggroRegistryTest, MultiWingMapIsKeyedByEntry)
{
    // SM's four wings share map 189; a row names its boss, not the map.
    EXPECT_NE(RoomAggroRegistry::Find(189, 6489), nullptr);    // Ironspine (Graveyard)
    EXPECT_NE(RoomAggroRegistry::Find(189, 300940), nullptr);  // Crell (Armory)
    EXPECT_EQ(RoomAggroRegistry::Find(189, 6487), nullptr);    // Doan (sealed, no room)
}

// The rework's wave 1 rooms (DcScarletMonastery::*_ROOM_*). Distances are the
// live-DB spawns (2026-11-20) measured from each boss's rework spot. The value
// counts a unit as room trash when it is within the radius, inside the Y band
// and outside the pull-out ring (the ring that comes with the boss); every row
// with a pull-out radius also forces the advanced pull-to-camp.
namespace
{
    // A unit `d` yd from the boss, as DungeonClearRoomTrashValue judges it.
    bool ScarletKept(RoomAggroBoss const& row, float d, float y)
    {
        return RoomAggroRegistry::InRoomBand(row, y) &&
               RoomAggroRegistry::IsRoomTrash(row, /*any hostile*/ 4540, d, row.pullOutRadius);
    }
}

TEST(RoomAggroRegistryTest, ScarletIronspineClearsHisLawnButThePhantasm)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_IRONSPINE);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::IRONSPINE_ROOM_RADIUS);
    EXPECT_FLOAT_EQ(r->pullOutRadius, S::IRONSPINE_PULL_OUT_RADIUS);
    EXPECT_FALSE(r->hasYBand);
    // STAGE-D2: the Phantasm 39873 2.5yd off his home (wander 3) comes with him.
    EXPECT_FALSE(ScarletKept(*r, 2.5f + 3.0f, 1283.1f));
    // Everything else within 30yd dies first: the Unfettered Spirit packs
    // 22.8-30yd (wander 3-5) and the Anguished Dead 39880 at 30.8yd.
    for (float d : { 22.8f - 3.0f, 24.4f, 25.1f - 5.0f, 27.5f, 30.0f, 30.8f })
        EXPECT_TRUE(ScarletKept(*r, d, 1300.0f)) << d;
}

TEST(RoomAggroRegistryTest, ScarletCouncilClearsItsLawnSpot)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_FALLEN_CHAMPION);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::COUNCIL_ROOM_RADIUS);
    EXPECT_FLOAT_EQ(r->pullOutRadius, S::COUNCIL_PULL_OUT_RADIUS);
    // Azshir is the second council anchor and a roster boss, never a room-aggro
    // boss of his own: one pre-clear per pull.
    EXPECT_EQ(RoomAggroRegistry::Find(S::MAP_ID, S::NPC_AZSHIR), nullptr);
    // The Anguished Dead 39899 14.6yd out comes with them; 39891 (18yd, wander 3),
    // the Unfettered Spirits 24.2-29yd and the Anguished Dead 39900 (29.8yd) die
    // first.
    EXPECT_FALSE(ScarletKept(*r, 14.6f, 1362.8f));
    for (float d : { 18.0f, 24.2f, 25.8f, 29.0f, 29.8f })
        EXPECT_TRUE(ScarletKept(*r, d, 1340.0f)) << d;
}

TEST(RoomAggroRegistryTest, ScarletLokseyClearsTheKennelHall)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_LOKSEY);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::LOKSEY_ROOM_RADIUS);
    // The hall starts 35yd from his alcove: the computed sphere excludes only his
    // own hounds, so no pull-out ring and no forced advanced pull.
    EXPECT_FLOAT_EQ(r->pullOutRadius, 0.0f);
    // The computed sphere at an even level: detection 20 + both reaches 1.5 +
    // AggroRangeMargin 2 + RoomAggroPathPadding 3 (DcEngageGeometry::AggroReachYards).
    float const sphere = 20.0f + 1.5f + 1.5f + 2.0f + 3.0f;
    // The hall's packs (R-1-LIB m3): Gallant 40081 (34.9), Beastmaster 40078
    // (47.4), the NE pack 40068 / 40058 / 40070 by the pups' entry (86-86.6), the
    // SE Gallants 40075 / 40076 (87.4 / 90.5).
    for (float d : { 34.9f, 47.4f, 62.8f, 69.3f, 80.7f, 86.6f, 87.4f, 90.5f })
        EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*r, 4288, d, sphere)) << d;
    // Outside the hall: the Gallant 40061 north of the NE entry (98.5yd) and Kell's
    // long hall (105yd and on).
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*r, 4287, 98.5f, sphere));
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*r, 4287, 105.0f, sphere));
}

TEST(RoomAggroRegistryTest, ScarletKellClearsTheCaseSpotsNotTheReadingHall)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_KELL);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::KELL_ROOM_RADIUS);
    EXPECT_FLOAT_EQ(r->pullOutRadius, S::KELL_PULL_OUT_RADIUS);
    ASSERT_TRUE(r->hasYBand);
    EXPECT_FLOAT_EQ(r->minY, S::KELL_ROOM_MIN_Y);
    EXPECT_FLOAT_EQ(r->maxY, S::KELL_ROOM_MAX_Y);
    // His three Diviners in the aisle (7.9 / 11.2 / 11.6yd) come with him.
    EXPECT_FALSE(ScarletKept(*r, 7.9f, -335.2f));
    EXPECT_FALSE(ScarletKept(*r, 11.6f, -332.6f));
    // The case-spot trash dies first: the Monk 40084 1.4yd from the north skull
    // case (27.9yd), the Diviner 40085 by the east skull case (23.5), the
    // Gallant 40074 (23.8), the Chaplain 40055 (25.2), and the bay rows out to
    // the Diviner 40056 (36.3yd, y -303).
    EXPECT_TRUE(ScarletKept(*r, 27.9f, -312.4f));
    EXPECT_TRUE(ScarletKept(*r, 23.5f, -336.3f));
    EXPECT_TRUE(ScarletKept(*r, 23.8f, -310.1f));
    EXPECT_TRUE(ScarletKept(*r, 25.2f, -304.4f));
    EXPECT_TRUE(ScarletKept(*r, 36.3f, -303.0f));
    // The reading hall's Monk 40094 (35.6yd, y -363.6) is Hollis Grey's room, and
    // the Adepts beyond the north door (y -286) are nobody's.
    EXPECT_FALSE(ScarletKept(*r, 35.6f, -363.6f));
    EXPECT_FALSE(ScarletKept(*r, 36.0f, -286.0f));
}

TEST(RoomAggroRegistryTest, ScarletHollisGreyClearsTheReadingHall)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_HOLLIS_GREY);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::HOLLIS_GREY_ROOM_RADIUS);
    EXPECT_FLOAT_EQ(r->pullOutRadius, S::HOLLIS_GREY_PULL_OUT_RADIUS);
    // STAGE-D7: the Diviner 40103 (8.7yd) and the Monk 40102 (11.5yd) come with him.
    EXPECT_FALSE(ScarletKept(*r, 8.7f, -386.9f));
    EXPECT_FALSE(ScarletKept(*r, 11.5f, -386.5f));
    // The other ten die first, 14.4-31.7yd (the computed ~28yd sphere would have
    // kept only the far four).
    for (float d : { 14.4f, 15.8f, 20.0f, 20.1f, 22.2f, 25.7f, 27.8f, 29.8f, 30.8f, 31.7f })
        EXPECT_TRUE(ScarletKept(*r, d, -390.0f)) << d;
}

TEST(RoomAggroRegistryTest, ScarletVishasThalnosAndDoanPullNoRoom)
{
    namespace S = DcScarletMonastery;
    for (uint32 entry : { S::NPC_VISHAS, S::NPC_THALNOS, S::NPC_DOAN })
        EXPECT_EQ(RoomAggroRegistry::Find(S::MAP_ID, entry), nullptr) << entry;
}

// Wave 2 (DC-2). Live-DB spawns 2026-11-20 measured from the rev spawns.
TEST(RoomAggroRegistryTest, ScarletCrellClearsTheWholeStoreroom)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_CRELL);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::CRELL_ROOM_RADIUS);
    EXPECT_FLOAT_EQ(r->pullOutRadius, S::CRELL_PULL_OUT_RADIUS);
    EXPECT_FALSE(r->hasYBand);
    // STAGE-D9: clear the storeroom first. The Guardsman 40149 (10.4yd) and the
    // Protector 89059 (11.0yd) by the anvil die first, and so does the patrol
    // 40151 at its south end (18.3yd). At its north end, past the anvil (7.9yd),
    // it is inside the ring for a moment and would come with him.
    for (float d : { 10.4f, 11.0f, 18.3f, 21.0f, 21.2f, 25.4f, 28.2f, 28.9f, 29.7f, 31.9f, 32.5f, 33.4f,
                     33.5f, 36.5f })
        EXPECT_TRUE(ScarletKept(*r, d, -390.0f)) << d;
    EXPECT_FALSE(ScarletKept(*r, 7.9f, -382.8f));
    // R-2-FW MN-1: his detection range is 9 (crell.py CRELL_DETECTION_RANGE, F-2c). The pull-out ring stays inside
    // it, so the two by the anvil are kept, and a non-zero ring forces the advanced pull: the tank tags them from
    // outside his live aggro sphere and drags them to a camp, never fighting them beside him.
    EXPECT_LE(S::CRELL_PULL_OUT_RADIUS, 9.0f);
    EXPECT_GT(r->pullOutRadius, 0.0f);
    // The Protector 89062 past the east strip's south end (43.3yd) is route trash.
    EXPECT_FALSE(ScarletKept(*r, 43.3f, -427.4f));
}

TEST(RoomAggroRegistryTest, ScarletHaldenIsPulledAlone)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_HALDEN);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::HALDEN_ROOM_RADIUS);
    EXPECT_FLOAT_EQ(r->pullOutRadius, S::HALDEN_PULL_OUT_RADIUS);
    // STAGE-D10: his five chamber soldiers (Protector 89075, Defenders 89074 /
    // 89077 / 89076, Myrmidon 89078) and the six in the necks and side rooms die
    // first. His detection range is 10: the pull-out ring stays inside it.
    EXPECT_LT(S::HALDEN_PULL_OUT_RADIUS, 10.0f);
    for (float d : { 11.3f, 11.4f, 12.9f, 17.5f, 17.9f, 19.6f, 25.5f, 26.2f, 27.3f, 27.6f, 27.7f })
        EXPECT_TRUE(ScarletKept(*r, d, -400.0f)) << d;
    // The far ends of the south band and the north room (29-35yd) are route trash.
    EXPECT_FALSE(ScarletKept(*r, 29.0f, -378.8f));
}

TEST(RoomAggroRegistryTest, ScarletThorneClearsTheGarden)
{
    namespace S = DcScarletMonastery;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(S::MAP_ID, S::NPC_THORNE);
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->radius, S::THORNE_ROOM_RADIUS);
    EXPECT_FLOAT_EQ(r->pullOutRadius, S::THORNE_PULL_OUT_RADIUS);
    // STAGE-D12: the Centurion 39958 (20.7yd, random movement 3) and the Defender
    // 39962 (22.7yd) would drag him in; they die first even wandered 3yd closer.
    EXPECT_TRUE(ScarletKept(*r, 20.7f - 3.0f, 1378.6f));
    EXPECT_TRUE(ScarletKept(*r, 22.7f - 3.0f, 1421.9f));
    // The rest of the garden: Centurion 39959 (27.8), the west entry's Myrmidons
    // 39954 / 39965 (27.9 / 28.2) and Sorcerer 39951 (30.9), the Myrmidon 39948
    // (35.6), the moved Centurion 39953 (39.0) and the Sorcerer 39966 (39.7).
    for (float d : { 27.8f, 27.9f, 28.2f, 30.9f, 35.6f, 39.0f, 39.7f })
        EXPECT_TRUE(ScarletKept(*r, d, 1400.0f)) << d;
}

TEST(RoomAggroRegistryTest, ScarletCathedralLostItsStockRoomPull)
{
    namespace S = DcScarletMonastery;
    // The stock CATHEDRAL_PULL_RANGE rows are gone (D13: never pull Mograine; the
    // nave is empty), and nothing stands within 37yd of Herod's pit.
    EXPECT_EQ(RoomAggroRegistry::Find(S::MAP_ID, S::NPC_MOGRAINE), nullptr);
    EXPECT_EQ(RoomAggroRegistry::Find(S::MAP_ID, S::NPC_WHITEMANE), nullptr);
    EXPECT_EQ(RoomAggroRegistry::Find(S::MAP_ID, S::NPC_HEROD), nullptr);
}

// Scholomance has NO room-aggro row any more. The Marduk / Vectus students
// (10475) are unattackable props in the reworked dungeon, so there is no room to
// pre-clear; the linked pair is a plain merged roster boss (see
// ScholomanceEvents.cpp). Assert it so a re-add is a deliberate choice.
TEST(RoomAggroRegistryTest, ScholomanceHasNoRoomAggroEntry)
{
    EXPECT_EQ(RoomAggroRegistry::Find(289, 10432), nullptr);  // Vectus
    EXPECT_EQ(RoomAggroRegistry::Find(289, 10433), nullptr);  // Marduk Blackpool
    EXPECT_EQ(RoomAggroRegistry::Find(289, 10475), nullptr);  // Scholomance Student
}

// Mechano-Lord Capacitus (Mechanar, 554) no longer has a room-aggro entry: the
// boss order was changed to Gyro-Kill -> Capacitus -> Iron-Hand, so the tank
// approaches his pit from the NW and the SE Driller pack falls on the post-boss
// walk down to Iron-Hand (plain trash-clear), removing the reason for the
// pre-clear. Assert the entry is gone so a re-add is a deliberate choice.
TEST(RoomAggroRegistryTest, MechanarCapacitusHasNoRoomAggroEntry)
{
    EXPECT_EQ(RoomAggroRegistry::Find(554, 19219), nullptr);
}

// Gilnid (Deadmines, 36) is flagged NOT because he force-pulls his room — his whole
// script is a Molten Metal cast, a yell and a door — but because the goblin foundry
// cannot be decomposed into packs: 19 level-18 ELITES standing 7.1-57.6yd from him,
// 17 of which chain together at a same-level elite's ~20yd aggro radius. The pull
// governor's 6yd-spread / 10yd-assist windows read that as "estimated 2" and Leeroy
// the room, then Gilnid joins the pile (tp-20260815-171416-1: 5 of 20 runs wiped
// there, status alternating boss/trash every 1-2s). radius 60 covers all 19; 50
// would strand the three western-most at 52.0 / 55.3 / 57.6yd.
TEST(RoomAggroRegistryTest, DeadminesGilnidPreClearsTheFoundry)
{
    RoomAggroBoss const* gilnid = RoomAggroRegistry::Find(36, 1763);
    ASSERT_NE(gilnid, nullptr);
    EXPECT_FLOAT_EQ(gilnid->radius, 60.0f);
    // Any hostile in the room — the foundry is a mixed Craftsman/Engineer floor and
    // the safe over-approximation is the documented default for a row like this.
    EXPECT_TRUE(gilnid->memberEntries.empty());
    EXPECT_FALSE(gilnid->hasYBand);
    // No overrides: the computed boss sphere already excludes the two Craftsmen at
    // 7.1/9.2yd (they come with the boss) and admits everything past it as trash.
    EXPECT_FLOAT_EQ(gilnid->pullOutRadius, 0.0f);
    EXPECT_FLOAT_EQ(gilnid->skirtRadius, 0.0f);
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(36, 1763), 0.0f);
    // The room members are room, not bosses.
    EXPECT_EQ(RoomAggroRegistry::Find(36, 1731), nullptr);  // Goblin Craftsman
    EXPECT_EQ(RoomAggroRegistry::Find(36, 622), nullptr);   // Goblin Engineer
}

// Emperor Dagran Thaurissan (BRD, 230), reworked: no CallForHelp, so the room
// clear only takes the floor packs flanking the throne (<= 29.9yd from him) and
// leaves the balconies (>= 39.6yd), which no longer answer his pull. A plain
// sphere: no balcony detour, no boss-side ordering, no overrides.
TEST(RoomAggroRegistryTest, BrdEmperorClearsOnlyTheThroneFloor)
{
    RoomAggroBoss const* emperor = RoomAggroRegistry::Find(230, 9019);
    ASSERT_NE(emperor, nullptr);
    EXPECT_GT(emperor->radius, 29.9f);
    EXPECT_LT(emperor->radius, 39.6f);
    EXPECT_TRUE(emperor->memberEntries.empty());
    EXPECT_FALSE(emperor->detourReachable) << "the balconies are not his room any more";
    EXPECT_FLOAT_EQ(emperor->bossSideLastRadius, 0.0f);
    EXPECT_FLOAT_EQ(emperor->pullOutRadius, 0.0f);
    EXPECT_FLOAT_EQ(emperor->skirtRadius, 0.0f);
    EXPECT_FALSE(emperor->hasCampBox);
    EXPECT_FALSE(emperor->hasYBand);
    // Moira (4.4yd, inside his aggro sphere) comes with his pull; no row of her own.
    EXPECT_EQ(RoomAggroRegistry::Find(230, 8929), nullptr);

    RoomAggroBoss const* gilnid = RoomAggroRegistry::Find(36, 1763);
    ASSERT_NE(gilnid, nullptr);
    EXPECT_FALSE(gilnid->detourReachable);
    EXPECT_FLOAT_EQ(gilnid->bossSideLastRadius, 0.0f);
}

// Nethermancer Sepethrea (Mechanar, 554) IS a room-aggro boss: her chamber holds
// three elite trash groups pre-cleared and dragged back toward the entrance before
// the pull. radius 70 covers the farthest (Pack A ~67yd); pullOutRadius 14 shrinks
// the room-trash exclusion so Pack B — parked ~17yd out, inside her ~28yd real
// aggro sphere — is KEPT as clearable trash instead of coming with the boss.
// Gundrak Slad'ran. Every entry in this room reads hostile, so the whitelist is
// not about the neutral-member bypass here -- it exists to keep two non-threats
// OUT: the 86 level-1 Fanged Pit Vipers (neutral faction 7, they never aggro on
// proximity, and sweeping them went badly live) and the Drakkari Frenzy that sit
// inside the radius but ~20yd DOWN in the moat, which the party cannot walk to.
TEST(RoomAggroRegistryTest, GundrakSladranClearsTheSnakePit)
{
    RoomAggroBoss const* sladran = RoomAggroRegistry::Find(604, 29304);
    ASSERT_NE(sladran, nullptr);
    EXPECT_FLOAT_EQ(sladran->radius, 75.0f);

    // The room is the nine elites, and nothing else.
    EXPECT_EQ(sladran->memberEntries, (std::vector<uint32>{29774, 29768}));
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(*sladran, 29774));   // Spitting Cobra
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(*sladran, 29768));   // Unyielding Constrictor
    EXPECT_FALSE(RoomAggroRegistry::IsMemberEntry(*sladran, 29630));  // Fanged Pit Viper — ignored
    EXPECT_FALSE(RoomAggroRegistry::IsMemberEntry(*sladran, 29834));  // Drakkari Frenzy — in the moat
    EXPECT_FALSE(RoomAggroRegistry::IsMemberEntry(*sladran, 29682));  // Slad'ran Summon Target
    EXPECT_FALSE(RoomAggroRegistry::IsMemberEntry(*sladran, 13321));  // Frog

    // No Y band and no overrides — the room is a plain radius sphere.
    EXPECT_FALSE(sladran->hasYBand);
    EXPECT_FLOAT_EQ(sladran->pullOutRadius, 0.0f);
    EXPECT_FLOAT_EQ(sladran->skirtRadius, 0.0f);
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(604, 29304), 0.0f);

    float const sphere = 28.0f;  // ~aggro + reaches + margin + padding

    // The radius is sized off the PATROL envelope, not the spawn rows: constrictor
    // guid 127014 walks path 1270140 out to 69.9yd. A radius cut to the 62.0yd
    // farthest SPAWN would drop it at the far end of its path and call the room
    // clear while it was still walking back.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sladran, 29768, 69.9f, sphere));

    // ...and still stops short of the next room's mobs of the same two entries.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sladran, 29774, 79.5f, sphere));
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sladran, 29768, 88.9f, sphere));

    // A pit viper is not room trash at any distance, even well inside the radius.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sladran, 29630, 40.0f, sphere));

    // The closest Spitting Cobra (26.9yd) falls inside the boss sphere and comes
    // with the pull; the closest Constrictor (29.9yd) is clearable room trash.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sladran, 29774, 26.9f, sphere));
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sladran, 29768, 29.9f, sphere));

    // Only Slad'ran is flagged on 604; the room entries are room, not bosses.
    EXPECT_EQ(RoomAggroRegistry::Find(604, 29630), nullptr);
    EXPECT_EQ(RoomAggroRegistry::Find(604, 29774), nullptr);
}

TEST(RoomAggroRegistryTest, MechanarSepethreaHasPullOutRoom)
{
    RoomAggroBoss const* sep = RoomAggroRegistry::Find(554, 19221);
    ASSERT_NE(sep, nullptr);
    EXPECT_FLOAT_EQ(sep->radius, 70.0f);
    EXPECT_TRUE(sep->memberEntries.empty());   // any hostile in the chamber
    EXPECT_FALSE(sep->hasYBand);
    EXPECT_GT(sep->pullOutRadius, 0.0f);        // the coupled shrink is set
    EXPECT_LT(sep->pullOutRadius, 17.0f);       // below Pack B's nearest member (17.3yd)
    // The fight-standoff skirt is WIDENED past her raw ~28yd sphere so the dragged
    // pack is fought clear of her aggro/CallForHelp (and the open floor her Raging
    // Flames roam), yet stays DECOUPLED from the exclusion (pullOutRadius).
    EXPECT_GT(sep->skirtRadius, 28.0f);
    EXPECT_GT(sep->skirtRadius, sep->pullOutRadius);
}

// SkirtOverride returns the row's widened skirt for a flagged boss and 0 elsewhere,
// so RoomAggroSphereRadius (which maxes the computed sphere against it) widens ONLY
// for the flagged boss. Pandemonius carries no override -> 0 (computed sphere alone).
TEST(RoomAggroRegistryTest, SkirtOverrideOnlyForFlaggedRow)
{
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(554, 19221),
                    RoomAggroRegistry::Find(554, 19221)->skirtRadius);
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(557, 18341), 0.0f);  // Pandemonius: none
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(554, 12345), 0.0f);  // not a room boss
}

// The widened skirt must NOT change Pack B's room-trash membership: that reads
// pullOutRadius (14yd), never the skirt. Pack B at 17.3yd is still KEPT even though
// it now sits well inside the 40yd skirt.
TEST(RoomAggroRegistryTest, MechanarSepethreaSkirtDoesNotReclassifyPackB)
{
    RoomAggroBoss const* sep = RoomAggroRegistry::Find(554, 19221);
    ASSERT_NE(sep, nullptr);
    ASSERT_GT(sep->skirtRadius, 17.3f);   // Pack B is inside the skirt
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 17.3f, sep->pullOutRadius));
}

// The pullOutRadius is what KEEPS Pack B as room trash. Pack B's nearest member
// sits ~17.3yd from Sepethrea: inside her computed ~28yd exclusion sphere (would be
// dropped as "comes with the boss") but OUTSIDE the 14yd pull-out radius the value
// passes as bossSafeRadius when the row carries one — so it stays clearable and the
// advanced pull drags it out. This mirrors what DungeonClearRoomTrashValue does with
// room->pullOutRadius; IsRoomTrash itself just takes the effective radius.
TEST(RoomAggroRegistryTest, MechanarSepethreaPullOutKeepsFrontPack)
{
    RoomAggroBoss const* sep = RoomAggroRegistry::Find(554, 19221);
    ASSERT_NE(sep, nullptr);
    // With the shrunk pull-out radius: Pack B (17.3yd) is KEPT.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 17.3f, sep->pullOutRadius));
    // With the real ~28yd exclusion sphere it would be dropped (the old behaviour
    // the shrink exists to override).
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 17.3f, 28.0f));
    // Something genuinely glued to her (inside 14yd) still comes with the boss.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 10.0f, sep->pullOutRadius));
    // Pack A far out (67yd) is still inside the 70yd room radius.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sep, 19168, 67.0f, sep->pullOutRadius));
}

// --- IsMemberEntry --------------------------------------------------------

TEST(RoomAggroRegistryTest, EmptyWhitelistMatchesAnyEntry)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(boss, 18309));
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(boss, 12345));
}

TEST(RoomAggroRegistryTest, NonEmptyWhitelistMatchesOnlyListed)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {18309, 18311, 18313}};
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(boss, 18311));
    EXPECT_FALSE(RoomAggroRegistry::IsMemberEntry(boss, 99999));
}

// --- IsRoomTrash (radius + boss-aggro-sphere exclusion) -------------------

TEST(RoomAggroRegistryTest, RoomTrashInsideRadiusOutsideBossSphere)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    // 30yd from the boss, boss aggro sphere 12yd -> clearable room trash.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(boss, 18309, 30.0f, 12.0f));
}

TEST(RoomAggroRegistryTest, RoomTrashBeyondRadiusExcluded)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(boss, 18309, 80.0f, 12.0f));
}

TEST(RoomAggroRegistryTest, RoomTrashInsideBossSphereExcluded)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    // Glued to the boss (8yd, sphere 12yd) -> comes with the boss, not counted.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(boss, 18309, 8.0f, 12.0f));
}

TEST(RoomAggroRegistryTest, RoomTrashNonMemberExcluded)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {18309}};
    // In range and outside the sphere, but not on the whitelist.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(boss, 99999, 30.0f, 12.0f));
}

// --- NeedsRoomAggroSkirt (the skirt chooser) ------------------------------
//
// Boss centred at the origin, aggro sphere radius 10. The bot approaches a
// target and we ask whether the straight 2D chord clips the sphere.

TEST(RoomAggroSkirtTest, ChordCrossingSphereNeedsSkirt)
{
    // Bot at (-30, 5), target at (30, 5): the chord runs left-to-right just 5yd
    // above the boss at the origin — well inside the 10yd sphere.
    EXPECT_TRUE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 5.0f, 30.0f, 5.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, ChordClearOfSphereGoesDirect)
{
    // Same span but 20yd above the boss: the chord never enters the 10yd sphere.
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 20.0f, 30.0f, 20.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, TargetOnNearSideGoesDirect)
{
    // Bot and target both well to one side of the boss; the segment's closest
    // approach to the origin is the target endpoint at 25yd, outside the sphere.
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -40.0f, 0.0f, -25.0f, 0.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, BotInsidePaddedSphereStillSkirts)
{
    // Recovery case: the bot is standing 4yd from the boss centre, inside the
    // 10yd sphere. The chord's closest point (the bot endpoint) is within the
    // radius, so a skirt/exit waypoint is still demanded — never "go direct".
    EXPECT_TRUE(DcEngageGeometry::NeedsRoomAggroSkirt(
        4.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, DegenerateZeroLengthChord)
{
    // Bot and target coincide far from the boss -> the (point) chord is 50yd out,
    // outside the sphere -> no skirt. (Exercises the A==B segment path.)
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        50.0f, 0.0f, 50.0f, 0.0f, 0.0f, 0.0f, 10.0f));
    // And the same coincident point sitting ON the boss -> inside -> skirt.
    EXPECT_TRUE(DcEngageGeometry::NeedsRoomAggroSkirt(
        1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, NonPositiveRadiusNeverSkirts)
{
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, -5.0f));
}

// --- FirstViolatedSphere (the en-route avoidance chooser) -----------------
//
// Same chord test, now over a SET of bystander packs. The contract is
// "nearest violated sphere to the bot", because the tank rounds obstacles one
// at a time and the proven single-sphere orbit does the actual walking.

namespace
{
    using Sphere = DcEngageGeometry::AvoidSphere;

    Sphere At(float x, float y, float r)
    {
        Sphere s;
        s.x = x;
        s.y = y;
        s.r = r;
        return s;
    }
}

TEST(DcEnRouteAvoidTest, NoSpheresMeansNoDetour)
{
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, {}), -1);
}

TEST(DcEnRouteAvoidTest, ClearCorridorMeansNoDetour)
{
    // Two packs well off the line: the walk passes nothing.
    std::vector<Sphere> const s{At(25.f, 40.f, 10.f), At(25.f, -40.f, 10.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), -1);
}

TEST(DcEnRouteAvoidTest, PicksTheViolatorNearestTheBot)
{
    // Three packs sitting ON the line at 10, 25 and 40 yards out. The tank must
    // round the FIRST one it would reach — rounding the far one first would walk
    // it straight through the near one.
    std::vector<Sphere> const s{At(40.f, 0.f, 8.f), At(10.f, 0.f, 8.f), At(25.f, 0.f, 8.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), 1);
}

TEST(DcEnRouteAvoidTest, IgnoresClearSpheresWhenPickingTheNearest)
{
    // The nearest pack (index 0) is off the line; the violator further out is the
    // one to round. "Nearest" is nearest VIOLATOR, not nearest pack.
    std::vector<Sphere> const s{At(5.f, 30.f, 8.f), At(30.f, 0.f, 8.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), 1);
}

TEST(DcEnRouteAvoidTest, DegenerateRadiiAreSkipped)
{
    // A zero/negative radius is "no sphere" (a mob whose aggro range resolved to
    // nothing), not "a sphere at the origin the tank must orbit".
    std::vector<Sphere> const s{At(25.f, 0.f, 0.f), At(25.f, 0.f, -5.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), -1);
}

TEST(DcEnRouteAvoidTest, PackTheTankIsStandingInIsStillRounded)
{
    // Recovery case, inherited from NeedsRoomAggroSkirt: the tank has drifted
    // inside a bystander's aggro sphere. It must be told to leave, not to read
    // "already inside, nothing to avoid" and carry on through.
    std::vector<Sphere> const s{At(3.f, 0.f, 10.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), 0);
}

// --- Karazhan (532) — Moroes' Banquet Hall ---------------------------------

TEST(RoomAggroRegistryTest, KarazhanMoroesClearsTheBanquetHall)
{
    RoomAggroBoss const* m = RoomAggroRegistry::Find(532, 15687);
    ASSERT_NE(m, nullptr);
    EXPECT_GE(m->radius, 56.4f);  // the far south formation
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(532, 15687), 32.0f);

    // A Phantom Guest in the closest (east) formation, 21.1yd out, is trash:
    // the pull-out radius keeps it clearable instead of "comes with the boss".
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*m, 16409, 21.1f, m->pullOutRadius));
    // Stewards and Waiters are trash too.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*m, 16414, 45.5f, m->pullOutRadius));
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*m, 16415, 36.5f, m->pullOutRadius));
    // Moroes' own summoned guests are never trash, wherever they stand.
    for (uint32 guest : {17007u, 19872u, 19873u, 19874u, 19875u, 19876u})
    {
        EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*m, guest, 5.0f, m->pullOutRadius)) << guest;
        EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*m, guest, 30.0f, m->pullOutRadius)) << guest;
    }
    // A Phantom Guest glued to the dais comes with the boss.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*m, 16409, 5.0f, m->pullOutRadius));
    // Other entries in range (16459, Wanton Hostess) are not the room's.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*m, 16459, 30.0f, m->pullOutRadius));
    // The summoned guests stand 4-7yd off him, inside the pull-out radius.
    EXPECT_GT(m->pullOutRadius, 7.0f);
    EXPECT_LT(m->pullOutRadius, 21.1f);
}

// A row without a camp box never bounds the camp.
TEST(RoomAggroRegistryTest, NoCampBoxAdmitsAnyPoint)
{
    RoomAggroBoss const* b = RoomAggroRegistry::Find(557, 18341);  // Pandemonius
    ASSERT_NE(b, nullptr);
    EXPECT_FALSE(b->hasCampBox);
    EXPECT_TRUE(RoomAggroRegistry::InCampBox(*b, -1.0e6f, 1.0e6f));
}

// Moroes' camp box keeps the pull camp inside the Banquet Hall. Without it the
// skirt-widened drag walked camps back onto the south door line, next to the
// Phantom Attendants' corridor (tr-20260926-174359-2).
TEST(RoomAggroRegistryTest, KarazhanMoroesCampStaysInTheHall)
{
    RoomAggroBoss const* m = RoomAggroRegistry::Find(532, 15687);
    ASSERT_NE(m, nullptr);
    ASSERT_TRUE(m->hasCampBox);

    // Inside his script's evade box (x >= -11028, y >= -1955) with room to
    // spare for the fanned-out camp.
    EXPECT_GE(m->campMinX, -11028.0f + 15.0f);
    EXPECT_GE(m->campMinY, -1955.0f + 15.0f);

    // The camps that run placed: planned on the door line, and the fresh camp
    // the Moroes fight was dragged to.
    EXPECT_FALSE(RoomAggroRegistry::InCampBox(*m, -10973.3f, -1955.1f));
    EXPECT_FALSE(RoomAggroRegistry::InCampBox(*m, -10975.2f, -1954.1f));
    EXPECT_FALSE(RoomAggroRegistry::InCampBox(*m, -10973.1f, -1957.2f));
    EXPECT_FALSE(RoomAggroRegistry::InCampBox(*m, -10971.2f, -1943.3f));
    // The Phantom Attendants' spawn south of the door is outside too.
    EXPECT_FALSE(RoomAggroRegistry::InCampBox(*m, -10956.9f, -1971.5f));

    // The dais and a south-hall spot 32yd+ off Moroes (the skirt) are inside, so
    // the camp search still has ground that clears his skirt.
    float const mx = -10982.7f, my = -1877.9f;
    EXPECT_TRUE(RoomAggroRegistry::InCampBox(*m, mx, my));
    float const cx = -10975.0f, cy = -1935.0f;
    EXPECT_TRUE(RoomAggroRegistry::InCampBox(*m, cx, cy));
    EXPECT_GE(std::hypot(cx - mx, cy - my), RoomAggroRegistry::SkirtOverride(532, 15687));
}

// --- Ragefire Chasm (389), the rework --------------------------------------
// Live spawns (acore_world.creature, 2026-11-24): position and wander distance.
namespace
{
    struct RagefireSpawn { uint32 guid; float x, y, z, wander; };
    struct RagefirePos { float x, y, z; };

    constexpr RagefirePos RFC_OGGLEFLINT = { -147.55f, 38.71f, -38.80f };
    constexpr RagefirePos RFC_JERGOSH    = { -376.81f, 209.22f, -21.80f };
    constexpr RagefirePos RFC_BAZZALAN   = { -384.87f, 146.04f, 7.82f };
    constexpr RagefirePos RFC_TARAGAMAN  = { -244.74f, 150.09f, -18.75f };

    float RagefireDist(RagefirePos const& a, RagefirePos const& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    // Kept as room trash everywhere its wander takes it (both ends of it).
    bool RagefireAlwaysKept(RoomAggroBoss const& r, RagefirePos const& boss, RagefireSpawn const& s)
    {
        float const d = RagefireDist(boss, { s.x, s.y, s.z });
        return RoomAggroRegistry::InRoomBand(r, s.y) &&
               RoomAggroRegistry::IsRoomTrash(r, 11322, d - s.wander, r.pullOutRadius) &&
               RoomAggroRegistry::IsRoomTrash(r, 11322, d + s.wander, r.pullOutRadius);
    }

    bool RagefireKept(RoomAggroBoss const& r, RagefirePos const& boss, RagefirePos const& p)
    {
        return RoomAggroRegistry::InRoomBand(r, p.y) &&
               RoomAggroRegistry::IsRoomTrash(r, 11322, RagefireDist(boss, p), r.pullOutRadius);
    }

    // Every pull-out is under the bosses' 12yd detection (so the kept units near
    // them are dragged out, never fought inside it) and forces the advanced pull.
    void RagefireRowIsSane(RoomAggroBoss const* r, float radius, float pullOut)
    {
        ASSERT_NE(r, nullptr);
        EXPECT_FLOAT_EQ(r->radius, radius);
        EXPECT_FLOAT_EQ(r->pullOutRadius, pullOut);
        EXPECT_GT(r->pullOutRadius, 0.0f);
        EXPECT_LT(r->pullOutRadius, 12.0f);
        EXPECT_FLOAT_EQ(r->skirtRadius, 0.0f);
        EXPECT_TRUE(r->memberEntries.empty());
    }
}

TEST(RoomAggroRegistryTest, RagefireOggleflintClearsTheDen)
{
    namespace R = DcRagefireChasm;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(R::MAP_ID, R::NPC_OGGLEFLINT);
    RagefireRowIsSane(r, R::DEN_ROOM_RADIUS, R::DEN_ROOM_PULLOUT);
    for (RagefireSpawn const& s : { RagefireSpawn{ 48643, -148.04f, 27.19f, -38.85f, 2.0f },
                                    RagefireSpawn{ 48645, -154.04f, 27.97f, -38.57f, 0.0f } })
        EXPECT_TRUE(RagefireAlwaysKept(*r, RFC_OGGLEFLINT, s)) << s.guid;
    // The up leg's Earthborer in the tunnel overhead and the neck's pair are not.
    EXPECT_FALSE(RagefireKept(*r, RFC_OGGLEFLINT, { -168.54f, 13.33f, -29.62f }));
    EXPECT_FALSE(RagefireKept(*r, RFC_OGGLEFLINT, { -152.45f, 3.91f, -40.59f }));
}

TEST(RoomAggroRegistryTest, RagefireJergoshClearsTheHall)
{
    namespace R = DcRagefireChasm;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(R::MAP_ID, R::NPC_JERGOSH);
    RagefireRowIsSane(r, R::HALL_ROOM_RADIUS, R::HALL_ROOM_PULLOUT);
    for (RagefireSpawn const& s : { RagefireSpawn{ 48737, -369.36f, 211.17f, -21.80f, 2.0f },    // 7.7
                                    RagefireSpawn{ 48741, -374.13f, 201.24f, -21.80f, 2.0f },    // 8.4
                                    RagefireSpawn{ 48744, -367.19f, 180.43f, -22.16f, 2.0f },    // the neck
                                    RagefireSpawn{ 48739, -360.93f, 179.06f, -22.19f, 2.0f },
                                    RagefireSpawn{ 48745, -367.06f, 173.70f, -22.01f, 2.0f },
                                    RagefireSpawn{ 48732, -346.64f, 213.99f, -21.39f, 2.0f },    // the east half
                                    RagefireSpawn{ 48728, -340.18f, 207.47f, -21.49f, 2.0f },
                                    // the offering spot's Enforcers (a bot that catches a wisp late runs there)
                                    RagefireSpawn{ 48731, -336.36f, 224.26f, -20.27f, 0.0f },
                                    RagefireSpawn{ 48725, -329.78f, 210.64f, -19.92f, 0.0f } })
        EXPECT_TRUE(RagefireAlwaysKept(*r, RFC_JERGOSH, s)) << s.guid;
    // The north shelf, 17yd up, is the walk to Bazzalan.
    for (RagefirePos const& p : { RagefirePos{ -385.12f, 239.62f, -4.85f }, RagefirePos{ -392.02f, 244.26f, -4.85f },
                                  RagefirePos{ -367.59f, 254.25f, -5.05f } })
        EXPECT_FALSE(RagefireKept(*r, RFC_JERGOSH, p)) << p.y;
    // The strip's Warlock (61.7) is on the way in.
    EXPECT_FALSE(RagefireKept(*r, RFC_JERGOSH, { -316.56f, 222.63f, -22.17f }));
}

TEST(RoomAggroRegistryTest, RagefireBazzalanClearsTheRidge)
{
    namespace R = DcRagefireChasm;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(R::MAP_ID, R::NPC_BAZZALAN);
    RagefireRowIsSane(r, R::RIDGE_ROOM_RADIUS, R::RIDGE_ROOM_PULLOUT);
    EXPECT_FALSE(r->hasYBand);
    for (RagefireSpawn const& s : { RagefireSpawn{ 48749, -385.16f, 156.60f, 7.74f, 2.0f },
                                    RagefireSpawn{ 48751, -396.31f, 150.83f, 7.82f, 2.0f },
                                    RagefireSpawn{ 48747, -404.93f, 181.38f, 6.58f, 0.0f },
                                    RagefireSpawn{ 48748, -412.36f, 177.34f, 6.71f, 0.0f } })
        EXPECT_TRUE(RagefireAlwaysKept(*r, RFC_BAZZALAN, s)) << s.guid;
    // The hall neck 30yd below.
    EXPECT_FALSE(RagefireKept(*r, RFC_BAZZALAN, { -367.06f, 173.70f, -22.01f }));
    EXPECT_FALSE(RagefireKept(*r, RFC_BAZZALAN, { -367.19f, 180.43f, -22.16f }));
}

TEST(RoomAggroRegistryTest, RagefireTaragamanTakesThePatrolAndTheWestArmFoot)
{
    namespace R = DcRagefireChasm;
    RoomAggroBoss const* r = RoomAggroRegistry::Find(R::MAP_ID, R::NPC_TARAGAMAN);
    RagefireRowIsSane(r, R::PLATEAU_ROOM_RADIUS, R::PLATEAU_ROOM_PULLOUT);

    // The Enforcer patrol (waypoint path 487070, a closed ping-pong): room trash at
    // every point of every leg, so the pull never opens while it is alive.
    std::vector<RagefirePos> const path = {
        { -263.52f, 150.47f, -19.00f }, { -275.88f, 149.00f, -22.23f }, { -283.07f, 148.15f, -25.43f },
        { -275.88f, 149.00f, -22.23f }, { -263.52f, 150.47f, -19.00f }, { -247.89f, 140.20f, -18.57f },
        { -225.04f, 153.12f, -19.04f }, { -212.73f, 154.86f, -21.48f }, { -199.50f, 155.41f, -25.24f },
        { -212.73f, 154.86f, -21.48f }, { -225.04f, 153.12f, -19.04f }, { -240.89f, 165.03f, -18.55f },
    };
    float nearest = 100.0f;
    for (std::size_t i = 0; i < path.size(); ++i)
    {
        RagefirePos const& a = path[i];
        RagefirePos const& b = path[(i + 1) % path.size()];
        for (int k = 0; k <= 40; ++k)
        {
            float const t = k / 40.0f;
            RagefirePos const p = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
            nearest = std::min(nearest, RagefireDist(RFC_TARAGAMAN, p));
            EXPECT_TRUE(RagefireKept(*r, RFC_TARAGAMAN, p)) << "leg " << i << " at " << p.x << ", " << p.y;
        }
    }
    EXPECT_NEAR(nearest, 7.06f, 0.1f);   // it walks inside his 12yd detection
    EXPECT_GT(nearest, r->pullOutRadius + 0.5f);

    // The -x arm's foot, where the party arrives.
    EXPECT_TRUE(RagefireKept(*r, RFC_TARAGAMAN, { -283.76f, 152.50f, -25.32f }));   // 48711
    EXPECT_TRUE(RagefireKept(*r, RFC_TARAGAMAN, { -283.59f, 143.86f, -25.30f }));   // 48708
    // The other arms' guards: past his hub, 6yd below it, never a reason to cross it.
    for (RagefirePos const& p : { RagefirePos{ -237.42f, 192.85f, -24.36f }, RagefirePos{ -230.13f, 191.09f, -24.92f },
                                  RagefirePos{ -267.37f, 110.76f, -24.94f }, RagefirePos{ -257.46f, 106.18f, -24.94f },
                                  RagefirePos{ -198.96f, 151.23f, -25.14f },
                                  RagefirePos{ -199.19f, 159.01f, -25.14f } })
        EXPECT_FALSE(RagefireKept(*r, RFC_TARAGAMAN, p)) << p.x << ", " << p.y;
}
