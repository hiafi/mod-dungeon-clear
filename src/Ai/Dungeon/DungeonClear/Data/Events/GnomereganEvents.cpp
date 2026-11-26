/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"

#include <unordered_map>

// --- Gnomeregan (map 90), the rework ---------------------------------------
//
// Six encounters (mod-dungeon-bosses src/Gnomeregan; GnomereganRework.h holds the
// instance contract this file reads, mirrored in DcGnomeregan). Two Finder
// sections and a full clear, each its own run wing (gnomer-s1, gnomer-s2,
// gnomer):
//
//   Section 1 (LFG 14, heroic 321), from the entrance:
//     1. Techbot              his dish is a side room 174yd off the path (353yd
//                             from the landing): a plain pull, nothing to clear.
//     2. Viscous Fallout      he stands at area C, his patrol dropped; the cave
//                             floor round him is cleared first (RoomAggroRegistry).
//     3. Electrocutioner 6000 a plain pull on the platform; his death unlocks the
//                             Workshop Door. The section ends here.
//   Section 2 (LFG 320, heroic 322), from the Train Depot:
//     4. The Mobile Alert System  in the hall, reached from the Depot side; his
//                             death opens the Workshop Door.
//     5. Crowd Pummeler 9-60  in the glass window of his pocket, pulled from the
//                             pocket floor only (FightInPlaceRegistry).
//     6. Mekgineer Thermaplugg the console pack first (RoomAggroRegistry), then
//                             him. The section ends here.
//   The full clear (the default wing) runs both, with the Alert System detour:
//     after Electrocutioner, walk to the Workshop Door's inside (from the
//     junction (-761, 406)), click it once it reads unlocked (event 2), fight the
//     Alert System in the hall, and leave by the y 290 corridor to Pummeler. The
//     detour adds about 249yd.
//
// The doors:
//   * the Workshop Door (90858) is the instance's: sealed (not selectable) until
//     Electrocutioner dies, unlocked after, open (latched) once the Alert System
//     dies or a player opens it. The navmesh routes through it shut, so it is
//     navigation-ignored (never a reason to park) and script-only (the generic
//     rung never clicks it); event 2 is the one click, from the inside, only in
//     the full clear and only once it reads unlocked. Section 2 never clicks it:
//     the Alert System's death opens it before Pummeler is next.
//   * the Final Chamber (142207), Thermaplugg's south door: the instance holds it
//     open from the load on, so it is navigation-ignored.
//
// What the clear must NOT do here: click the Clearance Console (the hard mode, a
// human's choice), a Punchograph, a Sparklematic, a Gnome Face or a Button, talk
// to Kernobee, or pull Crowd Pummeler from outside his pocket (he evades) or from
// the lower room under the window (refused). No event step names any of those
// objects, none of them is a door or a chest, and no boss here calls or answers
// assistance, so the room rows are for the trash itself.

void RegisterGnomereganEvents(std::vector<DungeonEvent>& out)
{
    namespace G = DcGnomeregan;

    // --- 1. The room pre-clears (Fallout's cave, Thermaplugg's console pack) -
    // The Scarlet Monastery shape: one Conditional gate, never latched, that
    // holds each room-aggro boss's pull until its room is empty.
    out.push_back(EventBuilder(G::MAP_ID, G::EVENT_ROOM_AGGRO, "Clear the room (room-aggro pre-clear)")
                      .Conditional(&DcRoomAggroPreClearCondition)
                      .KillCreature(/*room trash*/ 0)
                      .Build());

    // --- 2. The Workshop Door from the inside (the full clear's detour) ------
    // Hold on the door's inside until it reads unlocked (Electrocutioner is dead
    // by the order, so at once; the hold covers a reload that has not raised the
    // state yet), click it (skipped when it already reads open: the instance's
    // watch or a human got there first), then wait for the engine's open state.
    // The click latches it open (saved), so a wipe in the hall walks back through
    // it. The objective finishes on DATA_WORKSHOP_DOOR reading open, so a reload
    // or a re-entered instance never runs this again. PERSISTENT: a fight at the
    // door (a stopped patrol, a straggler from the junction) must not rewind the
    // click it already made.
    out.push_back(EventBuilder(G::MAP_ID, G::EVENT_WORKSHOP_DOOR, "The Workshop Door (from the inside)")
                      .Anchored(G::ORDER_ALERT)
                      .Persistent()
                      .MoveToHoldUntilInstanceData(G::DOOR_INSIDE_X, G::DOOR_INSIDE_Y, G::DOOR_INSIDE_Z,
                                                   G::DOOR_INSIDE_RADIUS, G::DATA_WORKSHOP_DOOR,
                                                   G::WORKSHOP_DOOR_UNLOCKED)
                          .Timeout(120000)
                      .UseGO(G::GO_WORKSHOP_DOOR, G::DOOR_SEARCH)
                          .SkipIfInstanceData(G::DATA_WORKSHOP_DOOR, G::WORKSHOP_DOOR_OPEN)
                      .WaitForGOState(G::GO_WORKSHOP_DOOR, G::GO_STATE_ACTIVE, /*timeoutMs*/ 30000, G::DOOR_SEARCH)
                      .Build());
}

// --- roster patch ----------------------------------------------------------
void RegisterGnomereganRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;
    namespace G = DcGnomeregan;

    // ORDER. The DBC bits (the Alert System 0, Fallout 1, Techbot 2,
    // Electrocutioner 3, Pummeler 4, Thermaplugg 5) would send the run to the hall
    // first, through the sealed door. Five auto-derived bosses keep their bits
    // and take the ruled order's keys.
    //
    // PUMMELER is removed and re-added with his own kill-bit (completionFrom = his
    // entry) at a pocket floor anchor: his spawn is inside the glass window, off
    // the mesh, 4.8yd under the pocket floor and 44yd over the lower room, and a
    // route to it could snap to either floor. The anchor is in the pocket box,
    // where his pull is accepted.
    //
    // THE DOOR OBJECTIVE (the full clear only; see the wings) finishes on the
    // door reading open, read live: a re-entered instance with the door open
    // goes straight to the Alert System.
    DungeonBossInfo pummeler = MakeBoss(G::NPC_PUMMELER, G::MAP_ID, "Crowd Pummeler 9-60", G::PUMMELER_FIGHT_X,
                                        G::PUMMELER_FIGHT_Y, G::PUMMELER_FIGHT_Z,
                                        /*completionFrom*/ G::NPC_PUMMELER, G::ORDER_PUMMELER);

    DungeonBossInfo door = MakeObjective(OBJ(G::OBJ_WORKSHOP_DOOR), /*encounterIndex*/ G::SLOT_ALERT_SYSTEM,
                                         G::MAP_ID, "The Workshop Door (from the inside)", G::DOOR_INSIDE_X,
                                         G::DOOR_INSIDE_Y, G::DOOR_INSIDE_Z, G::DOOR_INSIDE_RADIUS,
                                         /*gateEntry*/ 0, /*hook*/ 0, G::EVENT_WORKSHOP_DOOR, G::ORDER_ALERT);
    door.doneInstanceDataId = static_cast<int32>(G::DATA_WORKSHOP_DOOR);
    door.doneInstanceDataValue = G::WORKSHOP_DOOR_OPEN;

    BossRosterPatch p;
    p.mapId = G::MAP_ID;
    p.reorder = {
        { G::NPC_TECHBOT, G::ORDER_TECHBOT },
        { G::NPC_FALLOUT, G::ORDER_FALLOUT },
        { G::NPC_ELECTRO, G::ORDER_ELECTRO },
        { G::NPC_ALERT_SYSTEM, G::ORDER_ALERT },
        { G::NPC_THERMAPLUGG, G::ORDER_THERMAPLUGG },
    };
    p.remove = { G::NPC_PUMMELER };
    p.add = { pummeler, door };
    t.push_back(std::move(p));
}

// --- wing layout -------------------------------------------------------------
void RegisterGnomereganWings(std::unordered_map<uint32, DungeonWingLayout>& store)
{
    // One connected interior cut in two by the Workshop Door (NAVMESH: with the
    // door shut the Depot side reaches only the hall), queued as two Finder
    // sections. The wing is chosen per RUN (WingSelect::Explicit, see DcRunWing):
    // the queue-fill latches the LFG dungeon's section (14 / 321 -> gnomer-s1,
    // 320 / 322 -> gnomer-s2; the instance's DATA_SECTION follows the same first
    // LFG group), a test row or `dc on <wing>` picks one by hand, and a party
    // standing on the Depot side (its DcNavPenaltyRegistry region) falls back to
    // section 2. Anything else runs the whole dungeon, the default wing, which
    // sits last so WingOf / WingName still label each boss with its section.
    //
    // Each section ends on its last boss (Electrocutioner, Thermaplugg). The full
    // clear has no terminal boss: it is complete when its list is.
    namespace G = DcGnomeregan;
    using DcRoster::OBJ;
    store[G::MAP_ID] = {true, {
        {"Gnomeregan (section 1)", {
            G::NPC_TECHBOT,
            G::NPC_FALLOUT,
            G::NPC_ELECTRO,
        }, "gnomer-s1", G::LFG_SECTION_1, /*terminalBossEntry*/ G::NPC_ELECTRO, G::SECTION_1_ENCOUNTER_MASK,
           G::LFG_SECTION_1_HEROIC},
        {"Gnomeregan (The Workshop)", {
            G::NPC_ALERT_SYSTEM,
            G::NPC_PUMMELER,
            G::NPC_THERMAPLUGG,
        }, "gnomer-s2", G::LFG_SECTION_2, /*terminalBossEntry*/ G::NPC_THERMAPLUGG, G::SECTION_2_ENCOUNTER_MASK,
           G::LFG_SECTION_2_HEROIC},
        {"Gnomeregan", {
            G::NPC_TECHBOT,
            G::NPC_FALLOUT,
            G::NPC_ELECTRO,
            OBJ(G::OBJ_WORKSHOP_DOOR),  // the detour: the door from the inside (event 2)
            G::NPC_ALERT_SYSTEM,
            G::NPC_PUMMELER,
            G::NPC_THERMAPLUGG,
        }, "gnomer"},
    }, WingSelect::Explicit, /*defaultWing*/ "gnomer"};
}
