/*
 * mod-dungeon-clear — StayDeadAction.cpp
 */

#include "StayDeadAction.h"

#include "Map.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcReleaseRecovery.h"

bool DungeonClearStayDeadAction::HoldsHere() const
{
    // Dungeon/raid maps only. The stay-dead override exists for the wipe-recovery
    // case — a party corpse-run or rez inside an instance — so a bot dead out in
    // the open world keeps stock auto-release (a world bot left as a corpse just
    // lies there until someone happens across it). NOTE: this can't be gated on
    // an active DC run instead — a party death auto-disables the run
    // (DungeonClearPartyDiedTrigger), so by the time bots are corpses `enabled`
    // is already false and a run gate would never hold.
    //
    // Read live (not cached) so `.reload config` and per-run overrides both take
    // effect without a restart. The dead-state "auto release" trigger fires on
    // the throttled "often" cadence, so the per-call lookup is negligible.
    Map const* map = bot ? bot->GetMap() : nullptr;
    return map && map->IsDungeon() && DcSettings::GetBool(bot, "PreventBotRelease");
}

bool DungeonClearStayDeadAction::isUseful()
{
    // Stays a corpse until rezzed, or until its turn to release and walk back.
    if (HoldsHere())
        return _due = DcReleaseRecovery::ShouldRelease(bot);

    return AutoReleaseSpiritAction::isUseful();
}

bool DungeonClearStayDeadAction::Execute(Event event)
{
    bool const due = _due;
    _due = false;
    if (!HoldsHere())
        return AutoReleaseSpiritAction::Execute(event);
    if (!due)
        return false;  // never an unguarded stock release in here

    DcReleaseRecovery::Begin(bot);
    AutoReleaseSpiritAction::Execute(event);
    return DcReleaseRecovery::Finish(bot);
}
