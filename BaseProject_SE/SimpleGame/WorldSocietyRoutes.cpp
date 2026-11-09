#include "WorldSociety.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace Ashen
{
    namespace
    {
        constexpr float RoamingMinimum = -50000;
        constexpr float RoamingMaximum = 50000;

        bool Reachable(Vec2 point)
        {
            return point.x >= RoamingMinimum && point.x <= RoamingMaximum && point.y >= RoamingMinimum &&
                   point.y <= RoamingMaximum;
        }

        bool ReservedRoad(Vec2 point)
        {
            return (point.x >= 328 && point.x <= 440) || (point.y >= 330 && point.y <= 450) || point.y >= 530;
        }

        unsigned NearestMarket(Vec2 point)
        {
            return DistanceSquared(point, WorldSociety::MarketPosition(0)) <=
                           DistanceSquared(point, WorldSociety::MarketPosition(1))
                       ? 0
                       : 1;
        }
    } // namespace

    Location WorldSociety::Locate(ChunkKey home, Vec2 point)
    {
        const auto dx = static_cast<std::int64_t>(std::floor(point.x / ChunkSize));
        const auto dy = static_cast<std::int64_t>(std::floor(point.y / ChunkSize));
        return {{home.x + dx, home.y + dy},
                {point.x - static_cast<float>(dx) * ChunkSize, point.y - static_cast<float>(dy) * ChunkSize}};
    }

    Vec2 WorldSociety::InFrame(ChunkKey from, Vec2 point, ChunkKey to)
    {
        return {static_cast<float>(from.x - to.x) * ChunkSize + point.x,
                static_cast<float>(from.y - to.y) * ChunkSize + point.y};
    }

    Vec2 WorldSociety::MarketPosition(unsigned site)
    {
        return site % 2 == 0 ? Vec2{-400, 404} : Vec2{1156, 660};
    }

    bool WorldSociety::IsVillage(ChunkKey chunk) const
    {
        return TownWorld::At(chunk) >= 0;
    }

    bool WorldSociety::IsAtMarket(const CitizenRecord& npc) const
    {
        const Location current = Locate(npc.village, npc.position);
        const int town = TownWorld::At(current.chunk);
        if (town < 0)
        {
            return false;
        }
        const Vec2 point = InFrame(npc.village, npc.position, TownWorld::Towns()[town].center);
        return DistanceSquared(point, MarketPosition(NearestMarket(point))) <= 80 * 80;
    }

    Vec2 WorldSociety::ActivityPosition(const CitizenRecord& npc, Activity activity) const
    {
        if (activity == Activity::Rest)
        {
            return TownWorld::Door(npc.houseLot);
        }
        std::set<ChunkKey> villages = {npc.village};
        for (const auto& pair : citizens)
        {
            const ChunkKey key = pair.second.village;
            if (key.x >= npc.village.x - 1 && key.x <= npc.village.x + 1 && key.y >= npc.village.y - 1 &&
                key.y <= npc.village.y + 1)
            {
                villages.insert(key);
            }
        }
        unsigned merchantRank = 0;
        for (const auto& pair : citizens)
        {
            if (pair.first < npc.id && pair.second.village == npc.village &&
                pair.second.profession == Profession::Merchant)
            {
                ++merchantRank;
            }
        }

        if (activity == Activity::Raid)
        {
            // Search the roads outside the home village when no profitable person is in sight.
            const Vec2 patrol[] = {{-1300, 400}, {2080, 600}, {384, -1300}, {384, 2100}};
            return patrol[(npc.id + npc.journey) % 4];
        }

        if (activity == Activity::Trade)
        {
            Vec2 market = MarketPosition(merchantRank % 2);
            if (npc.profession == Profession::Merchant && IsMarketKeeper(npc))
            {
                // Keepers occupy different shops; extra keepers retain their own counter space.
                market.x += static_cast<float>((merchantRank / 2) % 3) * 24 - 24;
                return market;
            }
            if (npc.profession == Profession::Merchant)
            {
                std::vector<ChunkKey> destinations;
                for (ChunkKey village : villages)
                {
                    if (!(village == npc.village))
                    {
                        destinations.push_back(village);
                    }
                }
                if (!destinations.empty())
                {
                    const ChunkKey destination = destinations[(npc.id + npc.journey) % destinations.size()];
                    market = InFrame(destination, MarketPosition((npc.id + npc.journey) % 2), npc.village);
                }
                else
                {
                    market = MarketPosition((merchantRank + npc.journey + 1) % 2);
                }
            }
            else
            {
                float bestCost = 1e20f;
                for (const auto& pair : citizens)
                {
                    const auto& merchant = pair.second;
                    if (!(merchant.village == npc.village) || merchant.journeyKind != JourneyKind::None ||
                        merchant.profession != Profession::Merchant || merchant.health <= 0 ||
                        merchant.hostileSeconds > 0 || merchant.activity == Activity::Raid)
                    {
                        continue;
                    }
                    const bool selling = npc.goods > 1 && merchant.goods < 16 && merchant.coins >= 2 &&
                                         npc.coins <= 1000000 - 2;
                    const bool buying = npc.goods < 2 && merchant.goods > 0 && npc.coins >= 3 &&
                                        merchant.coins <= 1000000 - 3;
                    if (!selling && !buying)
                    {
                        continue;
                    }
                    Location destination = Locate(merchant.village, merchant.position);
                    if (!IsAtMarket(merchant))
                    {
                        destination = merchant.goalActivity == Activity::Trade
                                          ? Locate(merchant.village, merchant.activityGoal)
                                          : Location{merchant.village, MarketPosition(merchant.id % 2)};
                    }
                    if (!IsVillage(destination.chunk))
                    {
                        continue;
                    }
                    const int town = TownWorld::At(destination.chunk);
                    const ChunkKey center = TownWorld::Towns()[town].center;
                    const Vec2 point = InFrame(
                        center,
                        MarketPosition(NearestMarket(InFrame(destination.chunk, destination.local, center))),
                        npc.village);
                    if (!Reachable(point))
                    {
                        continue;
                    }
                    const float cost =
                        std::sqrt(DistanceSquared(npc.position, point)) + (IsAtMarket(merchant) ? 0 : 180);
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        market = point;
                    }
                }
            }
            // A small deterministic customer ring preserves trading reach without a shared center.
            const float angle = static_cast<float>((npc.id * 7 + npc.journey * 3) % 16) * .39269908f;
            return {market.x + std::cos(angle) * 38, market.y + std::sin(angle) * 38};
        }

        static constexpr Vec2 inns[] = {{116, 438}, {248, 424}, {476, 438},
                                        {668, 390}, {344, 602}, {440, 692}};
        static constexpr Vec2 study[] = {{552, 348}, {664, 438}, {392, 140},
                                         {116, 640}, {400, 712}, {676, 620}};
        Vec2 best = {};
        float bestCost = 1e20f;
        std::vector<ChunkKey> destinations(villages.begin(), villages.end());
        for (unsigned candidate = 0; candidate < 6; ++candidate)
        {
            const unsigned index = static_cast<unsigned>((npc.id + npc.journey * 5 + candidate) % 6);
            ChunkKey destination = npc.village;
            Vec2 point = inns[index];
            if (activity == Activity::Study)
            {
                // Alternate wilderness landmarks with visits to known settlements.
                const ChunkKey outward[] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
                if ((npc.journey + 1) % 3 != 0)
                {
                    for (unsigned direction = 0; direction < 4; ++direction)
                    {
                        const ChunkKey offset = outward[(npc.id + npc.journey / 3 + direction) % 4];
                        destination = {npc.village.x + offset.x, npc.village.y + offset.y};
                        if (!IsVillage(destination))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    destination = destinations[(npc.id + npc.journey) % destinations.size()];
                }
                point = study[index];
            }
            point = InFrame(destination, point, npc.village);
            float cost = candidate * 8.0f;
            for (const auto& pair : citizens)
            {
                const auto& other = pair.second;
                if (other.id == npc.id || other.health <= 0)
                {
                    continue;
                }
                if (DistanceSquared(point, InFrame(other.village, other.position, npc.village)) < 44 * 44)
                {
                    cost += 80;
                }
                if (other.goalActivity == activity &&
                    DistanceSquared(point, InFrame(other.village, other.activityGoal, npc.village)) < 36 * 36)
                {
                    cost += 120;
                }
            }
            if (cost < bestCost)
            {
                bestCost = cost;
                best = point;
            }
        }
        return best;
    }

    void WorldSociety::SetDestination(CitizenRecord& npc, Vec2 goal, Activity activity)
    {
        ++npc.journey;
        npc.activityGoal = goal;
        npc.goalActivity = activity;
        npc.goalDwellSeconds = 6 + static_cast<float>(npc.id % 5);
        npc.travelCommitSeconds =
            (std::clamp)(std::sqrt(DistanceSquared(npc.position, goal)) / 82 + 10, 10.0f, 45.0f);
        std::ostringstream route;
        route << "npc=" << npc.id << " home=" << npc.village.x << ',' << npc.village.y
              << " from=" << npc.position.x << ',' << npc.position.y << " to=" << goal.x << ',' << goal.y
              << " activity=" << static_cast<int>(activity) << " journey=" << npc.journey
              << " commitment_s=" << npc.travelCommitSeconds;
        RenderProfiler::Event("society_destination", route.str());
    }

    void WorldSociety::MoveCitizen(Game& game, CitizenRecord& npc, Vec2 delta)
    {
        auto canMove = [&](Vec2 point)
        {
            if (!Reachable(point))
            {
                return false;
            }
            const Location location = Locate(npc.village, point);
            if (game.chunks.count(location.chunk))
            {
                return game.Walkable(game.Relative(location), 14);
            }
            for (const auto& pair : citizens)
            {
                const auto& resident = pair.second;
                const Vec2 house =
                    InFrame(resident.village, TownWorld::House(resident.houseLot), npc.village);
                if (DistanceSquared(point, house) < 46 * 46)
                {
                    return false;
                }
            }
            for (const auto& town : TownWorld::Towns())
            {
                if (DistanceSquared(point, InFrame(town.center, {552, 228}, npc.village)) < 74 * 74)
                {
                    return false;
                }
            }
            return true;
        };
        if (canMove({npc.position.x + delta.x, npc.position.y}))
        {
            npc.position.x += delta.x;
        }
        if (canMove({npc.position.x, npc.position.y + delta.y}))
        {
            npc.position.y += delta.y;
        }
    }

    bool WorldSociety::Travel(Game& game, CitizenRecord& npc, Vec2 goal, float dt)
    {
        if (DistanceSquared(npc.position, goal) <= 10 * 10)
        {
            return true;
        }
        const Location here = Locate(npc.village, npc.position);
        const Location there = Locate(npc.village, goal);
        const float laneX = 342 + static_cast<float>((npc.id + npc.journey) % 4) * 28;
        const float laneY = 342 + static_cast<float>((npc.id * 3 + npc.journey) % 4) * 28;
        Vec2 localGoal = InFrame(npc.village, goal, here.chunk);
        Vec2 waypoint = goal;
        bool direct = here.chunk == there.chunk;
        const bool loaded = game.chunks.count(here.chunk) != 0;
        const float distance = std::sqrt(DistanceSquared(npc.position, goal));
        const unsigned steps = static_cast<unsigned>(std::ceil(distance / 24));
        for (unsigned i = 1; direct && i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / steps;
            const Vec2 point = {here.local.x + (localGoal.x - here.local.x) * t,
                                here.local.y + (localGoal.y - here.local.y) * t};
            // Loaded geometry permits safe shortcuts. Unloaded travel stays on reserved roads,
            // except for the short final approach to a person displaced by crowd separation.
            direct = loaded ? game.Walkable(game.Relative({here.chunk, point}), 14)
                            : ReservedRoad(point) ||
                                  (TownWorld::At(here.chunk) >= 0 && std::fabs(point.y - localGoal.y) < 3) ||
                                  DistanceSquared(point, localGoal) <= 96 * 96;
        }
        if (!loaded && here.chunk == there.chunk)
        {
            // The coarse simulation follows the same empty central lanes. Approach houses along
            // their doorway row instead of repeatedly switching between the road and the goal.
            bool housing = TownWorld::At(here.chunk) >= 0;
            if (!housing)
            {
                for (const auto& pair : citizens)
                {
                    housing |= Locate(pair.second.village, TownWorld::House(pair.second.houseLot)).chunk ==
                               here.chunk;
                }
            }
            if (housing)
            {
                Vec2 bend = localGoal;
                if (std::fabs(here.local.y - localGoal.y) > 3)
                {
                    bend = std::fabs(here.local.x - laneX) > 3 ? Vec2{laneX, here.local.y}
                                                               : Vec2{laneX, localGoal.y};
                }
                waypoint = InFrame(here.chunk, bend, npc.village);
                direct = true;
            }
        }
        bool detour = false;
        if (!direct && loaded && here.chunk == there.chunk)
        {
            auto clearSegment = [&](Vec2 from, Vec2 to)
            {
                const unsigned samples = static_cast<unsigned>(
                    (std::max)(1.0f, std::ceil(std::sqrt(DistanceSquared(from, to)) / 12)));
                for (unsigned sample = 1; sample <= samples; ++sample)
                {
                    const float t = static_cast<float>(sample) / samples;
                    const Vec2 point = {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
                    if (!game.Walkable(game.Relative({here.chunk, point}), 14))
                    {
                        return false;
                    }
                }
                return true;
            };
            // A few visible road bends handle legacy obstacles and goals behind a house.
            // Requiring both segments to be clear avoids returning to the same blocked intersection.
            const Vec2 bends[] = {{laneX, localGoal.y},
                                  {here.local.x, localGoal.y},
                                  {localGoal.x, laneY},
                                  {laneX, 110},
                                  {laneX, laneY},
                                  {laneX, 550},
                                  {laneX, 712},
                                  {112, laneY},
                                  {660, laneY},
                                  {112, 550},
                                  {660, 550}};
            float bestDistance = 1e20f;
            for (Vec2 bend : bends)
            {
                if (DistanceSquared(here.local, bend) < 3 * 3)
                {
                    continue;
                }
                const float routeDistance = std::sqrt(DistanceSquared(here.local, bend)) +
                                            std::sqrt(DistanceSquared(bend, localGoal));
                if (routeDistance < bestDistance && clearSegment(here.local, bend) &&
                    clearSegment(bend, localGoal))
                {
                    bestDistance = routeDistance;
                    waypoint = InFrame(here.chunk, bend, npc.village);
                    detour = true;
                }
            }
        }
        if (!direct && !detour)
        {
            Vec2 localWaypoint = localGoal;
            if (here.chunk.y != there.chunk.y)
            {
                // North/south borders are crossed on the continuous vertical road.
                if (std::fabs(here.local.x - laneX) > 3)
                {
                    localWaypoint = {laneX, here.local.y};
                    if (here.local.y < 330 || (here.local.y > 450 && here.local.y < 530))
                    {
                        localWaypoint = {here.local.x, laneY};
                    }
                }
                else
                {
                    localWaypoint = {laneX, localGoal.y};
                }
            }
            else if (here.chunk.x != there.chunk.x)
            {
                // The southern field also connects horizontally across neighboring chunks.
                localWaypoint = {localGoal.x, here.local.y};
                if (here.local.y < 330 || (here.local.y > 450 && here.local.y < 530))
                {
                    localWaypoint = {here.local.x, laneY};
                }
            }
            else if (here.local.y >= 530 && localGoal.y < 530)
            {
                localWaypoint = std::fabs(here.local.x - laneX) > 3 ? Vec2{laneX, here.local.y}
                                                                    : Vec2{laneX, localGoal.y};
            }
            else if (here.local.y <= 450 && localGoal.y > 450)
            {
                localWaypoint = std::fabs(here.local.x - laneX) > 3 ? Vec2{laneX, here.local.y}
                                                                    : Vec2{laneX, localGoal.y};
            }
            else
            {
                localWaypoint = {here.local.x, laneY};
                if (std::fabs(here.local.y - laneY) <= 3)
                {
                    localWaypoint = {laneX, laneY};
                }
            }
            waypoint = InFrame(here.chunk, localWaypoint, npc.village);
        }
        if (here.chunk == there.chunk && DistanceSquared(npc.position, waypoint) < 3 * 3)
        {
            waypoint = goal;
        }
        const float remaining = std::sqrt(DistanceSquared(npc.position, waypoint));
        if (remaining > .001f)
        {
            const float step = (std::min)(remaining, dt * (npc.activity == Activity::Raid ? 96 : 82));
            const Vec2 before = npc.position;
            const Vec2 forward = {(waypoint.x - before.x) / remaining, (waypoint.y - before.y) / remaining};
            MoveCitizen(game, npc, {forward.x * step, forward.y * step});
            if (remaining > 10 && DistanceSquared(before, npc.position) < .5f * .5f)
            {
                // Keep the same preferred side for the whole journey instead of alternating
                // left/right each frame. Every recovery step uses normal collision and bounds.
                const float side = (npc.id + npc.journey) % 2 ? 1.0f : -1.0f;
                const Vec2 tangent = {-forward.y * side, forward.x * side};
                const Vec2 alternatives[] = {
                    tangent,
                    {-tangent.x, -tangent.y},
                    {(tangent.x - forward.x) * .70710678f, (tangent.y - forward.y) * .70710678f},
                    {(-tangent.x - forward.x) * .70710678f, (-tangent.y - forward.y) * .70710678f}};
                for (Vec2 direction : alternatives)
                {
                    const float used = std::sqrt(DistanceSquared(before, npc.position));
                    const float allowance = (std::max)(0.0f, step - used);
                    MoveCitizen(game, npc, {direction.x * allowance, direction.y * allowance});
                    if (DistanceSquared(before, npc.position) >= .5f * .5f)
                    {
                        break;
                    }
                }
            }
        }
        return DistanceSquared(npc.position, goal) <= 10 * 10;
    }
} // namespace Ashen
