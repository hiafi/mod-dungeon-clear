/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcReleaseRecovery.h"

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPartyState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPlayerbotCompat.h"
#include "Ai/Dungeon/DungeonClear/Util/DcReleaseDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunProgress.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunWing.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestRunManager.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <unordered_map>

#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QueryResult.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"

namespace
{
    namespace Kernel = DcReleaseDecision;

    // Written once on the world thread before the first map update, read-only after.
    std::unordered_map<uint32, std::vector<Kernel::Spot>> checkpointSpots;

    // DungeonCheckpoint.Enable, read on the world thread (load and config reload).
    std::atomic<bool> checkpointsOn{false};

    // The bot whose release opcode is on this thread's stack right now.
    thread_local ObjectGuid releasing;

    Kernel::Rules RulesOf(Player* member)
    {
        Kernel::Rules r;
        r.releaseAfterMs = DcSettings::GetUInt(member, "ReleaseAfterSecs") * 1000;
        r.walkLimitMs = DcSettings::GetUInt(member, "ReleaseWalkLimitSecs") * 1000;
        r.maxReleases = DcSettings::GetUInt(member, "ReleaseMaxPerBot");
        r.quietGraceMs = DC_NO_REZZER_QUIET_GRACE_MS;
        return r;
    }

    // Where a run that is not a `.dc test` run started: its wing's catalogue row, or the
    // map's only one. Two rows and no wing: unknown, and unknown refuses.
    bool CatalogueStart(Player* owner, uint32 mapId, Kernel::Point& start)
    {
        std::string const wing = DcRunWing::ActiveWingToken(owner);
        DcTestDungeonRegistry::Row const* only = nullptr;
        bool several = false;
        for (DcTestDungeonRegistry::Row const& r : DcTestDungeonRegistry::All())
        {
            if (r.mapId != mapId || DcTestDungeonRegistry::IsScenario(r))
                continue;
            if (!wing.empty() && wing == r.token)
            {
                start = {r.x, r.y, r.z};
                return true;
            }
            several = only != nullptr;
            only = &r;
        }
        if (!only || several || !wing.empty())
            return false;
        start = {only->x, only->y, only->z};
        return true;
    }

    // Does the feature apply to this run right now? Cheapest first, and it refuses BEFORE
    // releasing: without a checkpoint on the run's own ground the ghost leaves the
    // instance or is revived in a wing nobody cleared, and either way no longer raisable.
    bool AppliesTo(Player* owner, Kernel::Point& start)
    {
        Map* const map = owner->FindMap();
        uint32 const mode = DcSettings::GetUInt(ObjectGuid::Empty, "ReleaseWalkBack");
        if (mode == static_cast<uint32>(Kernel::Mode::Off) || !map || !map->IsDungeon())
            return false;
        // The start of a test run doubles as "is this a test run".
        bool const testRun = DcTestRunManager::Instance().TestRunStart(owner->GetGUID(), start.x, start.y, start.z);
        if (!Kernel::Applies(mode, testRun, map->IsRaid()))
            return false;
        // With PreventBotRelease off stock releases the bots itself. The Oculus: its rings
        // are joined by air only, and it has its own regroup.
        if (!checkpointsOn.load(std::memory_order_relaxed) || map->GetId() == DcOculus::MAP_ID ||
            !DcSettings::GetBool(owner, "PreventBotRelease"))
            return false;
        auto const it = checkpointSpots.find(map->GetId());
        return it != checkpointSpots.end() && (testRun || CatalogueStart(owner, map->GetId(), start)) &&
               Kernel::HasOwnSpot(it->second, start);
    }

    bool InRegion(DcReleaseState const& rel, Player const* body)
    {
        auto const it = checkpointSpots.find(body->GetMapId());
        return it != checkpointSpots.end() &&
               Kernel::RevivesInRegion(it->second, {rel.startX, rel.startY, rel.startZ},
                                       {body->GetPositionX(), body->GetPositionY(), body->GetPositionZ()});
    }

    // A resurrect being cast on a member of the party, or one already offered to a body
    // and not yet accepted.
    bool AnyRezUnderway(std::vector<Player*> const& players)
    {
        for (Player const* p : players)
        {
            if (p->isDead())
            {
                if (p->isResurrectRequested())
                    return true;
                continue;
            }
            Spell const* const spell = p->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            SpellInfo const* const info = spell ? spell->GetSpellInfo() : nullptr;
            if (info && (info->HasEffect(SPELL_EFFECT_RESURRECT) || info->HasEffect(SPELL_EFFECT_RESURRECT_NEW)))
                return true;
        }
        return false;
    }

    Kernel::Body BodyOf(DcReleaseMember const& m, Player* body)
    {
        Kernel::Body b;
        b.isBot = GET_PLAYERBOT_AI(body) != nullptr && !DcPlayerbotCompat::IsSelfBot(body);
        b.isGhost = body->HasPlayerFlag(PLAYER_FLAGS_GHOST);
        b.canRelease = m.inRegion && body->m_InstanceValid && !body->HasPreventResurectionAura();
        b.rezUnderway = m.rezUnderway;
        b.deadMs = m.deadMs;
        b.releases = m.releases;
        return b;
    }

    Kernel::Scene SceneOf(DcReleaseState const& rel)
    {
        Kernel::Scene s;
        s.anyAlive = rel.anyAlive;
        s.partyEngaged = rel.engaged;
        s.rezBlocked = rel.rezBlocked;
        s.anyFlagged = rel.anyFlagged;
        s.quietMs = rel.quietMs;
        return s;
    }

    bool IsWalking(DcRunState const& run, Player const* p)
    {
        DcReleaseMember const* const m = run.release.Find(p->GetGUID());
        return m && m->walking;
    }

    // Who a walker walks to: the run's owner while it stands with the party; with the
    // owner dead or itself on the way back, the nearest member still standing where the
    // party was left. Nobody standing: the owner's own spot, and for the owner nobody.
    Player* RallyFor(Player* walker, Player* owner, DcRunState const& run)
    {
        if (owner != walker && owner->IsAlive() && !IsWalking(run, owner))
            return owner;
        Player* best = nullptr;
        if (Group* const group = walker->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* const p = ref->GetSource();
                if (!p || p == walker || !p->IsInWorld() || !p->IsAlive() || p->FindMap() != walker->FindMap() ||
                    IsWalking(run, p))
                    continue;
                if (!best || walker->GetDistance(p) < walker->GetDistance(best))
                    best = p;
            }
        return best ? best : (owner != walker && owner->FindMap() == walker->FindMap() ? owner : nullptr);
    }

    struct Destination
    {
        bool     ok = false;
        Position pos;
        float    dist = 0.0f;
        float    spread = 0.0f;
    };

    // Where that is, and how close counts: what the leader's own advance gate measures
    // (a camp in pull mode), so an arrival is exactly what lets the run move again.
    Destination DestinationOf(Player* walker, Player* owner, DcRunState const& run)
    {
        Destination d;
        Player* const rally = RallyFor(walker, owner, run);
        if (!rally)
            return d;
        d.ok = true;
        d.pos = rally->GetPosition();
        d.dist = rally->GetDistance(walker);
        d.spread = DcSettings::GetFloat(walker, "PartyMaxSpread");
        PlayerbotAI* const ownerAI = GET_PLAYERBOT_AI(owner);
        if (rally != owner || !owner->IsAlive() || !ownerAI)
            return d;
        DcPartyState::SpreadGate const gate = DcPartyState::GetSpreadGate(owner, ownerAI->GetAiObjectContext());
        if (gate.maxSpread < 1000.0f)  // not waived
            d.spread = gate.maxSpread;
        if (gate.anchor)
        {
            d.pos = *gate.anchor;
            d.dist = walker->GetDistance(*gate.anchor);
        }
        return d;
    }

    void FileOutcome(DcRunState& run, DcReleaseMember const& m, char const* outcome)
    {
        for (auto it = run.releaseLog.rbegin(); it != run.releaseLog.rend(); ++it)
            if (it->guid == m.guid && it->outcome.empty())
            {
                it->outcome = outcome;
                it->walkS = m.walkMs / IN_MILLISECONDS;
                return;
            }
    }

    // The owner of `bot`'s run with the feature live on it, dead or alive.
    DcRunState* LiveRun(Player* bot, Player*& owner)
    {
        owner = bot ? DcLeaderSignal::FindRunOwner(bot) : nullptr;
        PlayerbotAI* const ownerAI = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
        if (!ownerAI)
            return nullptr;
        DcRunState& run = DcRun::Of(ownerAI);
        return run.enabled && !run.paused && run.release.applies ? &run : nullptr;
    }
}

namespace DcReleaseRecovery
{
    void RefreshCheckpointSwitch()
    {
        // Default OFF: the key exists exactly when that module is compiled in, because
        // only then is its conf.dist loaded.
        checkpointsOn = sConfigMgr->GetOption<bool>("DungeonCheckpoint.Enable", false, false);
    }

    void LoadCheckpointSpots()
    {
        RefreshCheckpointSwitch();
        checkpointSpots.clear();
        // The column is asked for first: a query on a table or column that is not there
        // aborts the server.
        if (QueryResult column = WorldDatabase.Query(
                "SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE() "
                "AND table_name = 'dungeon_checkpoint_location' AND column_name = 'reach_radius'"))
        {
            // The map's lowest entrance row (no creature, not a "reached only" spot) sets
            // the checkpoint every new instance starts with, so it is active from the first
            // step; every row of that order is one of its respawn spots.
            if (QueryResult result = WorldDatabase.Query(
                    "SELECT l.map_id, l.position_x, l.position_y, l.position_z, l.reach_radius "
                    "FROM dungeon_checkpoint_location l JOIN (SELECT map_id, MIN(checkpoint_order) AS first_order "
                    "FROM dungeon_checkpoint_location WHERE creature_entry = 0 AND reach_radius = 0 GROUP BY map_id) f "
                    "ON f.map_id = l.map_id AND f.first_order = l.checkpoint_order"))
                do
                {
                    Field* const fields = result->Fetch();
                    Kernel::Spot s;
                    s.x = fields[1].Get<float>();
                    s.y = fields[2].Get<float>();
                    s.z = fields[3].Get<float>();
                    s.reach = fields[4].Get<float>();
                    checkpointSpots[fields[0].Get<uint32>()].push_back(s);
                } while (result->NextRow());
        }
        LOG_INFO("playerbots.dungeonclear", "[DC] release and walk back: {} map(s) with an entrance checkpoint{}",
                 checkpointSpots.size(),
                 checkpointsOn ? "" : "; the checkpoint module is off or absent, bots never release");
    }

    bool Refresh(Player* owner, DcRunState& run)
    {
        DcReleaseState& rel = run.release;
        Kernel::Point start;
        rel.applies = AppliesTo(owner, start);
        if (!rel.applies)
        {
            for (DcReleaseMember& m : rel.members)
                m.walking = false;  // switched off mid-walk: nobody is waited for
            return false;
        }
        rel.startX = start.x;
        rel.startY = start.y;
        rel.startZ = start.z;
        return true;
    }

    void Track(Player* owner, DcRunState& run, std::vector<DcRezDecision::Member> const& members,
               std::vector<Player*> const& players, bool partyEngaged, bool rezBlocked, uint32 nowMs)
    {
        DcReleaseState& rel = run.release;
        if (!rel.lastMs)
            rel.quietMs = 0;  // first sample of an episode: no quiet on file yet
        uint32 const elapsed = rel.lastMs ? getMSTimeDiff(rel.lastMs, nowMs) : 0;
        rel.lastMs = nowMs ? nowMs : 1;

        rel.engaged = partyEngaged;
        rel.rezBlocked = rezBlocked;
        rel.quietMs = partyEngaged ? 0 : Kernel::Accrue(rel.quietMs, elapsed, false);
        rel.anyAlive = false;
        rel.anyFlagged = false;
        rel.livingRezzer = false;
        bool landed = false;  // a body was raised since the last Track
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            if (members[i].isDead)
                continue;
            rel.anyAlive = true;
            rel.anyFlagged = rel.anyFlagged || players[i]->IsInCombat();
            rel.livingRezzer = rel.livingRezzer || members[i].canRezClass;
            if (DcReleaseMember* const m = rel.Find(players[i]->GetGUID()))
            {
                landed = landed || m->deadMs != 0;
                m->deadMs = 0;
            }
        }

        bool const rezUnderway = AnyRezUnderway(players);
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            if (!members[i].isDead || !members[i].isBot)
                continue;
            DcReleaseMember* m = rel.Find(players[i]->GetGUID());
            if (!m)
            {
                rel.members.emplace_back();
                m = &rel.members.back();
                m->guid = players[i]->GetGUID();
                m->name = players[i]->GetName();
            }
            // A rez that landed earns the bodies still waiting their full time again.
            m->deadMs = landed ? 0 : Kernel::Accrue(m->deadMs, elapsed, partyEngaged || rezBlocked);
            m->rezUnderway = rezUnderway;
            m->corpseUnsafe = members[i].corpseUnsafe;
            m->inRegion = InRegion(rel, players[i]);
        }

        Kernel::Rules const rules = RulesOf(owner);
        for (DcReleaseMember& m : rel.members)
        {
            if (!m.walking)
                continue;
            m.walkMs = Kernel::Accrue(m.walkMs, elapsed, partyEngaged);
            auto const it = std::find_if(players.begin(), players.end(),
                                         [&m](Player const* p) { return p->GetGUID() == m.guid; });
            Kernel::Walker w;
            w.walkMs = m.walkMs;
            if (it == players.end())
                w.dist = std::numeric_limits<float>::max();  // off the map: nothing but the limit ends it
            else
            {
                Destination const d = DestinationOf(*it, owner, run);
                w.alive = (*it)->IsAlive();
                w.hasRally = d.ok;
                w.dist = d.dist;
                w.spread = d.spread;
            }

            switch (Kernel::DecideWalk(rules, w))
            {
                case Kernel::Walk::Walking:
                    break;
                case Kernel::Walk::Arrived:
                    m.walking = false;
                    FileOutcome(run, m, "arrived");
                    run.BumpEventProgress();
                    DcRunProgress::Stamp(run.progress, nowMs);
                    LOG_INFO("playerbots.dungeonclear", "[DC:{}] walk back: {} is back with the party after {}s",
                             owner->GetName(), m.name, m.walkMs / IN_MILLISECONDS);
                    break;
                case Kernel::Walk::Died:
                    m.walking = false;
                    FileOutcome(run, m, "died");
                    LOG_INFO("playerbots.dungeonclear", "[DC:{}] walk back: {} died on the way after {}s",
                             owner->GetName(), m.name, m.walkMs / IN_MILLISECONDS);
                    break;
                case Kernel::Walk::TimedOut:
                    if (m.walkTimedOut)
                        break;
                    m.walkTimedOut = true;
                    FileOutcome(run, m, "timeout");
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] walk back: {} did not reach the party in {}s ({:.0f}yd short) -> ending the run",
                             owner->GetName(), m.name, m.walkMs / IN_MILLISECONDS, w.dist);
                    break;
            }
        }
    }

    bool HasWalkers(DcRunState const& run)
    {
        for (DcReleaseMember const& m : run.release.members)
            if (m.walking)
                return true;
        return false;
    }

    bool WillRelease(DcRunState const& run, Player* body)
    {
        if (!run.release.applies || !body || !body->isDead())
            return false;
        DcReleaseMember const* const m = run.release.Find(body->GetGUID());
        return m && Kernel::WillRelease(RulesOf(body), BodyOf(*m, body));
    }

    bool AnyWillRelease(DcRunState const& run, std::vector<Player*> const& players)
    {
        for (Player* p : players)
            if (WillRelease(run, p))
                return true;
        return false;
    }

    void ApplyWalkVerdict(DcRunState const& run, DcRezDecision::Result& verdict, std::string& who)
    {
        DcReleaseState const& rel = run.release;
        DcReleaseMember const* walker = nullptr;
        DcReleaseMember const* late = nullptr;
        for (DcReleaseMember const& m : rel.members)
        {
            if (!m.walking)
                continue;
            walker = walker ? walker : &m;
            if (m.walkTimedOut && !late)
                late = &m;
        }
        Kernel::FoldWalk(verdict, !rel.failedName.empty(), late != nullptr, walker != nullptr);
        if (verdict.reason == DcRezDecision::Reason::ReleaseFailed)
            who = rel.failedName;
        else if (verdict.reason == DcRezDecision::Reason::WalkTimedOut && late)
            who = late->name;
        else if (verdict.reason == DcRezDecision::Reason::WalkingBack && walker)
            who = walker->name;
    }

    bool ShouldRelease(Player* bot)
    {
        if (!bot || !bot->isDead() || !bot->IsInWorld() || bot->IsBeingTeleported() ||
            bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
            return false;
        // A corpse advances the release clocks of a run the feature applies to and
        // nothing else: the rez clocks belong to the living (DcRezDecision::MayAdvance).
        if (!DcRezRecovery::AdvanceRelease(bot))
            return false;
        Player* owner = nullptr;
        DcRunState const* const run = LiveRun(bot, owner);
        DcReleaseMember const* const m = run ? run->release.Find(bot->GetGUID()) : nullptr;
        return m && run->release.failedName.empty() &&
               Kernel::ReleaseNow(RulesOf(bot), SceneOf(run->release), BodyOf(*m, bot));
    }

    void Begin(Player* bot)
    {
        releasing = bot->GetGUID();
    }

    bool IsReleasing(Player const* player)
    {
        return player && !releasing.IsEmpty() && player->GetGUID() == releasing;
    }

    bool Finish(Player* bot)
    {
        releasing.Clear();
        Player* owner = nullptr;
        DcRunState* const run = LiveRun(bot, owner);
        DcReleaseMember* const m = run ? run->release.Find(bot->GetGUID()) : nullptr;
        if (!m)
            return false;

        uint32 const nowMs = getMSTime();
        // The guard on the repop hook keeps an unanswered ghost where it stands, so
        // "alive" is the whole question; the map is checked for the belt.
        bool const back = bot->IsAlive() && !bot->IsBeingTeleportedFar() && bot->FindMap() == owner->FindMap();
        char const* const why = Kernel::WhyNoRez(run->release.livingRezzer, m->corpseUnsafe, m->rezUnderway);
        uint32 const deadS = m->deadMs / IN_MILLISECONDS;
        ++m->releases;

        DcReleaseLogEntry entry;
        entry.guid = m->guid;
        entry.name = m->name;
        entry.atMs = nowMs;
        entry.deadS = deadS;
        entry.why = why;
        entry.outcome = back ? "" : "failed";
        if (run->releaseLog.size() < DcRunState::kMaxReleaseLog)
            run->releaseLog.push_back(std::move(entry));

        if (!back)
        {
            run->release.failedName = m->name;
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] release: {} released (dead {}s out of combat, {}) but no checkpoint revived it "
                     "-> ending the run", owner->GetName(), m->name, deadS, why);
            return false;
        }

        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] release: {} releases and walks back (dead {}s out of combat, {}; release {} of {})",
                 owner->GetName(), m->name, deadS, why, m->releases, RulesOf(owner).maxReleases);

        m->deadMs = 0;
        m->walkMs = 0;
        m->walking = true;
        // The party's situation just changed: whoever is still down gets a fresh rez
        // budget (the one that released may be the rezzer), and the no-progress clocks
        // see the one step this wait was for. Capped releases bound both.
        run->rezPendingSinceMs = 0;
        run->BumpEventProgress();
        DcRunProgress::Stamp(run->progress, nowMs);

        PlayerbotAI* const botAI = GET_PLAYERBOT_AI(bot);
        if (bot == owner && botAI)
        {
            // Every cursor of the drive points at where the tank died.
            AiObjectContext* const ctx = botAI->GetAiObjectContext();
            ctx->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().Reset();
            ctx->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
        }
        if (botAI)
            DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + m->name + " released and is walking back.");
        return true;
    }

    Player* DueBody(Player* bot)
    {
        Player* owner = nullptr;
        DcRunState const* const run = LiveRun(bot, owner);
        Group* const group = run ? bot->GetGroup() : nullptr;
        if (!group || !run->release.failedName.empty())
            return nullptr;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* const p = ref->GetSource();
            if (!p || !p->IsInWorld() || !p->isDead() || p->FindMap() != bot->FindMap())
                continue;
            DcReleaseMember const* const m = run->release.Find(p->GetGUID());
            if (m && Kernel::ReleaseNow(RulesOf(owner), SceneOf(run->release), BodyOf(*m, p)))
                return p;
        }
        return nullptr;
    }

    bool WalkTarget(Player* bot, Position& dest)
    {
        Player* owner = nullptr;
        DcRunState const* const run = LiveRun(bot, owner);
        if (!run || !bot->IsAlive() || !IsWalking(*run, bot))
            return false;
        Destination const d = DestinationOf(bot, owner, *run);
        if (!d.ok)
            return false;
        dest = d.pos;
        return true;
    }

    bool IsWalkingBack(Player* bot)
    {
        Player* owner = nullptr;
        DcRunState const* const run = LiveRun(bot, owner);
        return run && bot->IsAlive() && IsWalking(*run, bot);
    }

    bool IsLeaderWalkingBack(Player* bot)
    {
        Player* owner = nullptr;
        DcRunState const* const run = LiveRun(bot, owner);
        return run && owner != bot && owner->IsAlive() && IsWalking(*run, owner);
    }

    bool IsLeaderWalkingFar(Player* bot)
    {
        Player* owner = nullptr;
        DcRunState const* const run = LiveRun(bot, owner);
        return run && owner != bot && owner->IsAlive() && IsWalking(*run, owner) &&
               bot->GetDistance(owner) > DcSettings::GetFloat(bot, "PartyMaxSpread");
    }
}
