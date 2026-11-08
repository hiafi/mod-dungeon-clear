/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Scholomance (map 289) — what the clear does, and what it leaves alone -----
//
// There are NO event rows for this map any more. The Viewing Room used to have
// a room-aggro pre-clear that killed the ~32 Scholomance Students (10475)
// before the Marduk & Vectus pull. In the reworked dungeon the students are
// unattackable props (unit_flags NON_ATTACKABLE | NOT_SELECTABLE; only the quiz
// student, On the Spot, is selectable, and only mid-fight, which the boss
// descriptor drives), so there is nothing to clear and nothing for the clear to
// wait on: the RoomAggroRegistry rows for 10475 went with the event.
//
// What the clear must NOT do here, by design (all human-started or scripted):
//   * click the Torn Keepsake pages / book (301150-301152), the alchemy table
//     (301154) or the honors program paper (301153): they are goobers, and the
//     clear only ever Use()s doors on its route, plus whatever a DungeonEvent
//     step names (none for this map);
//   * open the Embalming Fluid (175965): a chest, skipped by DungeonClear.
//     IgnoreChests and absent from DcEncounterChests;
//   * click the Brazier of the Herald (175564): see Kirtonos below;
//   * click the six classroom gates (177371-177373, 177375-177377) or the
//     Gandling entrance gate (177374): the instance script owns them
//     (DcEventDoorRegistry::IsSelfClearing, SealedEncounterRegistry).

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterScholomanceRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Scholomance (map 289) — Marduk & Vectus, one merged boss ---
    // Marduk Blackpool (10433) and Vectus (10432) are two separate
    // DungeonEncounters (kill-bits 3 and 4) that share one room and are
    // wired together: pulling EITHER pulls BOTH, and they die together (shared
    // health in the rework). Collapse the pair into ONE boss, mirroring SM
    // Cathedral (Mograine/Whitemane): drop BOTH derived entries and re-add a
    // single "Marduk & Vectus" anchored on VECTUS's spawn. The merged boss
    // re-uses Vectus's real entry (10432) so the engage pipeline drives a real
    // creature, and inherits Vectus's own kill-bit via inheritCompletionFrom
    // (resolved from the base list before the remove). Both die by a player
    // kill, so both bits flip; the roster only needs the one.
    {
        BossRosterPatch p;
        p.mapId = 289;
        p.remove = { 10432, 10433 };
        p.add = {
            MakeBoss(10432, 289, "Marduk & Vectus",
                     143.5f, 99.1f, 104.7f, /*completionFrom*/ 10432),
        };
        t.push_back(std::move(p));
    }

    // --- NOT in the roster, on purpose ---------------------------------------
    //
    // Kirtonos the Herald (bit 0, creature 10506) has no `creature` row: the
    // instance script summons him when a player clicks the Brazier of the Herald
    // (GO 175564, lock 1179 — Blood of Innocents), so BossSpawnIndex never lists
    // him and the derived roster is the twelve spawned encounters. Unlike the
    // Deadmines' Sneed (a boss on the only road forward, anchored on his Shredder
    // so the party is walked to him), Kirtonos is an optional, item-gated secret
    // boss that stays stock. A bot party can neither hold the item nor click the
    // brazier, and routing it at an empty anchor would only produce a "not
    // spawned" stall. So he stays out of the run by not being in it (the
    // Blackrock Spire Solakar precedent): the clear ends when Gandling dies, with
    // Kirtonos's bit unset.
    //
    // Gandling (bit 12, 1853) needs no patch either. The encounter order puts him
    // last, and the reworked instance reveals him (visible, hostile) when the
    // sixth instructor's boss state reaches DONE, which is the death of the last
    // instructor the clear kills (Illucia, bit 11). The roster therefore never
    // targets a hidden Gandling in an unskipped run.
}
