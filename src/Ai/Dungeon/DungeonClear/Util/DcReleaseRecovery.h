/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCRELEASERECOVERY_H
#define _PLAYERBOT_DCRELEASERECOVERY_H

#include <string>
#include <vector>

#include "Define.h"
#include "Position.h"

#include "Ai/Dungeon/DungeonClear/Util/DcRezDecision.h"

class Player;
struct DcRunState;

// Engine glue for "release and walk back" (kernel and bounds: DcReleaseDecision.h).
//
// All state is on the run owner's DcRunState::release, advanced by Track from
// DcRezRecovery::Evaluate (so from any living member; a body moves only the release clocks), on the map thread
// every member of the group ticks on. Three things act on it:
//   * the release: DungeonClearStayDeadAction ("auto release") on the dead bot's own
//     session, run by the dead engine's poll or by a living bot's release rung;
//   * the walk: DungeonClearWalkBackAction on the released bot;
//   * the hold: DcRezRecovery::IsPending / Evaluate, which the whole drive already obeys.
namespace DcReleaseRecovery
{
    // World thread, first world tick: the respawn spots of the checkpoint each map's
    // instances start with. The table is another module's, so its absence (or that module
    // being off or not compiled in) is normal and leaves the feature inert.
    void LoadCheckpointSpots();
    // Re-read that module's own switch (config reload).
    void RefreshCheckpointSwitch();

    // Does the feature apply to the owner's run now? Records the answer on the run and
    // drops its walkers when it does not. Call before Track.
    bool Refresh(Player* owner, DcRunState& run);

    // Advance the clocks and file arrivals, deaths and timeouts. `players` matches
    // `members` row by row (DcRezRecovery's snapshot).
    void Track(Player* owner, DcRunState& run, std::vector<DcRezDecision::Member> const& members,
               std::vector<Player*> const& players, bool partyEngaged, bool rezBlocked, uint32 nowMs);

    bool HasWalkers(DcRunState const& run);

    // Will any body / this body release by itself, so the run may hold for it?
    bool AnyWillRelease(DcRunState const& run, std::vector<Player*> const& players);
    bool WillRelease(DcRunState const& run, Player* body);

    // DcReleaseDecision::FoldWalk over the run; `who` receives the bot's name when the
    // verdict became a walk verdict.
    void ApplyWalkVerdict(DcRunState const& run, DcRezDecision::Result& verdict, std::string& who);

    // Is it this dead bot's turn? Advances the release clocks (never the rez clocks), so
    // they run on from the dead bots themselves too.
    bool ShouldRelease(Player* bot);
    // Bracket the release opcode. Finish files the outcome and returns false when it
    // did not leave the bot alive on the same map (which ends the run).
    void Begin(Player* bot);
    bool Finish(Player* bot);
    // True inside that bracket, for the guard that keeps a ghost in the instance.
    bool IsReleasing(Player const* player);

    // The first body of `bot`'s run that looks due, or nullptr. Read-only, off the last
    // Track; the body's own ShouldRelease decides on a fresh one.
    Player* DueBody(Player* bot);

    // Is this bot walking back, and to where?
    bool WalkTarget(Player* bot, Position& dest);
    bool IsWalkingBack(Player* bot);
    // Is the living leader of `bot`'s run walking back? Its followers wait for it.
    bool IsLeaderWalkingBack(Player* bot);
    // ...and out of the party's spread from `bot`, so its fights are not the party's.
    bool IsLeaderWalkingFar(Player* bot);
}

#endif  // _PLAYERBOT_DCRELEASERECOVERY_H
