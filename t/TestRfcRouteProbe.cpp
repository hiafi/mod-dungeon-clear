/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Navmesh probe for the Ragefire Chasm rework (map 389): the four legs of the
// kill order, the funnel wall on the way to Oggleflint's den, Taragaman's dormant
// hub, and the reached-only checkpoint's revive point.
//
// Not a committed regression: reads the FULL mmaps dir from env DC_PROBE_MMAPS
// and GTEST_SKIPs when unset, same contract as the other route probes.
//
//   DC_PROBE_MMAPS=<dir holding mmaps/> ./dungeon_clear_tests --gtest_filter='RagefireChasmRouteProbe.*'

#include "gtest/gtest.h"
#include "NavHarness.h"

#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

#include "DetourNavMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace
{
    namespace R = DcRagefireChasm;

    // A map id with no rows: the router on the same mesh without the fence.
    constexpr uint32_t UNFENCED = 99999;

    struct Pt { float x, y, z; char const* name; };

    constexpr Pt ENTRANCE   = { 3.81f, -14.82f, -17.84f, "entrance" };
    constexpr Pt OGGLEFLINT = { -147.55f, 38.71f, -38.80f, "Oggleflint" };
    constexpr Pt JERGOSH    = { -376.81f, 209.22f, -21.80f, "Jergosh" };
    constexpr Pt BAZZALAN   = { -384.87f, 146.04f, 7.82f, "Bazzalan" };
    constexpr Pt HALL_RAMP  = { R::HALL_RAMP_X, R::HALL_RAMP_Y, R::HALL_RAMP_Z, "hall ramp" };
    constexpr Pt WEST_BANK  = { R::WEST_BANK_X, R::WEST_BANK_Y, R::WEST_BANK_Z, "west bank" };
    constexpr Pt TARAGAMAN  = { -244.74f, 150.09f, -18.75f, "Taragaman" };
    constexpr Pt CHECKPOINT = { -154.8f, 70.2f, -21.1f, "checkpoint" };

    // His detection (12yd) with both reaches, the aggro margin and some slack.
    constexpr float HUB_CLEAR = 20.0f;

    std::shared_ptr<dtNavMesh> LoadOrSkip()
    {
        char const* dir = std::getenv("DC_PROBE_MMAPS");
        if (!dir || !*dir)
            return nullptr;
        return DcNavHarness::LoadMap(dir, R::MAP_ID);
    }

    struct Leg
    {
        DcNavHarness::RouteResult r;
        int inFence = 0;
        float hub = std::numeric_limits<float>::max();  // closest approach to his spawn (XY)
    };

    Leg Walk(dtNavMesh const* mesh, Pt const& a, Pt const& b, uint32_t mapId = R::MAP_ID)
    {
        Leg leg;
        leg.r = DcNavHarness::Route(mesh, mapId, a.x, a.y, a.z, b.x, b.y, b.z);
        for (std::size_t i = 0; i < leg.r.points.size(); ++i)
        {
            G3D::Vector3 const& p = leg.r.points[i];
            if (DcNavPenaltyRegistry::IsInsideRegion(R::MAP_ID, p.x, p.y, p.z))
                ++leg.inFence;
            leg.hub = std::min(leg.hub, std::hypot(p.x - TARAGAMAN.x, p.y - TARAGAMAN.y));
        }
        std::printf("  %-10s -> %-10s complete=%d len2d=%.0f inFence=%d maxStepZ=%.1f hub=%.0fyd %s\n", a.name, b.name,
                    leg.r.corridorComplete, leg.r.routeLength2d, leg.inFence, leg.r.maxStepZ, leg.hub,
                    leg.r.failureReason.c_str());
        EXPECT_TRUE(leg.r.reachable) << a.name << " -> " << b.name;
        EXPECT_TRUE(leg.r.corridorComplete) << a.name << " -> " << b.name;
        return leg;
    }
}

// The run: entrance -> den -> hall -> ramp -> ridge -> west bank -> hub. Every leg is
// complete, never inside the funnel wall, and no leg before the last comes within
// Taragaman's notice (he is dormant until the third kill, and the party must not
// body-pull him on the way to anything else once he is not).
TEST(RagefireChasmRouteProbe, TheKillOrderRoutes)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 389 mmaps missing";

    std::vector<Pt> const legs = { ENTRANCE, OGGLEFLINT, JERGOSH, HALL_RAMP, BAZZALAN, WEST_BANK, TARAGAMAN };
    float total = 0.0f;
    for (std::size_t i = 0; i + 1 < legs.size(); ++i)
    {
        Leg const leg = Walk(mesh.get(), legs[i], legs[i + 1]);
        EXPECT_EQ(leg.inFence, 0) << legs[i].name;
        if (i + 2 < legs.size())
        {
            EXPECT_GT(leg.hub, HUB_CLEAR) << legs[i].name << " -> " << legs[i + 1].name;
        }
        total += leg.r.routeLength2d;
    }
    std::printf("  total %.0f yd\n", total);
    EXPECT_LT(total, 2000.0f);

    // He is reached by the -x arm: the last 25yd come in from his -x side.
    Leg const last = Walk(mesh.get(), WEST_BANK, TARAGAMAN);
    for (G3D::Vector3 const& p : last.r.points)
        if (std::hypot(p.x - TARAGAMAN.x, p.y - TARAGAMAN.y) > 12.0f)
        {
            EXPECT_LT(p.x, TARAGAMAN.x - 8.0f) << p.x << ", " << p.y;
        }
}

// The funnel wall and the den branch. The wall is there to stop the 5yd drop over
// the rib's lip, and must never close the branch: the fenced route is complete,
// clear of every row and free of drops, at a bounded detour; every other leg is
// what it would be without the wall.
TEST(RagefireChasmRouteProbe, TheFunnelWallDetoursButNeverClosesTheDen)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 389 mmaps missing";

    Leg const fenced = Walk(mesh.get(), ENTRANCE, OGGLEFLINT);
    Leg const open = Walk(mesh.get(), ENTRANCE, OGGLEFLINT, UNFENCED);
    EXPECT_EQ(fenced.inFence, 0);
    EXPECT_LT(fenced.r.maxStepZ, 3.0f);
    EXPECT_GT(open.inFence, 0);          // the cut the wall stops ...
    EXPECT_GT(open.r.maxStepZ, 4.5f);    // ... and why
    EXPECT_LT(fenced.r.routeLength2d - open.r.routeLength2d, 90.0f);

    for (Pt const& to : { JERGOSH, BAZZALAN, TARAGAMAN })
        EXPECT_LT(Walk(mesh.get(), ENTRANCE, to).r.routeLength2d -
                  Walk(mesh.get(), ENTRANCE, to, UNFENCED).r.routeLength2d, 10.0f) << to.name;
    EXPECT_LT(Walk(mesh.get(), OGGLEFLINT, JERGOSH).r.routeLength2d -
              Walk(mesh.get(), OGGLEFLINT, JERGOSH, UNFENCED).r.routeLength2d, 10.0f);
}

// The way up to Bazzalan. Straight from the hall the router climbs the side of the
// ramp, over 5yd in one 4yd step (52 degrees, more than a player walks); through
// the hall ramp objective it stays on the ramp, at no extra length. A leader who
// latches the objective from its arrive radius short of it goes on up just the same.
TEST(RagefireChasmRouteProbe, TheHallRampIsClimbedNotItsSide)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 389 mmaps missing";

    Leg const direct = Walk(mesh.get(), JERGOSH, BAZZALAN);
    EXPECT_GT(direct.r.maxStepZ, 4.8f);    // the face, and why the objective exists

    // Along both legs no step of a walkable length is steeper than 45 degrees.
    auto const steepest = [](Leg const& leg)
    {
        float worst = 0.0f;
        for (std::size_t i = 1; i < leg.r.points.size(); ++i)
        {
            G3D::Vector3 const& a = leg.r.points[i - 1];
            G3D::Vector3 const& b = leg.r.points[i];
            float const d = std::hypot(b.x - a.x, b.y - a.y);
            if (d > 1.0f)
                worst = std::max(worst, std::fabs(b.z - a.z) / d);
        }
        return worst;
    };
    Leg const up = Walk(mesh.get(), JERGOSH, HALL_RAMP);
    Leg const on = Walk(mesh.get(), HALL_RAMP, BAZZALAN);
    EXPECT_LT(steepest(up), 1.0f);
    EXPECT_LT(steepest(on), 1.0f);
    EXPECT_LT(up.r.routeLength2d + on.r.routeLength2d, direct.r.routeLength2d + 5.0f);

    for (G3D::Vector3 const& p : up.r.points)
        if (std::hypot(p.x - HALL_RAMP.x, p.y - HALL_RAMP.y, p.z - HALL_RAMP.z) <= R::HALL_RAMP_ARRIVE_RADIUS)
        {
            Leg const early = Walk(mesh.get(), { p.x, p.y, p.z, "ramp, early" }, BAZZALAN);
            EXPECT_LT(steepest(early), 1.0f) << p.x << ", " << p.y;
        }
}

// The reached-only checkpoint (mod-dungeon-checkpoint, order 0): a party that
// comes back there reaches every boss that can be left, and the west bank leg
// does not cross the hub. Oggleflint too, for a group that went past him.
TEST(RagefireChasmRouteProbe, TheCheckpointStrandsNobody)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 389 mmaps missing";

    for (Pt const& to : { JERGOSH, HALL_RAMP, BAZZALAN, WEST_BANK, OGGLEFLINT })
    {
        Leg const leg = Walk(mesh.get(), CHECKPOINT, to);
        EXPECT_EQ(leg.inFence, 0) << to.name;
        EXPECT_GT(leg.hub, HUB_CLEAR) << to.name;
    }
    Walk(mesh.get(), CHECKPOINT, TARAGAMAN);
}

// Every authored point sits on the mesh, on its own floor.
TEST(RagefireChasmRouteProbe, TheSpotsAreOnTheNavmesh)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 389 mmaps missing";

    for (Pt const& p : { ENTRANCE, OGGLEFLINT, JERGOSH, HALL_RAMP, BAZZALAN, WEST_BANK, TARAGAMAN, CHECKPOINT })
    {
        G3D::Vector3 s;
        bool const snapped = DcNavHarness::NearestPoint(mesh.get(), p.x, p.y, p.z, 2.0f, 3.0f, s);
        std::printf("  %-10s -> snapped=%d (%.2f, %.2f, %.2f)\n", p.name, snapped, s.x, s.y, s.z);
        ASSERT_TRUE(snapped) << p.name << " is off the navmesh";
        EXPECT_LT(std::hypot(s.x - p.x, s.y - p.y), 0.5f) << p.name;
        EXPECT_LT(std::fabs(s.z - p.z), 1.5f) << p.name << " snapped to another floor";
    }
}
