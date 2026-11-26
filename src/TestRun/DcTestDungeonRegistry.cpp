/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTestDungeonRegistry.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "DBCStores.h"
#include "Log.h"
#include "PlayerbotAIConfig.h"

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"

#include "TestRun/DcTestComp.h"
#include "TestRun/DcTestGearTiers.h"
#include "TestRun/DcTestRunRecord.h"
#include "Util/DcPlayerbotsConfig.h"

namespace DcTestDungeonRegistry
{
    std::vector<Row> const& All()
    {
        // Entrances = areatrigger_teleport targets (acore_world). Stratholme
        // uses the main (Crusaders' Square) gate, not the service entrance.
        // Dire Maul East uses the courtyard portal by Pusillin's start.
        // Every 3.3.5a 5-man dungeon, curated or not — uncurated maps ride the
        // auto-derived roster alone, and a failing run there is exactly the
        // signal the harness exists to produce. Sorted by recommended level.
        static std::vector<Row> const rows = {
            // --- Classic ---------------------------------------------------
            // Ragefire Chasm: level 17 against its level-16 bosses. Heroic (the rework,
            // level-60 bosses) is offered at the classic cap. No no-progress override:
            // the 800yd of trash from Oggleflint to Jergosh has no kill credit in it, but
            // the clock re-arms on every pull, target and yard closed, not on kills alone.
            { "rfc",             "Ragefire Chasm",                389,     3.81f,   -14.82f,  -17.84f, 4.390f, 17,
              "", /*heroicLevel*/ 60 },
            { "wc",              "Wailing Caverns",                43,  -163.49f,   132.90f,  -73.66f, 5.830f, 22, "" },
            // Deadmines runs at 24, well above the 18 the instance nominally opens at.
            // Its back half is a level-18 party's problem: Gilnid is a level-20
            // elite and his foundry holds 19 level-18 ELITES, so at 18 the party
            // fights same-level elites the whole way in with no level margin at
            // all. See the RoomAggroRegistry row for map 36.
            { "deadmines",       "The Deadmines",                  36,   -16.40f,  -383.07f,   61.78f, 1.860f, 24, "" },
            { "sfk",             "Shadowfang Keep",                33,  -229.13f,  2109.18f,   76.89f, 1.267f, 24, "" },
            { "stockade",        "The Stockade",                   34,    54.23f,     0.28f,  -18.34f, 6.260f, 24, "" },
            { "bfd",             "Blackfathom Deeps",              48,  -151.89f,   106.96f,  -39.87f, 4.530f, 24, "" },
            // Gnomeregan: level 28 against the rework's level-28 bosses (heroic 60, at the
            // classic cap). Two Finder sections on one connected map (see
            // GnomereganEvents), each its own row whose token IS the run wing, plus
            // the whole dungeon, `gnomer`, with the Alert System detour through the
            // Workshop Door. `gnomer` and section 1 enter at area trigger 324's
            // landing (Finder 14's start is 2yd above that floor), section 2 at the
            // Train Depot (area trigger 523, Finder 320). No no-progress override:
            // every leg is boss to boss through trash, and the 1,198yd walk to
            // Electrocutioner re-arms the clock yard by yard.
            { "gnomer",          "Gnomeregan",                     90,  DcGnomeregan::ENTRANCE_X,
              DcGnomeregan::ENTRANCE_Y, DcGnomeregan::ENTRANCE_Z, DcGnomeregan::ENTRANCE_O, 28, "",
              /*heroicLevel*/ 60 },
            { "gnomer-s1",       "Gnomeregan: section 1",          90,  DcGnomeregan::ENTRANCE_X,
              DcGnomeregan::ENTRANCE_Y, DcGnomeregan::ENTRANCE_Z, DcGnomeregan::ENTRANCE_O, 28, "Section 1",
              /*heroicLevel*/ 60 },
            { "gnomer-s2",       "Gnomeregan: The Workshop",       90,  DcGnomeregan::DEPOT_X,
              DcGnomeregan::DEPOT_Y, DcGnomeregan::DEPOT_Z, DcGnomeregan::DEPOT_O, 28, "The Workshop",
              /*heroicLevel*/ 60 },
            { "rfk",             "Razorfen Kraul",                 47,  1943.00f,  1544.63f,   82.00f, 1.380f, 30, "" },
            // Scarlet Monastery is four Finder sections on one map (see
            // ScarletMonasteryEvents); each row enters at its section's start
            // (the areatrigger / lfg_dungeon_template spot) and its token IS the
            // run wing. The rework's section aliases sm-s1..sm-s4 name the same
            // rows. Levels: the section's boss level (the hub: Graveyard 32,
            // Library 34-35, Armory 37, Cathedral 40).
            { "sm-gy",           "Scarlet Monastery: Graveyard",  189,  1688.99f,  1053.48f,   18.68f, 0.001f, 32,
              "Graveyard" },
            { "sm-lib",          "Scarlet Monastery: Library",    189,   255.35f,  -209.09f,   18.68f, 6.267f, 35, "Library" },
            { "sm-arm",          "Scarlet Monastery: Armory",     189,  1610.83f,  -323.43f,   18.67f, 6.280f, 37,
              "Armory" },
            { "sm-cath",         "Scarlet Monastery: Cathedral",  189,   855.68f,  1321.50f,   18.67f, 0.002f, 40, "Cathedral" },
            { "rfd",             "Razorfen Downs",                129,  2592.55f,  1107.50f,   51.29f, 4.740f, 40, "" },
            { "uldaman",         "Uldaman",                        70,  -226.80f,    49.09f,  -46.03f, 1.390f, 44, "" },
            // Zul'Farrak: level 46 against the rework's level 44-46 bosses; heroic (the
            // rework's own, level-60 bosses) at the classic cap. No no-progress
            // override: the Pyramid's waves and the crew re-arm the clock fight by
            // fight, and the global 600s covers the 2,373yd of route.
            { "zf",              "Zul'Farrak",                    209,  1213.52f,   841.59f,    8.93f, 6.090f, 46,
              "", /*heroicLevel*/ 60 },
            // Maraudon: the whole dungeon, then its three dungeon-finder wings at
            // their lfg_dungeon_template entrances (see MaraudonEvents). The row's
            // token IS the run wing; `maraudon` clears all of it.
            { "maraudon",        "Maraudon",                      349,  1019.69f,  -458.31f,  -43.43f, 0.310f, 48, "" },
            { "mara-orange",     "Maraudon: Orange Crystals",     349,  1019.69f,  -458.31f,  -43.43f, 0.310f, 46, "Orange" },
            { "mara-purple",     "Maraudon: Purple Crystals",     349,   752.91f,  -616.53f,  -33.11f, 1.370f, 46, "Purple" },
            { "mara-pw",         "Maraudon: Pristine Waters",     349,   495.70f,    17.34f,  -96.31f, 3.119f, 48, "Pristine Waters" },
            { "st",              "The Temple of Atal'Hakkar",     109,  -319.24f,    99.90f, -131.85f, 3.190f, 52, "" },
            // Blackrock Depths is several dungeons on one map (see
            // BlackrockDepthsEvents), the reworked dungeon's four Finder
            // sections. Each row enters where its Finder entry does and runs at
            // its section's boss level: section 1, the Detention Block
            // (Gerstahn -> the Ring of Law), at the portal; section 2,
            // Shadowforge City (Loregrain -> Bael'Gar), at S2g in the Ring's
            // gallery, with one Dark Keeper Key for the Vault (runItemId); section
            // 3, the Manufactory (the Shadowforge Lock -> Argelmach), in the East
            // Garrison by the lever; section 4, the Imperial Seat (the Grim
            // Guzzler -> the Emperor), at S4b on the lower bar floor. The retired
            // `brd` and `brd-db` (the stock Gerstahn -> Bael'Gar wing) tokens
            // alias to `brd-s1`, `brd-uc` (the stock Upper City) to `brd-s4`.
            { "brd-s1",          "Blackrock Depths: Detention Block", 230, 456.93f,  34.09f,  -68.09f, 4.712f, 52,
              "Detention Block" },
            { "brd-s2",          "Blackrock Depths: Shadowforge City", 230, DcBlackrockDepths::S2_START_X,
              DcBlackrockDepths::S2_START_Y, DcBlackrockDepths::S2_START_Z, DcBlackrockDepths::S2_START_O, 53,
              "Shadowforge City", 0, nullptr, {}, {}, 0, 0, 0, /*runItemId*/ DcBlackrockDepths::ITEM_DARK_KEEPER_KEY },
            { "brd-s3",          "Blackrock Depths: Manufactory", 230, DcBlackrockDepths::S3_START_X,
              DcBlackrockDepths::S3_START_Y, DcBlackrockDepths::S3_START_Z, DcBlackrockDepths::S3_START_O, 55,
              "Manufactory" },
            // Section 4's no-progress net is wider than the global 600s: in the
            // Lyceum a lost torch costs a 300s Flame Keeper respawn (plus the
            // 330s torch-wait stall it is allowed) between two objective latches.
            { "brd-s4",          "Blackrock Depths: Imperial Seat", 230, DcBlackrockDepths::S4_START_X,
              DcBlackrockDepths::S4_START_Y, DcBlackrockDepths::S4_START_Z, DcBlackrockDepths::S4_START_O, 56,
              "Imperial Seat", 0, nullptr, {}, {}, 0, 0, /*noProgressS*/ 900 },
            // Blackrock Spire is two dungeons on one map (see BlackrockSpireEvents).
            // LBRS enters at the shared portal; UBRS drops the party inside the
            // shared hall facing the Dragonspine Door (GO 164725), where a UBRS
            // run starts in practice. The retired `brs` token aliases to `lbrs`.
            // UBRS gets the Upper City's 900s no-progress budget: Emberseer's
            // ~69s release after the altar and the stadium's five waves are
            // legitimate quiet stretches between objective latches.
            { "lbrs",            "Lower Blackrock Spire",         229,    78.51f,  -225.04f,   49.84f, 5.100f, 58, "LBRS" },
            { "ubrs",            "Upper Blackrock Spire",         229,   105.00f,  -320.00f,   65.50f, 0.041f, 60, "UBRS",
              0, nullptr, {}, {}, 0, 0, /*noProgressS*/ 900 },
            { "dm-east",         "Dire Maul: East",               429,    44.45f,  -154.82f,   -2.71f, 0.000f, 58, "East" },
            { "dm-west",         "Dire Maul: West",               429,   -62.97f,   159.87f,   -3.46f, 3.148f, 60, "West" },
            { "dm-north",        "Dire Maul: North",              429,   255.25f,   -16.06f,   -2.59f, 4.700f, 60, "North" },
            { "scholo",          "Scholomance",                   289,   196.37f,   127.05f,  134.91f, 6.090f, 60, "" },
            { "strat",           "Stratholme",                    329,  3593.15f, -3646.56f,  138.50f, 5.330f, 60, "" },
            // --- The Burning Crusade --------------------------------------
            { "ramparts",        "Hellfire Ramparts",             543, -1355.24f,  1641.12f,   68.25f, 0.669f, 62, "", 70 },
            { "blood-furnace",   "The Blood Furnace",             542,    -4.00f,    14.64f,  -44.80f, 4.887f, 63, "", 70 },
            { "slave-pens",      "The Slave Pens",                547,   120.10f,  -131.96f,   -0.80f, 1.476f, 64, "", 70 },
            { "underbog",        "The Underbog",                  546,     9.71f,   -16.20f,   -2.75f, 5.571f, 64, "", 70 },
            { "mana-tombs",      "Mana-Tombs",                    557,     0.02f,     0.95f,   -0.95f, 3.032f, 66, "", 70 },
            { "auchenai",        "Auchenai Crypts",               558,   -21.90f,     0.16f,   -0.12f, 0.035f, 67, "", 70 },
            { "sethekk",         "Sethekk Halls",                 556,    -4.68f,    -0.09f,    0.01f, 0.035f, 68, "", 70 },
            { "old-hillsbrad",   "Old Hillsbrad Foothills",       560,  2741.87f,  1315.25f,   14.04f, 2.960f, 68, "", 70 },
            { "shadow-labs",     "Shadow Labyrinth",              555,     0.49f,    -0.22f,   -1.13f, 3.159f, 70, "", 70 },
            { "steamvault",      "The Steamvault",                545,   -13.84f,     6.75f,   -4.26f, 0.000f, 70, "", 70 },
            { "shattered-halls", "The Shattered Halls",           540,   -40.87f,   -19.75f,  -13.81f, 1.111f, 70, "", 70 },
            { "black-morass",    "The Black Morass",              269, -1496.24f,  7034.70f,   32.56f, 1.757f, 70, "", 70 },
            { "botanica",        "The Botanica",                  553,    40.04f,   -28.61f,   -1.12f, 2.359f, 70, "", 70 },
            { "mechanar",        "The Mechanar",                  554,   -28.91f,     0.68f,   -1.81f, 0.035f, 70, "", 70 },
            { "arcatraz",        "The Arcatraz",                  552,    -1.23f,     0.01f,   -0.20f, 0.016f, 70, "", 70 },
            { "mgt",             "Magisters' Terrace",            585,     7.09f,    -0.45f,   -2.80f, 0.050f, 70, "", 70 },
            // --- Wrath of the Lich King -----------------------------------
            { "uk",              "Utgarde Keep",                  574,   153.79f,   -86.55f,   12.55f, 0.304f, 70, "", 80 },
            { "nexus",           "The Nexus",                     576,   145.87f,   -10.55f,  -16.64f, 1.528f, 71, "", 80 },
            { "an",              "Azjol-Nerub",                   601,   413.31f,   795.97f,  831.35f, 5.500f, 74, "", 80 },
            { "ok",              "Ahn'kahet: The Old Kingdom",    619,   333.35f, -1109.94f,   69.77f, 0.553f, 74, "", 80 },
            { "dtk",             "Drak'Tharon Keep",              600,  -517.34f,  -487.98f,   11.01f, 4.831f, 75, "", 80 },
            { "vh",              "The Violet Hold",               608,  1808.82f,   803.93f,   44.36f, 6.282f, 75, "", 80 },
            { "gundrak",         "Gundrak",                       604,  1891.84f,   832.17f,  176.67f, 2.109f, 77, "", 80 },
            { "hos",             "Halls of Stone",                599,  1153.24f,   806.16f,  195.94f, 4.715f, 78, "", 80 },
            { "hol",             "Halls of Lightning",            602,  1331.47f,   259.62f,   53.40f, 4.772f, 80, "", 80 },
            { "cos",             "The Culling of Stratholme",     595,  1431.10f,   556.92f,   36.69f, 5.160f, 80, "", 80 },
            { "oculus",          "The Oculus",                    578,  1055.93f,   986.85f,  361.07f, 5.745f, 80, "", 80 },
            { "up",              "Utgarde Pinnacle",              575,   584.12f,  -327.97f,  110.14f, 3.122f, 80, "", 80 },
            { "toc",             "Trial of the Champion",         650,   805.23f,   618.04f,  412.39f, 3.146f, 80, "", 80 },
            { "fos",             "The Forge of Souls",            632,  4922.86f,  2175.63f,  638.73f, 2.004f, 80, "", 80 },
            { "pos",             "Pit of Saron",                  658,   435.74f,   212.41f,  528.71f, 6.256f, 80, "", 80 },
            { "hor",             "Halls of Reflection",           668,  5239.01f,  1932.64f,  707.70f, 0.801f, 80, "", 80 },

            // --- RAIDS (raid-support Plan D/E). Entrances are the world-DB
            // areatrigger targets (MC 2886, BWL 3726, Gruul 4535, Kara 4131); no heroic
            // mode (heroicLevel stays 0 — a raid's size is its difficulty); a
            // raid run picks its size via `size=` (default 10 for iteration
            // speed — see the raid-support plan). Classic rows run at 60.
            { "mc",              "Molten Core",                   409,  1091.89f,  -466.99f, -105.08f, 3.142f, 60, "" },
            { "bwl",             "Blackwing Lair",                469, -7673.03f, -1106.08f,  396.65f, 0.178f, 60, "" },
            // The other classic raids, from the same table (ZG 3928, AQ20 4008,
            // AQ40 4010, Onyxia 2848). Onyxia runs at 80: the 3.3.5 core ships her
            // level-80 revamp, so a realm that de-levels her passes `level=60`.
            { "zg",              "Zul'Gurub",                     309, -11916.10f, -1230.53f,  92.53f, 4.719f, 60, "" },
            { "aq20",            "Ruins of Ahn'Qiraj",            509, -8429.74f,  1512.14f,   31.91f, 2.580f, 60, "" },
            { "aq40",            "Temple of Ahn'Qiraj",           531, -8231.33f,  2010.60f,  129.33f, 0.930f, 60, "" },
            { "ony",             "Onyxia's Lair",                 249,    29.16f,   -71.34f,   -8.18f, 4.580f, 80, "" },
            // Gruul's Lair (TBC, level 70, 25-man): two kill-credit encounters
            // — High King Maulgar with his four-ogre council (instance
            // MINIONS of his encounter, despawned on his death), then Gruul —
            // down one linear corridor with 13 trash spawns. Both bosses
            // auto-derive from BossSpawnIndex, so the map has no roster patch
            // and no event file; the Maulgar portcullis (184468) is
            // DOOR_TYPE_PASSAGE on his encounter and opens itself. The
            // playerbots `gruulslair` strategy tanks Krosh with a MAGE and
            // Kiggler with a BALANCE DRUID and expects three tanks; a drawn
            // comp missing any of those leaves that ogre loose (pick the comp
            // on the roster page when it matters).
            { "gruul",           "Gruul's Lair",                  565,    62.78f,    35.46f,   -3.98f, 1.418f, 70, "" },
            // Karazhan (TBC, level 70, 10-man only — MapDifficulty caps it
            // at 10, so the launch form offers no 25 preset and a bigger run
            // is refused before it spawns). Main entrance, areatrigger 4131;
            // no key requirement in dungeon_access_template, min level 68.
            // Kill-credit encounters with a static spawn auto-derive (Moroes,
            // Maiden, Curator, Terestian, Aran, Netherspite, Malchezaar); the
            // other four run on event data (KarazhanEvents.cpp): Attumen has no
            // spawn (engaged through Midnight), Opera credits Barnes (a friendly
            // gossip NPC who starts the play), Chess credits the Status Bar
            // trigger (the chess objective plays the game instead), and
            // Nightbane lands only after the urn.
            { "kara",            "Karazhan",                      532, -11100.00f, -2003.98f,   49.89f, 0.577f, 70, "" },

            // --- SCENARIOS (Karazhan chess plan, T1). A scenario is a slice of
            // a parent row: an in-map drop point, a focus roster and (usually)
            // its own success predicate — see the Row's scenario fields. Kept
            // at the END of the table so every by-map scan (DcRezRecovery's
            // entrance lookup) meets the parent first. Spelled positionally:
            //   { token, name, map, x, y, z, o, level, "", 0 (no heroic),
            //     "<parent>", { focus entries }, InstanceDataEquals(id, v),
            //     graceS, overallTimeoutS, noProgressS },
            // ValidateScenario (gtest-pinned over this table) rejects a row
            // whose parent, map or focus does not line up.

            // Karazhan: Chess (T2). The raid lands on the Gamesman's Hall floor
            // off the board's col-0 edge (the chess objective's own anchor,
            // facing the board), everything but the chess objective is skipped,
            // and the run passes once GetData(DATA_CHESS_EVENT) reads DONE — with
            // 60s of grace for the conductor to loot the Dust Covered Chest. The
            // event's first step (walk into the hall) is satisfied on landing.
            // Up to three games of fifteen minutes plus the retries fit in the
            // half hour; the game bumps the event progress sequence on every
            // accepted move, piece death and phase change, so five minutes with
            // none of those is a real stall. No pre-loot: the harness revives and
            // unbinds at teardown, which also clears the win's perm-bind.
            { "kara-chess",      "Karazhan: Chess",               532,
              DcKarazhan::HALL_X, DcKarazhan::HALL_Y, DcKarazhan::HALL_Z, 5.608f, 70, "", 0,
              "kara", { BossRosterRegistry::ObjectiveEntry(DcKarazhan::OBJ_CHESS) },
              InstanceDataEquals(DcKarazhan::DATA_CHESS_EVENT, DcKarazhan::CHESS_EVENT_DONE),
              60, 1800, 300 },

            // Zul'Farrak: the pool (HANDOFF section 3.12, the bar's second run). The
            // `zf` run is complete at Ukorz: the pool objective is optional by design
            // and starts skipped. This scenario focuses it (the focus opt-in takes it
            // off the skipped set), from the entrance: Velratha, then Gahz'rilla, and
            // the run passes on DC's all-cleared once his slot reads DONE. No
            // predicate, no overrides.
            { "zf-pool",         "Zul'Farrak: the pool",          209,  1213.52f,   841.59f,    8.93f, 6.090f, 46,
              "", 0, "zf", { BossRosterRegistry::ObjectiveEntry(DcZulFarrak::OBJ_POOL) } },
        };
        return rows;
    }

    std::vector<Alias> const& Aliases()
    {
        static std::vector<Alias> const aliases = {
            { "brs", "lbrs" },   // Blackrock Spire before the LBRS/UBRS split
            { "brd", "brd-s1" },    // Blackrock Depths before the Detention Block/Upper City split
            { "brd-db", "brd-s1" }, // the stock Detention Block (to Bael'Gar), before the Finder sections
            { "brd-uc", "brd-s4" }, // the stock Upper City (Lock -> Emperor), before the Finder sections
            { "sm-s1", "sm-gy" },   // Scarlet Monastery Finder section 1, the Graveyard
            { "sm-s2", "sm-lib" },  // Scarlet Monastery Finder section 2, the Library
            { "sm-s3", "sm-arm" },  // Scarlet Monastery Finder section 3, the Armory
            { "sm-s4", "sm-cath" }, // Scarlet Monastery Finder section 4, the Cathedral
        };
        return aliases;
    }

    char const* AliasTarget(std::string const& token)
    {
        for (Alias const& a : Aliases())
            if (token == a.from)
                return a.to;
        return nullptr;
    }

    Row const* Find(std::string const& tokenOrMapId)
    {
        return Find(tokenOrMapId, All());
    }

    Row const* Find(std::string const& tokenOrMapId, std::vector<Row> const& rows)
    {
        if (tokenOrMapId.empty())
            return nullptr;

        for (Row const& row : rows)
            if (tokenOrMapId == row.token)
                return &row;

        if (char const* target = AliasTarget(tokenOrMapId))
        {
            for (Row const& row : rows)
                if (std::string(target) == row.token)
                {
                    LOG_INFO("playerbots.dungeonclear",
                             "[dc-test] dungeon token '{}' is an alias — resolving it as '{}'",
                             tokenOrMapId, target);
                    return &row;
                }
            return nullptr;
        }

        // Numeric fallback — only unambiguous for maps with a single row.
        char* end = nullptr;
        unsigned long const asMap = std::strtoul(tokenOrMapId.c_str(), &end, 10);
        if (!end || *end != '\0' || asMap == 0)
            return nullptr;

        Row const* hit = nullptr;
        for (Row const& row : rows)
        {
            // A scenario is a slice of its parent, never "the dungeon on map N":
            // `.dc test start 532` must keep meaning the whole of Karazhan no
            // matter how many scenarios hang off it.
            if (IsScenario(row) || row.mapId != asMap)
                continue;
            if (hit)
                return nullptr;  // wing-split map: demand a wing token
            hit = &row;
        }
        return hit;
    }

    std::string DescribePredicate(SuccessPredicate const& p)
    {
        switch (p.kind)
        {
            case SuccessPredicate::Kind::InstanceDataEquals:
                return "instanceData(" + std::to_string(p.dataId) + ")==" + std::to_string(p.value);
            case SuccessPredicate::Kind::None:
                break;
        }
        return "";
    }

    std::string ValidateScenario(Row const& row, std::vector<Row> const& rows,
                                 std::vector<std::uint32_t> const* rosterEntries)
    {
        if (!IsScenario(row))
        {
            // The scenario-only knobs mean nothing on a full-dungeon row, and a
            // focus there would silently skip most of the dungeon.
            if (!row.focusEntries.empty() || row.success.IsSet() || row.successGraceS)
                return "focus/success set on a row that is not a scenario (no scenarioOf)";
            return "";
        }

        std::string const parentToken = row.scenarioOf;
        if (parentToken.empty())
            return "scenarioOf is empty";
        if (parentToken == row.token)
            return "scenario names itself as its parent";

        Row const* parent = nullptr;
        for (Row const& r : rows)
            if (parentToken == r.token)
            {
                parent = &r;
                break;
            }
        if (!parent)
            return "parent '" + parentToken + "' is not in the catalogue";
        if (IsScenario(*parent))
            return "parent '" + parentToken + "' is itself a scenario";
        if (parent->mapId != row.mapId)
            return "scenario map " + std::to_string(row.mapId) + " differs from parent '" +
                   parentToken + "' map " + std::to_string(parent->mapId);

        if (row.heroicLevel)
            return "scenarios offer no heroic mode (heroicLevel must be 0)";

        if (row.focusEntries.empty())
            return "scenario has no focusEntries";
        std::set<std::uint32_t> seen;
        for (std::uint32_t entry : row.focusEntries)
        {
            if (entry == 0)
                return "focus entry 0";
            if (!seen.insert(entry).second)
                return "duplicate focus entry " + std::to_string(entry);
            if (rosterEntries &&
                std::find(rosterEntries->begin(), rosterEntries->end(), entry) ==
                    rosterEntries->end())
                return "focus entry " + std::to_string(entry) + " is not on map " +
                       std::to_string(row.mapId) + "'s roster";
        }

        if (row.success.IsSet() && row.success.dataId == 0)
            return "success predicate names no instance data id";
        if (row.successGraceS && !row.success.IsSet())
            return "successGraceS without a success predicate";
        return "";
    }

    std::string ScenarioSidecarFields(Row const& row)
    {
        if (!IsScenario(row))
            return "";
        using DcTestRunRecord::EscapeJson;
        std::ostringstream s;
        s << ",\"scenario\":true,\"scenarioOf\":\"" << EscapeJson(row.scenarioOf)
          << "\",\"focus\":[";
        for (std::size_t i = 0; i < row.focusEntries.size(); ++i)
            s << (i ? "," : "") << row.focusEntries[i];
        s << "],\"success\":\"" << EscapeJson(DescribePredicate(row.success)) << '"'
          << ",\"successGraceS\":" << row.successGraceS
          << ",\"overallTimeoutS\":" << row.overallTimeoutS
          << ",\"noProgressS\":" << row.noProgressS;
        return s.str();
    }

    std::uint32_t ExpansionOf(Row const& row)
    {
        MapEntry const* mapEntry = sMapStore.LookupEntry(row.mapId);
        return mapEntry ? mapEntry->Expansion() : 0u;
    }

    std::uint32_t MaxPlayers(Row const& row)
    {
        if (MapDifficulty const* diff = GetMapDifficultyData(row.mapId, Difficulty(0)))
            if (diff->maxPlayers)
                return diff->maxPlayers;
        MapEntry const* mapEntry = sMapStore.LookupEntry(row.mapId);
        return mapEntry ? mapEntry->maxPlayers : 0u;
    }

    std::vector<std::uint32_t> RaidSizePresets(std::uint32_t cap)
    {
        std::vector<std::uint32_t> out;
        for (std::uint32_t preset : {10u, 25u})
            if (SizeFits(preset, cap))
                out.push_back(preset);
        return out;
    }

    std::uint32_t RaidSizeMax(std::uint32_t cap)
    {
        auto const hardMax = static_cast<std::uint32_t>(DcTestComp::kMaxPartySize);
        return cap == 0 ? hardMax : std::min(cap, hardMax);
    }

    void WriteSidecar()
    {
        char const* path = "dc_test_dungeons.json";
        if (char const* env = std::getenv("DC_TEST_DUNGEONS_FILE"))
            if (env[0])
                path = env;

        using DcTestRunRecord::EscapeJson;

        // The ilvl ladder the dashboard's start form offers, per row and per
        // difficulty (heroic runs at a different level, so a TBC row's two
        // ladders differ). Emitted here rather than computed in the dashboard so
        // the two can't drift: this file IS the catalogue.
        auto appendLadder = [](std::ostringstream& out, std::uint32_t mapId, std::uint32_t level)
        {
            out << '[';
            bool firstChoice = true;
            for (DcTestGearTiers::Choice const& choice : DcTestGearTiers::Ladder(mapId, level))
            {
                if (!firstChoice)
                    out << ',';
                firstChoice = false;
                out << "{\"ilvl\":" << choice.ilvl << ",\"label\":\"" << EscapeJson(choice.label)
                    << "\"}";
            }
            out << ']';
        };

        std::ostringstream s;
        s << "{\"limits\":{\"maxConcurrent\":"
          << DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxConcurrent")
          << ",\"maxPlans\":"
          << DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxPlans")
          << ",\"planMaxTotal\":"
          << DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.Plan.MaxTotal")
          // Feature flag: this server takes `.dc test plan start pool=…`,
          // edit, pause and resume (the Test Deck's Continuous page).
          << ",\"planPool\":true}";

        // What a run gets when it asks for nothing. Read once at startup like
        // the rest of this file, so it is a label for the form's "server
        // default" entry, not an authority — the worldserver resolves the real
        // values when the run starts.
        s << ",\"gearDefaults\":{\"ilvl\":"
          << (DC_PB_CONFIG(AutoGearScoreLimit, autoGearScoreLimit) > 0 ? DC_PB_CONFIG(AutoGearScoreLimit, autoGearScoreLimit) : 0)
          << ",\"quality\":"
          << (DC_PB_CONFIG(AutoGearQualityLimit, autoGearQualityLimit) > 0 ? DC_PB_CONFIG(AutoGearQualityLimit, autoGearQualityLimit)
                                                          : 3)
          << "},\"qualities\":[";
        for (std::uint32_t q = 1; q <= 5; ++q)
            s << (q > 1 ? "," : "") << "{\"v\":" << q << ",\"label\":\""
              << DcTestGearTiers::QualityName(q) << "\"}";
        // Retired tokens -> their replacements, so the Deck and streamcast can
        // normalise stored tokens (soak pools, queued requests) instead of
        // rejecting them.
        s << "],\"aliases\":{";
        for (std::size_t i = 0; i < Aliases().size(); ++i)
            s << (i ? "," : "") << '"' << EscapeJson(Aliases()[i].from) << "\":\""
              << EscapeJson(Aliases()[i].to) << '"';
        s << "},\"dungeons\":[";
        bool first = true;
        for (Row const& row : All())
        {
            if (!first)
                s << ',';
            first = false;
            s << "{\"token\":\"" << EscapeJson(row.token) << '"'
              << ",\"name\":\"" << EscapeJson(row.name) << '"'
              << ",\"mapId\":" << row.mapId
              << ",\"expansion\":" << ExpansionOf(row)
              << ",\"level\":" << row.recommendedLevel
              << ",\"heroicLevel\":" << row.heroicLevel
              << ",\"wing\":\"" << EscapeJson(row.wing) << '"'
              // Scenario rows: the Deck shelves them under the parent dungeon,
              // locks the size to the parent's default and hides heroic.
              << ScenarioSidecarFields(row);
            // RAID rows (raid-support Plan D): tell the dashboard's launch
            // form to offer the size control, with the module's own bounds so
            // the two can't drift. The upper bound and presets are capped by
            // the map's own player limit (Karazhan: 10, no 25 preset).
            // defaultSize 10 mirrors the plan's iteration-speed choice; the
            // worldserver still validates.
            if (MapEntry const* mapEntry = sMapStore.LookupEntry(row.mapId);
                mapEntry && mapEntry->IsRaid())
            {
                std::uint32_t const cap = MaxPlayers(row);
                s << ",\"raid\":true"
                  << ",\"sizeMin\":" << DcTestComp::kMinPartySize
                  << ",\"sizeMax\":" << RaidSizeMax(cap) << ",\"sizePresets\":[";
                bool firstPreset = true;
                for (std::uint32_t preset : RaidSizePresets(cap))
                {
                    s << (firstPreset ? "" : ",") << preset;
                    firstPreset = false;
                }
                s << "],\"defaultSize\":" << std::min(kRaidDefaultSize, RaidSizeMax(cap));
            }
            s << ",\"gear\":";
            appendLadder(s, row.mapId, row.recommendedLevel);
            if (row.heroicLevel)
            {
                s << ",\"gearHeroic\":";
                appendLadder(s, row.mapId, row.heroicLevel);
            }
            s << '}';
        }
        s << "]}";

        std::ofstream f(path, std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return;
        f << s.str() << '\n';
    }
}
