/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"

#include <unordered_map>

// --- Scarlet Monastery (map 189) — ROOM-AGGRO PRE-CLEAR -------------------
// One conditional room-aggro event serves every RoomAggroRegistry boss on the
// map. Condition 3 (shared — see SharedConditions.cpp) reads DUE while the
// room-trash value (the RoomAggroRegistry geometry around the LIVE boss, minus
// the boss aggro sphere / unreachable / door-blocked, with the RoomClearTimeout
// give-up valve) still has anything to clear, and the lone KillCreature(0 = room
// trash mode) step gates the boss pull until it is empty. DcRunEventAction drives
// the actual engage (nearest room trash first); the spatial logic stays in
// DungeonClearRoomTrashValue. Required (hold the pull until clear or the value
// gives up) and NEVER latched — the same row re-fires for each room-aggro boss on
// the map: Ironspine's lawn (his parts land 15-20yd out and the group chases
// them), the council's lawn spot, Loksey's kennel hall, Kell's long hall (the
// trash on his case spots), Hollis Grey's reading hall (twelve Scarlet spawns
// round his pocket), Crell's storeroom, Halden's chamber and Thorne's garden.
// The rows are in RoomAggroRegistry.cpp.
//
// The stock Cathedral rows (Mograine and Whitemane, CATHEDRAL_PULL_RANGE 80:
// Mograine's engage dragged the whole cathedral into combat) are gone with wave
// 2: the rework deletes the nave's 42 spawns, Mograine never aggroes on
// proximity, and the Last Stand (event 2 below) is the Cathedral's set piece.
//
// --- the LAST STAND (section 4), ANCHORED between Thorne and Whitemane ------
// The keyless Chapel Door is the start: its use (go_sm_chapel_door) tells the
// instance, which puts slot 5 IN_PROGRESS and calls the first company. So only
// this event opens it, after the landing's door Centurions are dead and with
// the party gathered on the tank: the door and its lever are navigation-ignored
// (DcEventDoorRegistry), so the generic door rung never Use()s it on sight and
// never owns a stall for it (the BRD Dark Coffer lesson: a door whose use starts
// a fight). The event then holds the group in the nave until the instance reads
// the stand DONE (Mograine's 1% fall). Mograine himself is never pulled: he is
// passive on the altar steps until the third company falls, then comes down to
// the group (DcNeverTargetRegistry keeps the clear from going looking for him).
//
// PERSISTENT: the fights are >1s Drive gaps, and a persistent anchored event
// owns the pull and tight-follows the party (the Hurley gather lesson). Every
// step is idempotent under a rewind: ClearRadius is a gate, UseGO returns Done on
// a door that already stands open (a second click would shut it), MoveTo
// re-arrives. A wipe leaves the door open and slot 5 NOT_STARTED; the run comes
// back to the anchor, and the walk from the door to the hold crosses the strip
// (x 1074-1082) where the instance restarts the stand. A cold load with the
// stand DONE opens the door and completes the anchor (doneBossStateIndex 5).
// The chest Mograine leaves (301330 / 301331) is a chest: IgnoreChests.

void RegisterScarletMonasteryEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(189, 1, "Clear the room (room-aggro pre-clear)")
                      .Conditional(&DcRoomAggroPreClearCondition)
                      .KillCreature(/*room trash*/ 0)
                      .Build());

    namespace S = DcScarletMonastery;
    out.push_back(EventBuilder(S::MAP_ID, S::EVENT_LAST_STAND, "The Last Stand")
                      .Anchored(/*orderIndex*/ S::SLOT_LAST_STAND)
                      .Persistent()
                      // 1. The landing's four door Centurions, before the door.
                      .ClearRadius(S::LANDING_X, S::LANDING_Y, S::LANDING_Z, S::LANDING_CLEAR_RADIUS,
                                   S::LANDING_CLEAR_Z_BAND)
                      // 2. Open the Chapel Door: the stand starts.
                      .UseGO(S::GO_CHAPEL_DOOR, /*searchRadius*/ 15.0f)
                      // 3. Into the nave's center aisle (35yd, straight, through
                      //    the crossing strip) ...
                      .MoveTo(S::NAVE_HOLD_X, S::NAVE_HOLD_Y, S::NAVE_HOLD_Z, S::NAVE_HOLD_RADIUS)
                      // 4. ... and hold there, re-centering between companies,
                      //    until Mograine falls (slot 5 DONE).
                      .MoveToHoldUntilBossState(S::NAVE_HOLD_X, S::NAVE_HOLD_Y, S::NAVE_HOLD_Z,
                                                S::NAVE_HOLD_RADIUS, S::SLOT_LAST_STAND,
                                                DcBossStateBit(S::STATE_DONE))
                      .Timeout(S::LAST_STAND_TIMEOUT_MS)
                      .Build());
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterScarletMonasteryRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Scarlet Monastery rework, wave 1: the Graveyard and the Library ----
    // Clear order per Finder section (ORDER_*; each wing's list is filtered
    // before the picker sees it, so the two key ranges never meet):
    //   Graveyard: Vishas -> Ironspine (the lawn) -> the council (the grave
    //              lawn) -> Thalnos (the crypt, the section's end)
    //   Library:   Loksey (the kennel's dead-end alcove, first: 206yd from the
    //              start) -> Kell -> Hollis Grey -> Doan (the section's end)
    // The DBC bits alone would send a Graveyard run to Thalnos (bit 1) before
    // Ironspine (7) and the council (8), and a Library run to Doan (3) before
    // Kell (9) and Hollis Grey (10). Ironspine, Kell and Hollis Grey have
    // kill-creature credits (instance_encounters 1074 / 1076 / 1077), so
    // BossSpawnIndex derives them at their spawns (Ironspine's moved to the lawn
    // by rev_ironspine) and only their order is set here. Off-route bosses
    // (Ironspine, the council, Loksey: section 7 #13 A) stay in the list: the
    // Finder section is complete on its end boss, but DC clears everything.
    //
    // IRONSPINE BEFORE THE COUNCIL (R-1-GY MN-3). Both fight on the lawn, 63yd
    // apart: Ironspine's 40yd arena reaches within 23yd of the council's spot,
    // and the council's 40yd disc within 23yd of his home and of the Haunting
    // Phantasm beside him, so whichever lives while the other is fought can be
    // body-pulled. Ironspine goes first: the walk up from Vishas meets his home
    // (on the lawn, x 1796.5) before the council's spot, so the council-first
    // order would walk the party past him alive; his pull comes from the south
    // (the side the party walks up from) and his parts land within 20yd of him,
    // so a fight held at his home keeps more than the council's aggro reach
    // (~28yd) away from them; and his death takes the Phantasm with him, which
    // removes the reverse case before the council is pulled. Pinned in
    // t/TestRunWing.
    //
    // THE COUNCIL BEFORE THALNOS (D4H). The Desecration's stones refuse a touch
    // while the council lives. DC never touches them (a hard mode is a human's
    // choice; no step on this map names a hard mode object, t/TestDungeonEvent),
    // and the order already kills the council before Thalnos, so a run that
    // reaches him on a lit lockout has nothing left on the lawn.
    //
    // THE COUNCIL has a spell credit (212300, cast by the instance once Azshir
    // and the Fallen Champion are both dead), so BossSpawnIndex never lists it:
    // both units are added on its bit 8 at the lawn spot they fight on. The
    // Champion is the first anchor (the melee the tank picks up); Azshir is a
    // same-key sibling, so a run whose Champion is a corpse (his raises spent
    // on normal) while Azshir still stands heads to Azshir next, and both drop
    // the moment bit 8 sets. Listing Azshir also keeps him a boss entry: the
    // trash scans never camp-pull him as a stray. During the Last Vigil the
    // fallen Champion is alive but NOT_SELECTABLE; the combat target scans
    // already reject that (AttackersValue::IsPossibleTarget) and his anchor
    // stays a live candidate, never a corpse. Both entries are removed first so
    // a database whose council still has a creature credit cannot list them
    // twice.
    {
        namespace S = DcScarletMonastery;
        BossRosterPatch p;
        p.mapId = S::MAP_ID;
        p.remove = { S::NPC_FALLEN_CHAMPION, S::NPC_AZSHIR };
        p.reorder = {
            { S::NPC_VISHAS, S::ORDER_VISHAS },
            { S::NPC_IRONSPINE, S::ORDER_IRONSPINE },
            { S::NPC_THALNOS, S::ORDER_THALNOS },
            { S::NPC_LOKSEY, S::ORDER_LOKSEY },
            { S::NPC_KELL, S::ORDER_KELL },
            { S::NPC_HOLLIS_GREY, S::ORDER_HOLLIS_GREY },
            { S::NPC_DOAN, S::ORDER_DOAN },
        };
        p.add = {
            MakeBossWithBit(S::NPC_FALLEN_CHAMPION, S::MAP_ID, "Azshir the Sleepless and the Fallen Champion",
                            S::CHAMPION_X, S::CHAMPION_Y, S::CHAMPION_Z, S::BIT_COUNCIL, S::ORDER_COUNCIL),
            MakeBossWithBit(S::NPC_AZSHIR, S::MAP_ID, "Azshir the Sleepless",
                            S::AZSHIR_X, S::AZSHIR_Y, S::AZSHIR_Z, S::BIT_COUNCIL, S::ORDER_COUNCIL),
        };
        t.push_back(std::move(p));
    }

    // --- Scarlet Monastery rework, wave 2: the Armory and the Cathedral -----
    // Clear order per Finder section:
    //   Armory:    Crell (the storeroom, down the stairs from the entry hall)
    //              -> Halden (his chamber) -> Herod (the hall, the section's end)
    //   Cathedral: Thorne (the lower garden, 22yd off the route) -> the Last
    //              Stand (the Chapel Door) -> Whitemane (the vigil, the end)
    // The DBC bits alone would send an Armory run to Herod (4) before Crell (11)
    // and Halden (12), and a Cathedral run to Whitemane (6) before Thorne (13),
    // which as the section's terminal would end the run with Thorne alive. Crell,
    // Halden and Thorne have kill-creature credits (instance_encounters 1078 /
    // 1079 / 1080), Herod his stock 448: BossSpawnIndex derives all four at their
    // spawns, so only their order is set here.
    //
    // THE LAST STAND has a spell credit (212301: encounter 449 re-pointed off
    // Fairbanks), so BossSpawnIndex never lists it, and it is not a kill: it is an
    // OBJECTIVE on the landing outside the Chapel Door carrying event 2 (above),
    // complete when slot 5 reads DONE. Mograine (3976) is never a roster entry:
    // the stock patch that put him on Whitemane's bit is gone (D13: never pull
    // him). Fairbanks is removed in case a database still credits 449 to him.
    //
    // WHITEMANE walks out of her chamber when the stand is DONE and kneels at the
    // vigil on the crossing (D14), so she is re-anchored there on her own bit 6
    // (MakeBossWithBit, the council's shape: the bit does not depend on her
    // derived row). Her DB spawn is behind the High Inquisitor's Door, which the
    // instance opens at the stand's end; the vigil spot keeps the route out of
    // her chamber. The tank pulls her by attacking her: she never aggroes on
    // proximity. Herod and Whitemane are their sections' terminals (the wings).
    {
        namespace S = DcScarletMonastery;
        DungeonBossInfo stand = MakeObjective(OBJ(S::OBJ_LAST_STAND), /*encounterIndex*/ S::BIT_LAST_STAND,
                                              S::MAP_ID, "The Last Stand", S::LANDING_X, S::LANDING_Y,
                                              S::LANDING_Z, S::LANDING_ARRIVE_RADIUS, /*gateEntry*/ 0,
                                              /*hook*/ 0, S::EVENT_LAST_STAND, S::ORDER_LAST_STAND);
        stand.doneBossStateIndex = S::SLOT_LAST_STAND;

        BossRosterPatch p;
        p.mapId = S::MAP_ID;
        p.remove = { S::NPC_WHITEMANE, S::NPC_FAIRBANKS, S::NPC_MOGRAINE };
        p.reorder = {
            { S::NPC_CRELL, S::ORDER_CRELL },
            { S::NPC_HALDEN, S::ORDER_HALDEN },
            { S::NPC_HEROD, S::ORDER_HEROD },
            { S::NPC_THORNE, S::ORDER_THORNE },
        };
        p.add = {
            stand,
            MakeBossWithBit(S::NPC_WHITEMANE, S::MAP_ID, "High Inquisitor Whitemane", S::VIGIL_X, S::VIGIL_Y,
                            S::VIGIL_Z, S::BIT_WHITEMANE, S::ORDER_WHITEMANE),
        };
        t.push_back(std::move(p));
    }
}

// --- wing layout (relocated from DungeonWingRegistry) --------------------
void RegisterScarletMonasteryWings(std::unordered_map<uint32, DungeonWingLayout>& store)
{
    // --- Scarlet Monastery (map 189) -----------------------------
    // Four wings, each entered through its own portal off the shared
    // outdoor courtyard (and, in the rework, by an in-map portal at each
    // section's end boss to the next section's start). No walkable route
    // connects them; they sit 600+ yds apart: Graveyard (x~1800, y~1270)
    // and Cathedral (x~1160, y~1370) in the north half, Library (x~130,
    // y~-345) and Armory (x~1965, y~-430) in the south half.
    // isolated == true: filter to the run's wing.
    //
    // EXPLICIT, like Blackrock Depths: each wing is a Finder section
    // (LFG 18 / 165 / 163 / 164, heroic 314-317) with its own last boss,
    // so the wing is a per-run latch — the test row's token, `.dc on
    // <wing>`, the group's LFG dungeon, else the wing region the party
    // stands in (the four map-189 rows of DcNavPenaltyRegistry, cost 1:
    // regions only, they fence nothing). A run ends on its section's
    // terminal boss: walking through the end boss's portal into the next
    // wing does not carry the run on (`.dc on` there re-reads the region).
    // Herod's death lets twenty Scarlet Trainees in through the reopened door
    // (D11): the all-cleared trigger holds the run end while the party fights
    // them (DungeonClearMath::HoldRunEndForFight), so a wipe to the trainees
    // reads as a wipe, not a cleared wing; DC never takes the P3 portal.
    //
    // Entries are the kill-creature credit-entries from
    // instance_encounters (what BossSpawnIndex emits) PLUS every anchor the
    // roster patches add (the council's two units, the Last Stand objective,
    // Whitemane at her vigil). Every section carries its fields: LFG and
    // heroic ids, terminal boss, encounter mask (bit = slot).
    //
    // Each wing's token is its DcTestDungeonRegistry row token (aliases
    // sm-s1..sm-s4 name the sections), so rez recovery regroups a wiped
    // party at ITS wing's entrance.
    namespace S = DcScarletMonastery;
    using DcRoster::OBJ;
    store[S::MAP_ID] = {true, {
        {"Scarlet Monastery (Graveyard)", {
            S::NPC_VISHAS,            // Interrogator Vishas
            S::NPC_IRONSPINE,         // Ironspine
            S::NPC_FALLEN_CHAMPION,   // the Fallen Champion (the council)
            S::NPC_AZSHIR,            // Azshir the Sleepless (the council)
            S::NPC_THALNOS,           // Bloodmage Thalnos
        }, "sm-gy", S::LFG_SECTION_1, /*terminalBossEntry*/ S::NPC_THALNOS, S::SECTION_1_ENCOUNTER_MASK,
           S::LFG_SECTION_1_HEROIC},
        {"Scarlet Monastery (Library)", {
            S::NPC_LOKSEY,            // Houndmaster Loksey
            S::NPC_KELL,              // Ambrose Kell, the Antiquarian
            S::NPC_HOLLIS_GREY,       // Lector Hollis Grey, the Librarian
            S::NPC_DOAN,              // Arcanist Doan
        }, "sm-lib", S::LFG_SECTION_2, /*terminalBossEntry*/ S::NPC_DOAN, S::SECTION_2_ENCOUNTER_MASK,
           S::LFG_SECTION_2_HEROIC},
        {"Scarlet Monastery (Armory)", {
            S::NPC_CRELL,             // Forgemaster Gideon Crell, the Armorer
            S::NPC_HALDEN,            // Drillmaster Corin Halden, the Master-at-Arms
            S::NPC_HEROD,             // Herod
        }, "sm-arm", S::LFG_SECTION_3, /*terminalBossEntry*/ S::NPC_HEROD, S::SECTION_3_ENCOUNTER_MASK,
           S::LFG_SECTION_3_HEROIC},
        {"Scarlet Monastery (Cathedral)", {
            S::NPC_THORNE,            // Brother Aldous Thorne, the Gardener
            OBJ(S::OBJ_LAST_STAND),   // the Last Stand (event 2, the Chapel Door)
            S::NPC_WHITEMANE,         // High Inquisitor Whitemane (with Mograine risen)
        }, "sm-cath", S::LFG_SECTION_4, /*terminalBossEntry*/ S::NPC_WHITEMANE, S::SECTION_4_ENCOUNTER_MASK,
           S::LFG_SECTION_4_HEROIC},
    }, WingSelect::Explicit, /*defaultWing*/ "sm-gy"};
}
