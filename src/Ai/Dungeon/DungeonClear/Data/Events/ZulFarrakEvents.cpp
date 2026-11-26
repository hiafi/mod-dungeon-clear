/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Creature.h"
#include "CreatureAI.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

// --- Zul'Farrak (map 209), the rework --------------------------------------
//
// Seven encounters (mod-dungeon-bosses src/ZulFarrak; ZulFarrakRework.h holds the
// instance contract this file reads). The clear, in the ruled order:
//
//   1. Theka the Martyr      a plain pull in his courtyard, his three escorts
//                            first (RoomAggroRegistry).
//   2. Antu'sul              he waits at the dead end of his den and is woken by
//                            area trigger 1447, which bots never send: event 5
//                            clears the chamber, stands the tank inside the
//                            trigger's sphere and does what the trigger does
//                            (SetData(1, 1)). He runs out; the fight is in the
//                            chamber.
//   3. Witch Doctor Zum'rah  the stock wake (event 3, hook 5: faction 35 -> 37),
//                            now after his bowl's two patrols.
//   4. The plaza             objective + event 6: the floor round the stair foot
//                            is cleared before the cages open (section 7 #20 A).
//   5. The Pyramid           objective + event 1 on the top: pull the
//                            Executioner (stage 1, the Block), open a cage once
//                            he is dead (stage 2 starts) and hold the LANDING
//                            until the instance's stage data reads the Altar (3).
//   6. The Altar             objective + event 7 on the plaza: Nekrum and
//                            Sezz'ziz, until the stage reads won (4).
//   7. Bly's crew            objective + event 8: Bly's gossip option 0 (the
//                            betrayal; the Test is a human's choice), the four
//                            fight, then the End Door (Weegli's barrel).
//   8. Chief Ukorz           a plain pull of the pair; the mesa's guards first.
//   9. Hydromancer Velratha  a plain pull (no gong: she strikes it at 30%).
//  10. The pool              objective + event 2: Gahz'rilla once he is ashore.
//                            Optional: the bar is the clear through Ukorz.
//
// What the clear must NOT do here:
//   * click a Troll Cage before the Executioner is dead (rule 30): the UseGO is
//     the step after his kill, and the cages' lock (420, a key item) keeps the
//     generic door rung off them;
//   * click the End Door (146084): lock 0, so DcDoorPolicy refuses it, and it is
//     navigation-ignored, so its shut state never pauses the run. The crew
//     objective completes only on DATA_END_DOOR reading 3 (blown), and that is
//     what puts Ukorz next;
//   * pick Bly's Test (option 1, heroic only, behind a confirmation), use the gong
//     (141832, inert and NOT_SELECTABLE in the rework) or attack the crew outside
//     their fight (faction 35, not hostile).
//
// No boss or wave unit here calls or answers assistance, so the room rows are
// there for the trash itself, not for a scripted room pull.

namespace
{
    namespace Z = DcZulFarrak;

    // --- Witch Doctor Zum'rah, the area-trigger wake-up (event 3) ----------
    // Zum'rah (7271) spawns FRIENDLY: creature_template faction 35, restored on
    // every Reset. He turns hostile only when a party member crosses area trigger
    // 962 (10yd round the cauldron), whose SmartAI row sets faction 37. That runs
    // off CMSG_AREATRIGGER, which bots never send for a non-teleport trigger, so in
    // an all-bot party he never flips and the engage gate (attackability-blind:
    // GetLiveBoss only asks IsAlive) deadlocks on him. Hook 5 (WakeZumrah) sets the
    // faction the row would have set; the rework keeps 35 / 37 (STAGE-D3).
    //
    // The wake now also waits for his room: the bowl's two Witch Doctor patrols
    // (RoomAggroRegistry) cross the tongue's approach, and a woken Zum'rah fights
    // whatever is near him. Both conditionals read the same room-trash value, so
    // the wake never runs while the room-aggro event still has work.
    //
    // REPEATABLE: a wipe restores faction 35, and the condition ends the loop by
    // itself the moment he is hostile (or dead).
    constexpr float ZF_ZUMRAH_SCAN = 60.0f;
    constexpr uint32 ZF_ZUMRAH_FACTION_FRIENDLY = 35;  // creature_template default

    bool ZfZumrahAsleep(Player* bot, AiObjectContext* context)
    {
        Creature* zumrah = bot->FindNearestCreature(Z::NPC_ZUMRAH, ZF_ZUMRAH_SCAN);
        if (!zumrah || !zumrah->IsAlive())
            return false;

        // Still on his spawn faction => nobody has tripped area trigger 962.
        if (zumrah->GetFaction() != ZF_ZUMRAH_FACTION_FRIENDLY)
            return false;

        // His bowl first (the room-aggro event owns that).
        return !DcRoomAggroPreClearCondition(bot, context);
    }

    // --- Antu'sul, the area-trigger wake-up (event 5) -----------------------
    // In the rework he stands at the dead end of his den, and the stock trigger
    // (1447, a 30yd sphere over the den mouth and the chamber's north half) sends
    // him out at the party: SmartAI action 45, SetData(1, 1) on him. A bot never
    // sends the trigger, so this event does its work: clear the chamber (D2:
    // Sandcrawler 81512 and Witch Doctor 81482, plus the Blood Drinker patrol
    // while it crosses), stand inside the sphere, wake him. He engages the nearest
    // living player within 100yd and runs at them, and the fight is in the chamber
    // (DenArena leashes him there). The tank never walks the 7-13yd throat to the
    // dead end, which is why the dynamic-pull stand-down round his anchor is cut
    // to the throat (SummonHoldRadius).
    //
    // DUE while he is alive, out of combat and not evading, with the leader within
    // 60yd of the chamber; Theka's courtyard is 133yd away, so it cannot fire
    // before his turn. REPEATABLE: after a wipe he walks home, and the condition
    // reads true again once the party is back.
    bool ZfAntusulAsleep(Player* bot, AiObjectContext* /*context*/)
    {
        if (bot->GetExactDist(Z::CHAMBER_X, Z::CHAMBER_Y, Z::CHAMBER_Z) > Z::WAKE_APPROACH_RADIUS)
            return false;
        Creature* antusul = bot->FindNearestCreature(Z::NPC_ANTUSUL, Z::ANTUSUL_SCAN);
        if (!antusul || !antusul->IsAlive())
            return false;
        return !antusul->IsInCombat() && !antusul->IsInEvadeMode();
    }

    bool ZfMemberInWakeTrigger(Player* bot)
    {
        auto const inTrigger = [](Player* p)
        {
            return p && p->IsAlive() && p->GetMapId() == Z::MAP_ID &&
                   p->GetExactDist(Z::WAKE_TRIGGER_X, Z::WAKE_TRIGGER_Y, Z::WAKE_TRIGGER_Z) <=
                       Z::WAKE_TRIGGER_RADIUS;
        };
        if (inTrigger(bot))
            return true;
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member->GetInstanceId() == bot->GetInstanceId() && inTrigger(member))
                    return true;
        return false;
    }

    ObjectiveArriveResult WakeAntusul(Player* bot, AiObjectContext* /*context*/, DungeonBossInfo const& /*info*/)
    {
        Creature* antusul = bot->FindNearestCreature(Z::NPC_ANTUSUL, Z::ANTUSUL_SCAN);
        if (!antusul || !antusul->IsAlive() || antusul->IsInCombat())
            return ObjectiveArriveResult::Done;  // dead, or already coming
        if (!Z::WakeAntusulDue(true, false, antusul->IsInEvadeMode(), ZfMemberInWakeTrigger(bot)))
            return ObjectiveArriveResult::Running;  // walking home after a wipe, or nobody in the sphere yet
        if (CreatureAI* ai = antusul->AI())
        {
            ai->SetData(Z::ANTUSUL_WAKE_DATA_ID, Z::ANTUSUL_WAKE_VALUE);
            LOG_INFO("playerbots.dungeonclear",
                     "DungeonClear: ZulFarrak: woke Antu'sul ({}) with SetData({}, {}) for {} "
                     "(area trigger 1447 never fires for an all-bot party)",
                     Z::NPC_ANTUSUL, Z::ANTUSUL_WAKE_DATA_ID, Z::ANTUSUL_WAKE_VALUE, bot->GetName());
        }
        return ObjectiveArriveResult::Done;
    }

    ObjectiveArriveResult ToArrive(Z::Gate g)
    {
        switch (g)
        {
            case Z::Gate::Done:
                return ObjectiveArriveResult::Done;
            case Z::Gate::Restart:
                return ObjectiveArriveResult::Restart;
            case Z::Gate::Running:
                break;
        }
        return ObjectiveArriveResult::Running;
    }

    uint32 ZfData(Player* bot, uint32 id)
    {
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        return inst ? inst->GetData(id) : 0;
    }

    // Either Altar boss alive near the plaza (and out of combat, when `idleOnly`).
    constexpr float ZF_ALTAR_SCAN = 100.0f;

    bool ZfAltarBossAlive(Player* bot, bool idleOnly)
    {
        for (uint32 entry : { Z::NPC_NEKRUM, Z::NPC_SEZZZIZ })
            if (Creature* c = bot->FindNearestCreature(entry, ZF_ALTAR_SCAN, /*alive*/ true))
                if (!idleOnly || !c->IsInCombat())
                    return true;
        return false;
    }

    // --- The Altar (event 7) ------------------------------------------------
    ObjectiveArriveResult AltarReady(Player* bot, AiObjectContext* /*context*/, DungeonBossInfo const& /*info*/)
    {
        return ToArrive(Z::AltarReady(ZfData(bot, Z::DATA_PYRAMID_STAGE), ZfAltarBossAlive(bot, false)));
    }

    ObjectiveArriveResult AltarRearm(Player* bot, AiObjectContext* /*context*/, DungeonBossInfo const& /*info*/)
    {
        Z::Gate const g = Z::AltarRearm(ZfData(bot, Z::DATA_PYRAMID_STAGE),
                                        ZfData(bot, Z::DATA_PYRAMID_RUNNING) != 0, ZfAltarBossAlive(bot, true));
        if (g == Z::Gate::Restart)
            LOG_INFO("playerbots.dungeonclear",
                     "DungeonClear: ZulFarrak: the Altar is back in play with its bosses idle "
                     "(a stage 3 wipe): {} pulls them again", bot->GetName());
        return ToArrive(g);
    }

    // --- The pool (event 2) -------------------------------------------------
    constexpr float ZF_POOL_SCAN = 100.0f;

    ObjectiveArriveResult PoolReady(Player* bot, AiObjectContext* /*context*/, DungeonBossInfo const& /*info*/)
    {
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        uint32 const slot = inst ? static_cast<uint32>(inst->GetBossState(Z::SLOT_GAHZRILLA)) : 0;
        Creature* gahz = bot->FindNearestCreature(Z::NPC_GAHZRILLA, ZF_POOL_SCAN, /*alive*/ true);
        bool const attackable = gahz && !gahz->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE) && !gahz->IsImmuneToPC();
        bool const velrathaAlive = bot->FindNearestCreature(Z::NPC_VELRATHA, ZF_POOL_SCAN, /*alive*/ true);
        return ToArrive(Z::PoolReady(slot, attackable, velrathaAlive));
    }
}

void RegisterZulFarrakHooks(ObjectiveHookRegistry::HookTable& out)
{
    ObjectiveHookRegistry::AddHook(out, Z::HOOK_ALTAR_READY, &AltarReady);
    ObjectiveHookRegistry::AddHook(out, Z::HOOK_ALTAR_REARM, &AltarRearm);
    ObjectiveHookRegistry::AddHook(out, Z::HOOK_WAKE_ANTUSUL, &WakeAntusul);
    ObjectiveHookRegistry::AddHook(out, Z::HOOK_POOL_READY, &PoolReady);
}

void RegisterZulFarrakEvents(std::vector<DungeonEvent>& out)
{
    // --- 1. The Pyramid: the Block and the Stair ---------------------------
    // The instance's stage machine (STAGE-S0 section 3) runs the three stages; DC
    // drives the party through them and never writes a slot. Stage 1 starts at
    // the Executioner's pull and is won by his death; a cage's GO_ACTIVATED starts
    // stage 2 (a UseGO counts; Use() skips the lock); the waves climb the stair
    // (the barrel lane) to the landing, so the party holds the landing, the crew's
    // line, until the stage reads the Altar. Gated on DATA_PYRAMID_STAGE, not the
    // stock phase 7: it is monotonic, so a fight straight through wave 2 still
    // releases the hold the moment the party drops combat.
    //
    // Wipes restart without a rewind: a stage 1 wipe evades the Executioner and
    // the kill step pulls him again; a stage 2 wipe restarts once a living player
    // has stood 10s on the landing, which is where the hold walks the party back
    // to. PERSISTENT: every wave is a combat gap.
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_PYRAMID, "The Pyramid (the Block and the Stair)")
                      .Anchored(Z::ORDER_PYRAMID)
                      .Persistent()
                      .KillCreatureEngage(Z::NPC_EXECUTIONER, /*count*/ 1, /*searchRadius*/ 60.0f)
                      .UseGO(Z::GO_TROLL_CAGE, /*searchRadius*/ 25.0f)
                          .Timeout(60000)
                      .MoveToHoldUntilInstanceData(Z::LANDING_X, Z::LANDING_Y, Z::LANDING_Z,
                                                   Z::LANDING_HOLD_RADIUS, Z::DATA_PYRAMID_STAGE,
                                                   Z::STAGE_ALTAR)
                          .Timeout(900000)  // two waves, the barrels, a wipe and its restart
                      .Build());

    // --- 2. The pool: Velratha, then Gahz'rilla (optional) -----------------
    // Velratha is a roster boss and is pulled like any other (she no longer
    // patrols). At 30% she runs to the gong and strikes it; the instance summons
    // Gahz'rilla, who cannot be selected until he is ashore at (1677, 1167), 15s
    // at most. This objective, last in the order and anchored on the gong,
    // finishes him: Velratha first if a pool wipe brought her back, then wait
    // until he can be attacked, kill him, and hold until slot 1 reads DONE. A pool
    // wipe (slot 1 back to NOT_STARTED; he despawns, she is respawned) rewinds to
    // her pull. Never UseGO the gong. OPTIONAL: the bar is the clear through Ukorz
    // (section 7 #57 A), so a failure here skips instead of stalling the run.
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_POOL, "The pool (Velratha and Gahz'rilla)")
                      .Anchored(Z::ORDER_POOL)
                      .Persistent()
                      .Optional()
                      .KillCreatureEngage(Z::NPC_VELRATHA, /*count*/ 1, /*searchRadius*/ 150.0f)
                      .Custom(Z::HOOK_POOL_READY)
                          .Timeout(120000)
                      .KillCreatureEngage(Z::NPC_GAHZRILLA, /*count*/ 1, /*searchRadius*/ 120.0f)
                          .EngageOnlyWhenActive()
                      .MoveToHoldUntilBossState(Z::GONG_X, Z::GONG_Y, Z::GONG_Z, Z::POOL_HOLD_RADIUS,
                                                Z::SLOT_GAHZRILLA, DcBossStateBit(Z::STATE_DONE))
                          .RestartOnBossState(DcBossStateBit(Z::STATE_NOT_STARTED) |
                                              DcBossStateBit(Z::STATE_FAIL))
                          .Timeout(300000)
                      .Build());

    // --- 3. Wake Zum'rah (stock, after his room) ---------------------------
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_WAKE_ZUMRAH, "Wake Witch Doctor Zum'rah")
                      .Conditional(&ZfZumrahAsleep)
                      .Repeatable()
                      .PanelBeforeBoss(Z::NPC_ZUMRAH)
                      .Custom(Z::HOOK_WAKE_ZUMRAH)
                      .Build());

    // --- 4. The room pre-clears (Theka's courtyard, Zum'rah's bowl, the mesa) --
    // The Scarlet Monastery shape: one Conditional gate, never latched, that holds
    // each room-aggro boss's pull until its room is empty (RoomAggroRegistry).
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_ROOM_AGGRO, "Clear the room (room-aggro pre-clear)")
                      .Conditional(&DcRoomAggroPreClearCondition)
                      .KillCreature(/*room trash*/ 0)
                      .Build());

    // --- 5. Wake Antu'sul ---------------------------------------------------
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_WAKE_ANTUSUL, "Wake Antu'sul")
                      .Conditional(&ZfAntusulAsleep)
                      .Repeatable()
                      .PanelBeforeBoss(Z::NPC_ANTUSUL)
                      .SummonHoldRadius(Z::ANTUSUL_SUMMON_HOLD_RADIUS)
                      .ClearRadius(Z::CHAMBER_X, Z::CHAMBER_Y, Z::CHAMBER_Z, Z::CHAMBER_CLEAR_RADIUS,
                                   Z::CHAMBER_CLEAR_Z_BAND)
                          .Timeout(300000)
                      .MoveTo(Z::WAKE_STAND_X, Z::WAKE_STAND_Y, Z::WAKE_STAND_Z, Z::WAKE_STAND_RADIUS)
                      .Custom(Z::HOOK_WAKE_ANTUSUL)
                          .Timeout(60000)
                      .Build());

    // --- 6. The plaza pre-clear --------------------------------------------
    // Eleven Sandfury stand where the waves gather, the Altar is fought and the
    // crew fight (section 7 #20 A: stock trash, cleared before the cages open).
    // One gate, so nothing to rewind.
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_PLAZA, "Clear the plaza")
                      .Anchored(Z::ORDER_PLAZA)
                      .ClearRadius(Z::PLAZA_X, Z::PLAZA_Y, Z::PLAZA_Z, Z::PLAZA_CLEAR_RADIUS,
                                   Z::PLAZA_CLEAR_Z_BAND)
                          .Timeout(600000)
                      .Build());

    // --- 7. The Altar: Nekrum and Sezz'ziz ---------------------------------
    // Stage 3 summons both idle at the altar 5s after the Stair is won; their pull
    // starts it (DATA_PYRAMID_START 3), their deaths win the Pyramid (stage 4,
    // slot 5 DONE). The hold rewinds the event when a stage 3 wipe has put them
    // back idle. PERSISTENT: two kills with gaps.
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_ALTAR, "The Altar (Nekrum and Sezz'ziz)")
                      .Anchored(Z::ORDER_ALTAR)
                      .Persistent()
                      .Custom(Z::HOOK_ALTAR_READY)
                          .Timeout(60000)
                      .KillCreatureEngage(Z::NPC_NEKRUM, /*count*/ 1, /*searchRadius*/ 80.0f)
                      .KillCreatureEngage(Z::NPC_SEZZZIZ, /*count*/ 1, /*searchRadius*/ 80.0f)
                      .MoveToHoldUntilInstanceData(Z::PLAZA_X, Z::PLAZA_Y, Z::PLAZA_Z, Z::ALTAR_HOLD_RADIUS,
                                                   Z::DATA_PYRAMID_STAGE, Z::STAGE_WON)
                          .WhileHolding(Z::HOOK_ALTAR_REARM)
                          .Timeout(300000)
                      .Build());

    // --- 8. Bly's crew: the betrayal ---------------------------------------
    // After the Pyramid the five walk to their gather points on the plaza. Bly's
    // gossip option 0 is the betrayal (CR); the menu shows only in Gather mode
    // with the path open, so the step waits for him to stand still and retries
    // until the option is offered. About 4.5 to 7.5s of stock lines later the
    // fight starts (slot 6 IN_PROGRESS) and the four attack the chooser. It is
    // over when slot 6 reads DONE (normal: all four dead; heroic: the fallen
    // bodies die together with the last), whatever the kill order. A crew wipe
    // puts slot 6 back to NOT_STARTED and the five on their spots at full health:
    // rewind to the gossip. Then Weegli's barrel blows the End Door (or the
    // instance's 60s fallback does), and only then is Ukorz next: the objective
    // finishes on DATA_END_DOOR reading 3. PERSISTENT: four kills and a door.
    out.push_back(EventBuilder(Z::MAP_ID, Z::EVENT_CREW, "Bly's crew (the betrayal)")
                      .Anchored(Z::ORDER_CREW)
                      .Persistent()
                      .Gossip(Z::NPC_BLY, /*option*/ 0, /*searchRadius*/ 40.0f)
                          .Timeout(180000)
                          .WaitTargetStill()
                      .MoveToHoldUntilBossState(Z::BLY_GATHER_X, Z::BLY_GATHER_Y, Z::BLY_GATHER_Z,
                                                Z::CREW_HOLD_RADIUS, Z::SLOT_CREW,
                                                DcBossStateBit(Z::STATE_IN_PROGRESS) |
                                                    DcBossStateBit(Z::STATE_DONE))
                          .Timeout(60000)
                      .MoveToHoldUntilBossState(Z::BLY_GATHER_X, Z::BLY_GATHER_Y, Z::BLY_GATHER_Z,
                                                Z::CREW_HOLD_RADIUS, Z::SLOT_CREW, DcBossStateBit(Z::STATE_DONE))
                          .RestartOnBossState(DcBossStateBit(Z::STATE_NOT_STARTED) |
                                              DcBossStateBit(Z::STATE_FAIL))
                          .Timeout(900000)
                      .WaitForGOState(Z::GO_END_DOOR, Z::GO_STATE_ACTIVE_ALTERNATIVE, /*timeoutMs*/ 120000,
                                      Z::END_DOOR_SEARCH)
                      .Build());
}

// --- roster patch ----------------------------------------------------------
void RegisterZulFarrakRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // ORDER. The DBC bits (Velratha 0, Antu'sul 2, Theka 3, Zum'rah 4, Ukorz 7)
    // would send the run to the far pool first. The five auto-derived bosses keep
    // their bits and take the ruled order's keys. The Pyramid (bit 5) and the
    // crew (bit 6) carry cast-spell credits in the rework (213840 / 213841), so
    // BossSpawnIndex never lists them and they are objectives here; Gahz'rilla
    // (bit 1, summoned) is the pool objective's.
    //
    // THE SLOTS. Each objective names what finishes it in the instance, so a
    // reloaded or re-entered instance never replays a won stage, and while the
    // slot reads IN_PROGRESS the stuck-combat breaker stands down
    // (DcBossStandDown::ObjectiveEncounterLive): the waves leave gaps with no
    // victim. The crew's objective finishes on the End Door instead (3 = blown,
    // saved), which also says the crew is done.
    DungeonBossInfo plaza = MakeObjective(OBJ(Z::OBJ_PLAZA), /*encounterIndex*/ Z::SLOT_PYRAMID, Z::MAP_ID,
                                          "The plaza (pre-clear)", Z::PLAZA_X, Z::PLAZA_Y, Z::PLAZA_Z,
                                          Z::PLAZA_ARRIVE_RADIUS, /*gateEntry*/ 0, /*hook*/ 0, Z::EVENT_PLAZA,
                                          Z::ORDER_PLAZA);
    plaza.doneBossStateIndex = static_cast<int32>(Z::SLOT_PYRAMID);

    DungeonBossInfo pyramid = MakeObjective(OBJ(Z::OBJ_PYRAMID), /*encounterIndex*/ Z::SLOT_PYRAMID, Z::MAP_ID,
                                            "The Pyramid (Block and Stair)", Z::TOP_X, Z::TOP_Y, Z::TOP_Z,
                                            Z::TOP_ARRIVE_RADIUS, /*gateEntry*/ 0, /*hook*/ 0, Z::EVENT_PYRAMID,
                                            Z::ORDER_PYRAMID);
    pyramid.doneBossStateIndex = static_cast<int32>(Z::SLOT_PYRAMID);

    DungeonBossInfo altar = MakeObjective(OBJ(Z::OBJ_ALTAR), /*encounterIndex*/ Z::SLOT_PYRAMID, Z::MAP_ID,
                                          "The Altar (Nekrum and Sezz'ziz)", Z::PLAZA_X, Z::PLAZA_Y, Z::PLAZA_Z,
                                          Z::PLAZA_ARRIVE_RADIUS, /*gateEntry*/ 0, /*hook*/ 0, Z::EVENT_ALTAR,
                                          Z::ORDER_ALTAR);
    altar.doneBossStateIndex = static_cast<int32>(Z::SLOT_PYRAMID);

    DungeonBossInfo crew = MakeObjective(OBJ(Z::OBJ_CREW), /*encounterIndex*/ Z::SLOT_CREW, Z::MAP_ID,
                                         "Bly's crew (betrayal, End Door)", Z::BLY_GATHER_X, Z::BLY_GATHER_Y,
                                         Z::BLY_GATHER_Z, Z::CREW_ARRIVE_RADIUS, /*gateEntry*/ 0, /*hook*/ 0,
                                         Z::EVENT_CREW, Z::ORDER_CREW);
    crew.doneInstanceDataId = static_cast<int32>(Z::DATA_END_DOOR);
    crew.doneInstanceDataValue = Z::END_DOOR_BLOWN;

    // The pool, last: on the gong, where Velratha fights on after 30%.
    DungeonBossInfo pool = MakeObjective(OBJ(Z::OBJ_POOL), /*encounterIndex*/ Z::SLOT_GAHZRILLA, Z::MAP_ID,
                                         "The pool (Gahz'rilla)", Z::GONG_X, Z::GONG_Y, Z::GONG_Z,
                                         Z::POOL_ARRIVE_RADIUS, /*gateEntry*/ 0, /*hook*/ 0, Z::EVENT_POOL,
                                         Z::ORDER_POOL);
    pool.doneBossStateIndex = static_cast<int32>(Z::SLOT_GAHZRILLA);

    BossRosterPatch p;
    p.mapId = Z::MAP_ID;
    p.reorder = {
        { Z::NPC_THEKA, Z::ORDER_THEKA },
        { Z::NPC_ANTUSUL, Z::ORDER_ANTUSUL },
        { Z::NPC_ZUMRAH, Z::ORDER_ZUMRAH },
        { Z::NPC_UKORZ, Z::ORDER_UKORZ },
        { Z::NPC_VELRATHA, Z::ORDER_VELRATHA },
    };
    p.add = { plaza, pyramid, altar, crew, pool };
    t.push_back(std::move(p));
}
