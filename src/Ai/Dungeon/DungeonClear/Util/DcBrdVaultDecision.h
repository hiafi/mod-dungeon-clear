/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCBRDVAULTDECISION_H
#define _PLAYERBOT_DCBRDVAULTDECISION_H

#include "Define.h"

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

// PURE kernel for the reworked Blackrock Depths Vault (map 230, event 9): what
// does the leader do this tick? The game side (RunTheVault in
// BlackrockDepthsEvents.cpp) reads the world into a View and acts on the answer.
//
// The Vault is optional and key-gated, exactly as for a player: the rework
// starts it when the Dark Coffer door 174565 (lock 659, the Dark Keeper Key) is
// opened AFTER Warder Stilgiss and Verek are both dead (the instance refuses the
// trigger before that). Opening the coffer never uses the key up: the instance
// consumes one when the Vault COMPLETES (user ruling 2026-11-17). A wipe puts
// TYPE_VAULT back to NOT_STARTED, and the already-open coffer's gossip starts it
// again for free. So, in order:
//
//   * TYPE_VAULT DONE                        -> Finish (the event is over)
//   * TYPE_VAULT IN_PROGRESS, or a state in
//     between (FAIL settling to NOT_STARTED,
//     TO_BE_DECIDED before the first Update) -> Hold (fight; between fights stay
//                                               on the hold spot)
//   * the warders' slot is not DONE, but the
//     fight is settling (Stilgiss's credit bit
//     is set, or a warder still stands near
//     the Vault: evaded, leashed)            -> Hold (the slot follows)
//   * the warders' slot is not DONE, and no
//     sign of a settling fight               -> Skip (the instance would refuse;
//                                               also a stock instance, which has
//                                               no slot 6 and reads TO_BE_DECIDED)
//   * the party already lost VAULT_MAX_ATTEMPTS
//     attempts                               -> Skip (gave up: on to Fineous)
//   * the coffer is not in the grid yet      -> Hold
//   * the coffer is shut, and:
//       a living bot carries a key           -> go on below
//       only a living HUMAN carries one      -> WaitForHuman (never open it for
//       (a `.bot self` human included)          them: the human opens it, user
//                                               ruling 2026-11-17)
//       only a dead or absent member does    -> WaitForKey (hold until they are
//                                               back; the event timeout bounds it)
//       nobody                               -> Skip (a bot party never cheats a
//                                               loot key; the DC door policy waives
//                                               keys on traversal doors only)
//   * someone is dead, absent or outside the
//     rim, and the muster wait has not run
//     out (timed from the leader's return,
//     NextMusterStamp)                       -> Muster (the start seals the
//                                               entrance)
//   * the coffer is shut and the bot key
//     holder is not in reach of it           -> BringKey
//   * otherwise                              -> Open (Use() the coffer: as the bot
//                                               key holder while it is shut, as
//                                               the leader once it is open, which
//                                               only re-runs its gossip)
namespace DcBrdVault
{
    enum class Action : uint8
    {
        Finish,       // the Vault is DONE: the event completes
        Skip,         // the Vault cannot start for this party: the event completes, untouched
        Hold,         // it is running (or settling): stay on the hold spot between fights
        Muster,       // wait on the hold spot for the party to come inside the rim
        BringKey,     // walk the bot key holder to the coffer
        Open,         // Use() the coffer now
        WaitForHuman, // only a human carries the key: hold and let them open the coffer
        WaitForKey,   // only a dead or absent member carries the key: hold until they are back
    };

    struct View
    {
        uint32 vaultState = DcBlackrockDepths::STATE_NOT_STARTED;  // GetData(TYPE_VAULT)
        uint32 wardersState = 0;      // GetBossState(SLOT_STILGISS_VEREK), as uint32
        bool wardersSettling = false; // Stilgiss's credit bit is set, or a warder lives near the Vault
        uint32 failedAttempts = 0;    // attempts DC started that ended without DONE
        bool cofferLoaded = false;    // the Dark Coffer door is in the grid
        bool cofferOpen = false;      // its GO state is not READY (someone opened it)
        bool keyOnBot = false;        // a living bot member on the map carries a Dark Keeper Key
        bool keyOnHuman = false;      // ... a living human member does (a `.bot self` one included)
        bool keyOnAbsent = false;     // ... a dead member does, or one off the map was last seen with one
        bool keyInReach = false;      // the bot key holder stands within VAULT_KEY_REACH of the coffer
        bool mustered = false;        // every member is alive, on the map and inside the rim
        bool musterExpired = false;   // the muster wait ran out (MusterExpired)
    };

    // An attempt is live (or settling after a wipe) while the Vault is neither
    // NOT_STARTED nor DONE. The hook re-stamps the step clock on such ticks
    // (up to VAULT_ATTEMPT_LIVE_CAP_MS, see HoldsStepClock), so the event
    // timeout counts only the time spent between attempts.
    inline bool AttemptLive(uint32 vaultState)
    {
        namespace B = DcBlackrockDepths;
        return vaultState != B::STATE_NOT_STARTED && vaultState != B::STATE_DONE;
    }

    // A clock stamp that is never 0 (0 means "not running").
    inline uint32 StampOf(uint32 now)
    {
        return now ? now : 1u;
    }

    // The muster clock (R-2-B MJ-2). After a wipe the leader respawns at S2a,
    // about 750yd away, and the hook keeps ticking during the ~110 s run back;
    // a clock that started when the Vault went back to NOT_STARTED has run out
    // before anyone is home, and the bot key holder opens the coffer with the
    // party still at the entrance. So the wait is timed from the walk-back's
    // END: it starts on the first tick the leader stands inside the rim with
    // the Vault NOT_STARTED and the coffer in the grid, and is cleared while
    // the coffer is out of the grid (the leader is away) or an attempt runs.
    // It stays armed while the leader steps out of the rim within the coffer's
    // scan (walking a key in), so it never restarts on the coffer's doorstep.
    inline uint32 NextMusterStamp(uint32 stamp, uint32 vaultState, bool cofferLoaded,
                                  bool leaderInsideRim, uint32 now)
    {
        namespace B = DcBlackrockDepths;
        if (vaultState != B::STATE_NOT_STARTED || !cofferLoaded)
            return 0;
        if (stamp)
            return stamp;
        return leaderInsideRim ? StampOf(now) : 0u;
    }

    inline bool MusterExpired(uint32 stamp, uint32 now)
    {
        return stamp != 0 && static_cast<uint32>(now - stamp) >= DcBlackrockDepths::VAULT_MUSTER_WAIT_MS;
    }

    // The live-attempt clock (R-2-B MN-2): when the current attempt went live,
    // 0 while none is. An attempt may hold the step clock for
    // VAULT_ATTEMPT_LIVE_CAP_MS at most, so a Vault kept IN_PROGRESS forever
    // (a warder held in combat by a stuck unit) still ends in the timeout.
    inline uint32 NextLiveStamp(uint32 stamp, uint32 vaultState, uint32 now)
    {
        if (!AttemptLive(vaultState))
            return 0;
        return stamp ? stamp : StampOf(now);
    }

    inline bool HoldsStepClock(uint32 liveStamp, uint32 now)
    {
        return liveStamp != 0 &&
               static_cast<uint32>(now - liveStamp) < DcBlackrockDepths::VAULT_ATTEMPT_LIVE_CAP_MS;
    }

    // Whose Dark Keeper Key a member's is (R-2-B MN-3 / MN-4).
    //   * A member off the leader's map is updated on another map thread, so
    //     their bags are never read: they count by the last reading taken
    //     while they were on this map (lastSeenWithKey), as absent.
    //   * On the map: no key -> None; dead -> Absent (wait for them); a
    //     human, `.bot self` included (DcPlayerbotCompat::IsHumanControlled),
    //     -> Human (DC waits for them, never uses their key); else -> Bot.
    enum class KeyHolder : uint8
    {
        None,
        Bot,
        Human,
        Absent,
    };

    inline KeyHolder ClassifyKeyHolder(bool onMap, bool keyNow, bool lastSeenWithKey, bool alive,
                                       bool humanControlled)
    {
        if (!onMap)
            return lastSeenWithKey ? KeyHolder::Absent : KeyHolder::None;
        if (!keyNow)
            return KeyHolder::None;
        if (!alive)
            return KeyHolder::Absent;
        return humanControlled ? KeyHolder::Human : KeyHolder::Bot;
    }

    inline Action Decide(View const& v)
    {
        namespace B = DcBlackrockDepths;
        if (v.vaultState == B::STATE_DONE)
            return Action::Finish;
        if (v.vaultState != B::STATE_NOT_STARTED)
            return Action::Hold;
        if (v.wardersState != B::STATE_DONE)
            return v.wardersSettling ? Action::Hold : Action::Skip;
        if (v.failedAttempts >= B::VAULT_MAX_ATTEMPTS)
            return Action::Skip;
        if (!v.cofferLoaded)
            return Action::Hold;
        if (!v.cofferOpen && !v.keyOnBot)
        {
            if (v.keyOnHuman)
                return Action::WaitForHuman;
            if (v.keyOnAbsent)
                return Action::WaitForKey;
            return Action::Skip;
        }
        if (!v.mustered && !v.musterExpired)
            return Action::Muster;
        if (!v.cofferOpen && !v.keyInReach)
            return Action::BringKey;
        return Action::Open;
    }

    // Whether a point is inside the Vault for the muster: within the rim and on
    // its floor, not on a level above or below it.
    inline bool InsideRim(float x, float y, float z)
    {
        namespace B = DcBlackrockDepths;
        float const dx = x - B::VAULT_CENTER_X;
        float const dy = y - B::VAULT_CENTER_Y;
        float const dz = z - B::VAULT_FLOOR_Z;
        return dx * dx + dy * dy <= B::VAULT_MUSTER_RADIUS * B::VAULT_MUSTER_RADIUS &&
               dz <= B::VAULT_MUSTER_ZBAND && dz >= -B::VAULT_MUSTER_ZBAND;
    }

    inline char const* ActionName(Action a)
    {
        switch (a)
        {
            case Action::Finish:   return "finish";
            case Action::Skip:     return "skip";
            case Action::Hold:     return "hold";
            case Action::Muster:   return "muster";
            case Action::BringKey: return "bring-key";
            case Action::Open:     return "open";
            case Action::WaitForHuman: return "wait-for-human";
            case Action::WaitForKey:   return "wait-for-key";
        }
        return "?";
    }
}

#endif  // _PLAYERBOT_DCBRDVAULTDECISION_H
