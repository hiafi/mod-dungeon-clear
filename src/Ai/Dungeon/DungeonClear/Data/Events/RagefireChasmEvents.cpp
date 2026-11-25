/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Ragefire Chasm (map 389), the rework ----------------------------------
//
// Four encounters and no door, lever or scripted step: Oggleflint, Jergosh the
// Invoker and Bazzalan in any order, then Taragaman, who stands on his hub
// DORMANT (immune to all, passive, NOT_SELECTABLE) until the instance reads the
// other three DONE. So the clear needs the kill order (the roster patch), the
// rooms cleared before each pull (event 1 plus the RoomAggroRegistry rows) and
// nothing else. The branch down to Oggleflint is the funnel wall's business
// (DcNavPenaltyRegistry, map 389).
//
// What the clear must NOT do here:
//   * click the Blood Shrine (301340) or the pour spot (301341): Starving, the
//     heroic hard mode, is a human's choice. They are goobers (type 10, no lock,
//     no loot), and the clear only ever Use()s doors on its route, loots chests
//     and works whatever object an event step names (no step here names one);
//     their scripts refuse a headless player too. The stock Blood Filled Orb
//     (182024) is the same;
//   * take Taragaman's Searing Spikes (301342, generic, collision on, up for one
//     Nova) for a shut door: a blocked ray counts only with a closed DOOR on the
//     line (DcEngageGeometry::ClosedDoorBetween), and this map has none;
//   * look for Zelemar (no encounter) or wait at a checkpoint: the reached-only
//     row at (-154.8, 70.2, -21.1) only moves the revive point past the descent,
//     and every remaining boss routes from it (t/TestRfcRouteProbe.cpp).
//
// The room-aggro event is the Scarlet Monastery's shape: one Conditional gate,
// never latched, that holds each room-aggro boss's pull until its room is empty.
void RegisterRagefireChasmEvents(std::vector<DungeonEvent>& out)
{
    namespace R = DcRagefireChasm;
    out.push_back(EventBuilder(R::MAP_ID, R::EVENT_ROOM_AGGRO, "Clear the room (room-aggro pre-clear)")
                      .Conditional(&DcRoomAggroPreClearCondition)
                      .KillCreature(/*room trash*/ 0)
                      .Build());
}

// --- roster patch ----------------------------------------------------------
void RegisterRagefireChasmRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;
    namespace R = DcRagefireChasm;

    // ORDER. All four bosses have kill-creature credits (instance_encounters 430
    // to 433), so BossSpawnIndex derives them at their spawns. Their DBC bits put
    // Taragaman (1) between Oggleflint (0) and Jergosh (2): the run would walk
    // from the den to a boss nobody can select and stand there. Reordered to the
    // rework's kill order, which is also the DBC's own OrderIndex; the bits stay.
    // Heroic keeps the normal entries and bits, so the gate is Any.
    //
    // THE HALL RAMP. A travel objective (no event, no hook: it latches on arrival)
    // between Jergosh and Bazzalan. The router climbs out of the hall by the side
    // of the north-east ramp, (-335, 238) to (-334, 246), 10yd up in 8 (52
    // degrees): bots walk it, a human following them cannot. Through the point on
    // the ramp's top the party takes the ramp itself (42 degrees at the steepest)
    // and the leg is no longer. A penalty box cannot do this: the face and the
    // ramp are one navmesh polygon, so a row either leaves the climb or prices
    // out both (403yd, back out by the strip). The way back down is left alone:
    // a drop of 5yd stops nobody. Done by his own slot too.
    //
    // THE SLOTS. Both objectives carry the slot of the boss behind them, so while
    // that slot reads IN_PROGRESS the stuck-combat breaker stands down
    // (DcBossStandDown::ObjectiveEncounterLive). It has to: Bazzalan's heroic chain
    // holds no victim for 9.5s (passive, still engaged and attackable) and
    // Taragaman has none while he casts, spins or leaps, and the breaker's boss
    // test (HoldsFiveManBossFight) reads a boss with no victim as no fight.
    // Oggleflint's heroic 4.5s is a third of its 15s clock.
    DungeonBossInfo ramp = MakeObjective(OBJ(R::OBJ_HALL_RAMP), /*encounterIndex*/ R::BIT_BAZZALAN, R::MAP_ID,
                                         "The hall ramp", R::HALL_RAMP_X, R::HALL_RAMP_Y, R::HALL_RAMP_Z,
                                         R::HALL_RAMP_ARRIVE_RADIUS, /*gateEntry*/ 0, /*hook*/ 0,
                                         /*eventId*/ 0, R::ORDER_BAZZALAN);
    ramp.doneBossStateIndex = R::SLOT_BAZZALAN;

    // THE WEST BANK. A travel objective of the same kind (it latches on
    // arrival) between Bazzalan and Taragaman. Without it the router comes down
    // Bazzalan's north shelf and in by the +y arm (338yd), past five units no
    // earlier leg met, with the -x arm foot's Enforcers on the far side of him.
    // Through the objective the party retraces the hall strip and the west bank,
    // cleared on the way to Jergosh, and comes in by the -x arm (357yd), the side
    // the plateau pre-clear is cut for. The point is on the bank, not at the
    // arm's foot, because of the checkpoint: a party that wipes on this leg comes
    // back from (-154.8, 70.2), and from there the arm's foot is nearest ACROSS
    // the hub (10yd from him), while this point is nearest round the bank (57yd
    // from him). Once latched, a wipe to him walks straight back by the +x arm.
    // Done by his own slot too, so a run begun on a finished instance has nothing
    // left to walk to.
    DungeonBossInfo bank = MakeObjective(OBJ(R::OBJ_WEST_BANK), /*encounterIndex*/ R::BIT_TARAGAMAN, R::MAP_ID,
                                         "The west bank", R::WEST_BANK_X, R::WEST_BANK_Y, R::WEST_BANK_Z,
                                         R::WEST_BANK_ARRIVE_RADIUS, /*gateEntry*/ 0, /*hook*/ 0,
                                         /*eventId*/ 0, R::ORDER_TARAGAMAN);
    bank.doneBossStateIndex = R::SLOT_TARAGAMAN;

    BossRosterPatch p;
    p.mapId = R::MAP_ID;
    p.reorder = {
        { R::NPC_OGGLEFLINT, R::ORDER_OGGLEFLINT },
        { R::NPC_JERGOSH, R::ORDER_JERGOSH },
        { R::NPC_BAZZALAN, R::ORDER_BAZZALAN },
        { R::NPC_TARAGAMAN, R::ORDER_TARAGAMAN },
    };
    p.add = { ramp, bank };
    t.push_back(std::move(p));
}
