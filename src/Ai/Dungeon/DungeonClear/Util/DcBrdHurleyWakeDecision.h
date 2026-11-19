/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCBRDHURLEYWAKEDECISION_H
#define _PLAYERBOT_DCBRDHURLEYWAKEDECISION_H

#include <cmath>

#include "Define.h"

// PURE kernel for the gather in front of Hurley's kegs (Blackrock Depths, map
// 230, WakeHurley / hook 52): may the leader use the FIRST Thunderbrew Lager
// Keg yet?
//
// Hurley's wake walk ends in an attack on the NEAREST player
// (boss_hurley_blackbreath_rework.cpp Arrive -> NearestTarget). Only the tank is
// sent to the keg point; the followers trail it 15-20yd back, which is on the
// wake path. Smoke run tr-20261119-033912-2 (brd-s4): the healer stood 8yd from
// Hurley's walk end, the tank 25yd away at the kegs; Hurley and his three
// Cronies picked her and the party wiped in 13 s (STAGE-SMOKE3-DIAG item 1).
// So before the first keg the party stacks on the tank: every living member
// within GATHER_RADIUS of it. Whoever Hurley then picks stands beside the tank.
//
//   * a keg is already used (the chain is under way) -> Wake (never hold
//                                                       a chain half-done)
//   * every living member is gathered                -> Wake
//   * someone straggles, inside GATHER_WAIT_MS of
//     the hook step's start                          -> Gather (walk the bot
//                                                       stragglers in, Running)
//   * someone straggles, past the wait               -> WakeUngathered (go
//                                                       anyway, logged)
namespace DcBrdHurleyWake
{
    constexpr float GATHER_RADIUS = 6.0f;
    // The walk aims inside the radius, so arriving reads "gathered" at once.
    constexpr float GATHER_AIM = 3.0f;
    // Bounded: a member that cannot reach the tank (stuck, a human looking
    // elsewhere) costs the event this much, not its 60 s step timeout.
    constexpr uint32 GATHER_WAIT_MS = 15000;
    // Floor between two walks of the stragglers.
    constexpr uint32 GATHER_NUDGE_MS = 2000;

    enum class Gate : uint8
    {
        Wake,            // use the next keg
        Gather,          // hold: walk the stragglers to the tank
        WakeUngathered,  // the wait ran out: use the keg anyway
    };

    inline bool Straggles(float distToTank)
    {
        return distToTank > GATHER_RADIUS;
    }

    // `stepStartMs`: when the hook step was entered (DungeonEventProgress::
    // stepStartMs), so a rewind or a fresh activation gives a fresh wait.
    inline Gate Decide(bool kegUsed, uint32 stragglers, uint32 stepStartMs, uint32 now)
    {
        if (kegUsed || stragglers == 0)
            return Gate::Wake;
        if (static_cast<uint32>(now - stepStartMs) < GATHER_WAIT_MS)
            return Gate::Gather;
        return Gate::WakeUngathered;
    }

    struct Aim
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Where a straggler walks: GATHER_AIM from the tank, on the straggler's own
    // side, so nobody walks through the tank to stand on its far side.
    inline Aim AimFor(float memberX, float memberY, float tankX, float tankY)
    {
        float const d = std::hypot(memberX - tankX, memberY - tankY);
        if (d <= GATHER_AIM)
            return { memberX, memberY };
        return { tankX + (memberX - tankX) / d * GATHER_AIM, tankY + (memberY - tankY) / d * GATHER_AIM };
    }
}

#endif  // _PLAYERBOT_DCBRDHURLEYWAKEDECISION_H
