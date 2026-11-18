/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Navmesh probe for the Blackrock Depths wing split (map 230): do the Finder
// sections that never clear the Detention Block (2, 3 and 4, each from its own
// start) route clear of the Ring of Law?
//
// The Ring of Law is a sealed arena run by area trigger 1526: a player crossing
// the centre starts the gauntlet and slams the gates. A full-dungeon run went
// through it on purpose (it is a Detention Block encounter). A later section's
// run must not stumble into it — if the navmesh ever routes a leg across the
// arena floor, that section needs a wing-tagged DcNavPenaltyRegistry fence
// there, as Blackrock Spire has.
//
// The suite also PRINTS each leg's closest approach to every Detention Block
// boss, so a route that walks the party past one of them (a boss pull the run
// does not count) is visible.
//
// Not a committed regression: reads the FULL mmaps dir from env DC_PROBE_MMAPS
// and GTEST_SKIPs when unset, same contract as the other route probes.
//
//   DC_PROBE_MMAPS=/home/jared/azerothcore/env/dist/bin \
//     ./dungeon_clear_tests --gtest_filter='BlackrockDepthsRouteProbe.*'

#include "gtest/gtest.h"
#include "NavHarness.h"

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
    constexpr uint32_t BRD = 230;

    struct Pt { float x, y, z; char const* name; };

    // Area trigger 1526, the arena centre on the floor (BlackrockDepthsEvents).
    constexpr Pt ARENA = { 596.432f, -188.498f, -53.9f, "Ring of Law" };
    // The arena floor is ~40yd across; anything within this of the centre, on
    // the floor's level, is inside the sealed ring.
    constexpr float ARENA_FLOOR_R = 25.0f;
    constexpr float ARENA_FLOOR_DZ = 8.0f;

    // Section 3 (the Manufactory) from its Finder start, in clear order.
    std::vector<Pt> const MANUFACTORY = {
        { 607.2f,   -47.8f,  -59.8f,  "S3 start" },
        { 615.61f,  -49.78f, -59.82f, "Shadowforge Lock" },
        { 652.4f,    21.4f,  -60.0f,  "Angerforge" },
        { 846.8f,    16.3f,  -53.6f,  "Argelmach" },
    };

    // Section 4 (the Imperial Seat) from S4b, in clear order (the bar's bosses
    // at their spawns, Plugger at his camp past the Bar Door).
    std::vector<Pt> const IMPERIAL_SEAT = {
        { 843.8f,  -167.8f,  -49.7f,  "S4b" },
        { 878.1f,  -153.1f,  -49.8f,  "Hurley" },
        { 878.5f,  -167.7f,  -49.7f,  "Ribbly" },
        { 869.0f,  -225.0f,  -43.7f,  "Phalanx" },
        { 871.0f,  -235.0f,  -43.27f, "Plugger camp" },
        { 1009.8f, -239.0f,  -61.3f,  "Flamelash" },
        { 1215.1f, -220.7f,  -85.6f,  "The Seven" },
        { 1380.7f, -659.3f,  -92.0f,  "Magmus" },
        { 1380.2f, -831.6f,  -87.6f,  "Thaurissan" },
    };

    std::vector<Pt> const DETENTION_BLOCK = {
        { 310.6f, -146.3f, -70.3f, "Gerstahn" },
        { 615.5f, -267.4f, -83.6f, "Roccor" },
        { 607.7f, -174.8f, -84.5f, "Grebmar" },
        { 530.2f, -243.9f, -43.0f, "Loregrain" },
        { 893.5f, -267.1f, -71.9f, "Incendius" },
        { 823.4f, -342.3f, -50.1f, "Stilgiss" },
        { 963.3f, -343.7f, -71.7f, "Darkvire" },
        { 702.4f,  184.5f, -72.0f, "Bael'Gar" },
    };

    std::shared_ptr<dtNavMesh> LoadOrSkip()
    {
        char const* dir = std::getenv("DC_PROBE_MMAPS");
        if (!dir || !*dir)
            return nullptr;
        return DcNavHarness::LoadMap(dir, BRD);
    }

    Pt SnapOrKeep(dtNavMesh const* mesh, Pt p)
    {
        G3D::Vector3 s;
        if (DcNavHarness::NearestPoint(mesh, p.x, p.y, p.z, 10.0f, 20.0f, s))
            return { s.x, s.y, s.z, p.name };
        return p;
    }

    float Dist3(G3D::Vector3 const& a, Pt const& b)
    {
        float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // The polyline sampled every 2yd: its corners alone can straddle the arena.
    std::vector<G3D::Vector3> Densify(std::vector<G3D::Vector3> const& pts)
    {
        std::vector<G3D::Vector3> out;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        {
            G3D::Vector3 const d = pts[i + 1] - pts[i];
            int const steps = std::max(1, static_cast<int>(d.length() / 2.0f));
            for (int k = 0; k < steps; ++k)
                out.push_back(pts[i] + d * (static_cast<float>(k) / steps));
        }
        if (!pts.empty())
            out.push_back(pts.back());
        return out;
    }

    bool OnArenaFloor(G3D::Vector3 const& p)
    {
        float const dx = p.x - ARENA.x, dy = p.y - ARENA.y;
        return std::sqrt(dx * dx + dy * dy) < ARENA_FLOOR_R && std::fabs(p.z - ARENA.z) < ARENA_FLOOR_DZ;
    }
}

TEST(BlackrockDepthsRouteProbe, SectionsThreeAndFourStayOutOfTheRingOfLaw)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 230 mmaps missing";

    for (std::vector<Pt> const* section : { &MANUFACTORY, &IMPERIAL_SEAT })
    {
        std::vector<Pt> const& legs = *section;
        std::printf("Blackrock Depths — %s legs (closest approach to the arena / Detention Block bosses)\n",
                    legs[0].name);
        for (std::size_t i = 0; i + 1 < legs.size(); ++i)
        {
            Pt const from = SnapOrKeep(mesh.get(), legs[i]);
            Pt const to = SnapOrKeep(mesh.get(), legs[i + 1]);
            DcNavHarness::RouteResult const r =
                DcNavHarness::Route(mesh.get(), BRD, from.x, from.y, from.z, to.x, to.y, to.z);
            std::printf("  %-16s -> %-16s reachable=%d complete=%d len2d=%.1f pts=%zu %s\n", from.name, to.name,
                        r.reachable, r.corridorComplete, r.routeLength2d, r.points.size(), r.failureReason.c_str());
            EXPECT_TRUE(r.reachable) << from.name << " -> " << to.name;

            std::vector<G3D::Vector3> const walk = Densify(r.points);
            float arenaMin = std::numeric_limits<float>::max();
            for (G3D::Vector3 const& p : walk)
            {
                arenaMin = std::min(arenaMin, Dist3(p, ARENA));
                EXPECT_FALSE(OnArenaFloor(p)) << from.name << " -> " << to.name << " crosses the Ring of Law at ("
                                              << p.x << ", " << p.y << ", " << p.z << ")";
            }
            std::printf("      arena %.1fyd", arenaMin);
            for (Pt const& boss : DETENTION_BLOCK)
            {
                float d = std::numeric_limits<float>::max();
                for (G3D::Vector3 const& p : walk)
                    d = std::min(d, Dist3(p, boss));
                if (d < 60.0f)
                    std::printf(" | %s %.1fyd", boss.name, d);
            }
            std::printf("\n");
        }
    }
}

// Sections 3 and 4's starts and event stand points (Data/Events/
// BlackrockDepthsEvents.cpp): each must sit ON the navmesh (snapped within a
// couple of yards, on the right floor) and be reachable from the one before it,
// in clear order. Doors are not in the navmesh, so this proves geometry only —
// the events own the doors.
TEST(BlackrockDepthsRouteProbe, SectionThreeAndFourSpotsAreOnTheNavmesh)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 230 mmaps missing";

    namespace B = DcBlackrockDepths;
    std::vector<Pt> const section3 = {
        { B::S3_START_X, B::S3_START_Y, B::S3_START_Z, "S3 start" },
        { 615.61f, -49.78f, -59.82f, "Shadowforge Lock" },
        { 652.4f, 21.4f, -60.0f, "Angerforge" },
        { 846.8f, 16.3f, -53.6f, "Argelmach" },
    };
    std::vector<Pt> const section4 = {
        { B::S4_START_X, B::S4_START_Y, B::S4_START_Z, "S4b" },
        { B::KEG_X, B::KEG_Y, B::KEG_Z, "Hurley's kegs" },
        { B::RIBBLY_X, B::RIBBLY_Y, B::RIBBLY_Z, "Ribbly" },
        { B::PHALANX_SPAWN_X, B::PHALANX_SPAWN_Y, B::PHALANX_SPAWN_Z, "Phalanx's corner" },
        { 868.97f, -224.98f, -43.72f, "Phalanx (door)" },
        { B::PLUGGER_CAMP_X, B::PLUGGER_CAMP_Y, B::PLUGGER_CAMP_Z, "Plugger camp" },
        { 1009.75f, -239.02f, -61.3f, "Flamelash" },
        { B::TOMB_HOLD_X, B::TOMB_HOLD_Y, B::TOMB_HOLD_Z, "Tomb garrison" },
        { 1281.14f, -282.15f, -78.14f, "Doom'rel" },
        { 1311.48f, -343.07f, -92.01f, "Lyceum door" },
        { B::LYCEUM_ENTRY_X, B::LYCEUM_ENTRY_Y, B::LYCEUM_ENTRY_Z, "Lyceum entry" },
        { B::BRAZIER_S_X, B::BRAZIER_S_Y, B::BRAZIER_S_Z, "S brazier stand" },
        { B::FLAME_KEEPER_POINTS[0].x, B::FLAME_KEEPER_POINTS[0].y, B::FLAME_KEEPER_POINTS[0].z, "keeper pt 0" },
        { B::FLAME_KEEPER_POINTS[1].x, B::FLAME_KEEPER_POINTS[1].y, B::FLAME_KEEPER_POINTS[1].z, "keeper pt 1" },
        { B::FLAME_KEEPER_POINTS[2].x, B::FLAME_KEEPER_POINTS[2].y, B::FLAME_KEEPER_POINTS[2].z, "keeper pt 2" },
        { B::FLAME_KEEPER_POINTS[3].x, B::FLAME_KEEPER_POINTS[3].y, B::FLAME_KEEPER_POINTS[3].z, "keeper pt 3" },
        { B::BRAZIER_N_X, B::BRAZIER_N_Y, B::BRAZIER_N_Z, "N brazier stand" },
        { 1389.55f, -555.85f, -92.01f, "Golem door N" },
        { B::MAGMUS_X, B::MAGMUS_Y, B::MAGMUS_Z, "Magmus" },
        { 1380.12f, -710.48f, -92.01f, "Throne door" },
        { 1380.18f, -831.65f, -87.59f, "Thaurissan" },
    };

    for (std::vector<Pt> const* section : { &section3, &section4 })
    {
        std::vector<Pt> const& spots = *section;
        std::printf("Blackrock Depths — %s event spots\n", spots[0].name);
        for (std::size_t i = 0; i < spots.size(); ++i)
        {
            Pt const& want = spots[i];
            G3D::Vector3 s;
            bool const snapped = DcNavHarness::NearestPoint(mesh.get(), want.x, want.y, want.z, 4.0f, 6.0f, s);
            float const off2d = snapped ? std::hypot(s.x - want.x, s.y - want.y) : -1.0f;
            std::printf("  %-18s (%.1f, %.1f, %.2f) -> snapped=%d (%.2f, %.2f, %.2f) off2d=%.2f dz=%.2f\n",
                        want.name, want.x, want.y, want.z, snapped, s.x, s.y, s.z, off2d,
                        snapped ? s.z - want.z : 0.0f);
            ASSERT_TRUE(snapped) << want.name << " is off the navmesh";
            EXPECT_LT(off2d, 2.0f) << want.name;
            EXPECT_LT(std::fabs(s.z - want.z), 2.5f) << want.name << " snapped to another floor";

            if (i == 0)
                continue;
            Pt const from = SnapOrKeep(mesh.get(), spots[i - 1]);
            DcNavHarness::RouteResult const r =
                DcNavHarness::Route(mesh.get(), BRD, from.x, from.y, from.z, s.x, s.y, s.z);
            std::printf("      from %-16s reachable=%d complete=%d len2d=%.1f %s\n", from.name, r.reachable,
                        r.corridorComplete, r.routeLength2d, r.failureReason.c_str());
            EXPECT_TRUE(r.reachable) << from.name << " -> " << want.name;
        }
    }
}

// The reworked dungeon's section 2 ("brd-s2", Shadowforge City): a Finder group
// lands at S2a, north of the Ring of Law, with the Ring NOT won — its gates are
// shut, and the navmesh does not carry doors. Every leg of the section's clear
// order must be reachable from S2a without crossing the arena floor (a route
// through it would hit a closed gate in game, or pull the party into the
// trial), and the two new stand points (S2a, the Vault's hold in front of the
// Dark Coffer) must sit on the floor. Each leg PRINTS whether it uses the
// spectator gallery that rings the arena (z -38 to -34). That is the expected
// way to Loregrain, and it is not a fight: for a group whose instance starts
// at S2a every gallery unit (spectators, citizens, the armed Anvilrage guards,
// medics, officers, footmen and the senators) stands down (user ruling
// 2026-11-17, the instance's flag), so the route walks through, not over them.
TEST(BlackrockDepthsRouteProbe, ShadowforgeCityRunsFromS2aOutsideTheRingOfLaw)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 230 mmaps missing";

    namespace B = DcBlackrockDepths;
    std::vector<Pt> const legs = {
        { B::S2_START_X, B::S2_START_Y, B::S2_START_Z, "S2a" },
        { 530.2f, -243.9f, -43.0f, "Loregrain" },
        { B::STILGISS_X, B::STILGISS_Y, B::STILGISS_Z, "Stilgiss" },
        { B::VAULT_HOLD_X, B::VAULT_HOLD_Y, B::VAULT_FLOOR_Z, "Vault hold" },
        { B::FINEOUS_X, B::FINEOUS_Y, B::FINEOUS_Z, "Fineous" },
        { 893.5f, -267.1f, -71.9f, "Incendius" },
        { 702.4f, 184.5f, -72.0f, "Bael'Gar" },
    };

    for (Pt const& spot : { legs[0], legs[3] })
    {
        G3D::Vector3 s;
        bool const snapped = DcNavHarness::NearestPoint(mesh.get(), spot.x, spot.y, spot.z, 4.0f, 6.0f, s);
        std::printf("  %-12s (%.1f, %.1f, %.2f) -> snapped=%d (%.2f, %.2f, %.2f)\n", spot.name, spot.x, spot.y,
                    spot.z, snapped, s.x, s.y, s.z);
        ASSERT_TRUE(snapped) << spot.name << " is off the navmesh";
        EXPECT_LT(std::hypot(s.x - spot.x, s.y - spot.y), 2.0f) << spot.name;
        EXPECT_LT(std::fabs(s.z - spot.z), 2.5f) << spot.name << " snapped to another floor";
    }

    std::printf("Blackrock Depths — section 2 legs from S2a\n");
    for (std::size_t i = 0; i + 1 < legs.size(); ++i)
    {
        Pt const from = SnapOrKeep(mesh.get(), legs[i]);
        Pt const to = SnapOrKeep(mesh.get(), legs[i + 1]);
        DcNavHarness::RouteResult const r =
            DcNavHarness::Route(mesh.get(), BRD, from.x, from.y, from.z, to.x, to.y, to.z);
        EXPECT_TRUE(r.reachable) << from.name << " -> " << to.name;

        bool gallery = false;
        float arenaMin = std::numeric_limits<float>::max();
        for (G3D::Vector3 const& p : Densify(r.points))
        {
            arenaMin = std::min(arenaMin, Dist3(p, ARENA));
            EXPECT_FALSE(OnArenaFloor(p)) << from.name << " -> " << to.name << " crosses the Ring of Law at ("
                                          << p.x << ", " << p.y << ", " << p.z << ")";
            float const dx = p.x - ARENA.x, dy = p.y - ARENA.y;
            gallery = gallery || (std::sqrt(dx * dx + dy * dy) < 50.0f && p.z > -42.0f && p.z < -30.0f);
        }
        std::printf("  %-10s -> %-10s reachable=%d len2d=%.1f arena %.1fyd gallery=%d %s\n", from.name, to.name,
                    r.reachable, r.routeLength2d, arenaMin, gallery, r.failureReason.c_str());
    }
}
