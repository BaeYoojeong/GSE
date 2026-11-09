#include "WorldSociety.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <tuple>
#include <vector>

namespace Ashen
{
    namespace
    {
        constexpr float CitizenSpacing = 32;

    } // namespace

    Vec2 WorldSociety::QuarryPosition(int slot, unsigned variant)
    {
        // Disjoint territories, away from the plaza. Respawns rotate within their own territory.
        const Vec2 centers[] = {{1980, 600}, {-1200, 570}, {400, -1200},
                                {400, 2190}, {2050, 2130}, {-1200, 2100}};
        const Vec2 offsets[] = {{-24, -24}, {24, 24}, {24, -24}, {-24, 24},
                                {0, -24},   {0, 24},  {-24, 0},  {24, 0}};
        const Vec2 center = centers[(std::clamp)(slot, 0, HuntingSlots - 1)];
        const Vec2 offset = offsets[variant % 8];
        return {center.x + offset.x, center.y + offset.y};
    }

    Vec2 WorldSociety::HuntingPosition(const CitizenRecord& npc, const HuntingGround& ground) const
    {
        unsigned rank = 0;
        for (const auto& pair : citizens)
        {
            const auto& other = pair.second;
            if (other.id < npc.id && other.village == npc.village && other.health > 0 &&
                other.intention == Activity::Hunt && other.quarrySlot == ground.slot)
            {
                ++rank;
            }
        }
        // Separate firing positions make ranged attacks visible and leave respawn clearance.
        const Vec2 center = InFrame(ground.village, ground.position, npc.village);
        return {(std::clamp)(center.x + (rank % 2 ? 68 : -68), -50000.0f, 50000.0f), center.y};
    }

    void WorldSociety::ResolveCrowding(Game& game, float dt)
    {
        // Use physical cells, so visitors and residents separate even across chunk borders.
        using Cell = std::tuple<std::int64_t, std::int64_t, int, int>;
        std::map<Cell, std::vector<CitizenRecord*>> cells;
        for (auto& pair : citizens)
        {
            auto& npc = pair.second;
            if (npc.health > 0 && game.chunks.count(Locate(npc.village, npc.position).chunk))
            {
                const Location location = Locate(npc.village, npc.position);
                cells[{location.chunk.x, location.chunk.y,
                       static_cast<int>(std::floor(location.local.x / CitizenSpacing)),
                       static_cast<int>(std::floor(location.local.y / CitizenSpacing))}]
                    .push_back(&npc);
            }
        }
        for (const auto& cell : cells)
        {
            for (auto* npc : cell.second)
            {
                for (int y = -1; y <= 1; ++y)
                {
                    for (int x = -1; x <= 1; ++x)
                    {
                        auto chunkX = std::get<0>(cell.first);
                        auto chunkY = std::get<1>(cell.first);
                        int cellX = std::get<2>(cell.first) + x;
                        int cellY = std::get<3>(cell.first) + y;
                        if (cellX < 0 || cellX >= 24)
                        {
                            chunkX += cellX < 0 ? -1 : 1;
                            cellX = (cellX + 24) % 24;
                        }
                        if (cellY < 0 || cellY >= 24)
                        {
                            chunkY += cellY < 0 ? -1 : 1;
                            cellY = (cellY + 24) % 24;
                        }
                        const auto adjacent = cells.find({chunkX, chunkY, cellX, cellY});
                        if (adjacent == cells.end())
                        {
                            continue;
                        }
                        for (auto* other : adjacent->second)
                        {
                            if (other->id <= npc->id)
                            {
                                continue;
                            }
                            const Vec2 otherPosition = InFrame(other->village, other->position, npc->village);
                            Vec2 away = {npc->position.x - otherPosition.x,
                                         npc->position.y - otherPosition.y};
                            float distance = std::sqrt(DistanceSquared(npc->position, otherPosition));
                            if (distance >= CitizenSpacing)
                            {
                                continue;
                            }
                            if (distance < .001f)
                            {
                                const float angle =
                                    static_cast<float>((npc->id * 7 + other->id * 13) % 32) * .19634954f;
                                away = {std::cos(angle), std::sin(angle)};
                            }
                            else
                            {
                                away = {away.x / distance, away.y / distance};
                            }
                            const float push = (std::min)((CitizenSpacing - distance) * .5f, dt * 40);
                            for (int side = 0; side < 2; ++side)
                            {
                                auto& person = side == 0 ? *npc : *other;
                                const float sign = side == 0 ? 1.0f : -1.0f;
                                const Vec2 delta = {away.x * push * sign, away.y * push * sign};
                                MoveCitizen(game, person, delta);
                            }
                        }
                    }
                }
            }
        }
    }

    bool WorldSociety::PlaceQuarry(Game& game, HuntingGround& ground, bool respawn)
    {
        const Location player = game.Absolute(game.hero->position);
        std::vector<Actor*> actors;
        bool queriedActors = false;
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            const unsigned variant = (ground.spawnVariant + attempt + (respawn ? 1 : 0)) % 8;
            const Vec2 point = QuarryPosition(ground.slot, variant);
            const Location location = Locate(ground.village, point);
            const bool loaded = game.chunks.count(location.chunk) != 0;
            bool free = game.Dead() || DistanceSquared(point, InFrame(player.chunk, player.local,
                                                                      ground.village)) >= 90 * 90;
            for (const auto& pair : citizens)
            {
                const auto& npc = pair.second;
                if (npc.health > 0 &&
                    DistanceSquared(point, InFrame(npc.village, npc.position, ground.village)) < 56 * 56)
                {
                    free = false;
                    break;
                }
            }
            // Also reserve legacy spawn sites before BuildChunk has created their actors.
            for (int slot = 0; slot < 2 && free; ++slot)
            {
                free = !game.EnemySlotEnabled(location.chunk, slot) ||
                       DistanceSquared(location.local, game.EnemyPosition(location.chunk, slot)) >= 64 * 64;
            }
            for (const auto& pair : hunting)
            {
                const auto& other = pair.second;
                if (&other != &ground && other.health > 0 && !other.placementPending &&
                    DistanceSquared(point, InFrame(other.village, other.position, ground.village)) < 64 * 64)
                {
                    free = false;
                    break;
                }
            }
            if (free && loaded)
            {
                free = game.Walkable(game.Relative(location), 18);
                if (!queriedActors)
                {
                    actors = game.scene.Snapshot();
                    queriedActors = true;
                }
                for (Actor* base : actors)
                {
                    const auto* actor = dynamic_cast<const WorldActor*>(base);
                    if (actor && actor->kind == Kind::Enemy && actor->health > 0 &&
                        DistanceSquared(point, InFrame(actor->chunk, actor->position, ground.village)) <
                            64 * 64)
                    {
                        free = false;
                        break;
                    }
                }
            }
            if (!free)
            {
                continue;
            }
            ground.spawnVariant = variant;
            ground.position = point;
            if (respawn)
            {
                ground.health = HuntingGround::MaximumHealth;
                ground.respawnSeconds = 0;
                ground.attackTimer = 1;
            }
            ground.placementPending = false;
            ground.spawnDeferred = false;
            std::ostringstream detail;
            detail << "sim_s=" << simulationSeconds << " chunk=" << ground.village.x << ','
                   << ground.village.y << " slot=" << ground.slot << " x=" << point.x << " y=" << point.y
                   << " physical_chunk=" << location.chunk.x << ',' << location.chunk.y
                   << " variant=" << variant;
            RenderProfiler::Event(respawn ? "quarry_respawn" : "quarry_spawn_position", detail.str());
            return true;
        }
        if (!ground.spawnDeferred)
        {
            RenderProfiler::Event("quarry_respawn_deferred", "chunk=" + std::to_string(ground.village.x) +
                                                                 "," + std::to_string(ground.village.y) +
                                                                 " slot=" + std::to_string(ground.slot) +
                                                                 " reason=occupied");
            ground.spawnDeferred = true;
        }
        return false;
    }

    void WorldSociety::ReportSpacing(Game& game) const
    {
        size_t quarryAlive = 0, pending = 0, citizenActors = 0, quarryActors = 0, enemyActors = 0,
               duplicates = 0;
        std::map<std::tuple<int, std::int64_t, std::int64_t, std::uint64_t>, unsigned> identities;
        std::map<std::tuple<std::int64_t, std::int64_t, int, int>, unsigned> density;
        unsigned peakCell = 0;
        for (const auto& pair : citizens)
        {
            const auto& npc = pair.second;
            const Location location = Locate(npc.village, npc.position);
            auto& count =
                density[{location.chunk.x, location.chunk.y, static_cast<int>(location.local.x / 64),
                         static_cast<int>(location.local.y / 64)}];
            peakCell = (std::max)(peakCell, ++count);
        }
        for (const auto& pair : hunting)
        {
            quarryAlive += pair.second.health > 0 && !pair.second.placementPending ? 1 : 0;
            pending += pair.second.spawnDeferred || pair.second.placementPending ? 1 : 0;
        }
        for (Actor* base : game.scene.Snapshot())
        {
            const auto* actor = dynamic_cast<const WorldActor*>(base);
            if (!actor ||
                (actor->kind != Kind::Citizen && actor->kind != Kind::Quarry && actor->kind != Kind::Enemy))
            {
                continue;
            }
            citizenActors += actor->kind == Kind::Citizen ? 1 : 0;
            quarryActors += actor->kind == Kind::Quarry ? 1 : 0;
            enemyActors += actor->kind == Kind::Enemy ? 1 : 0;
            const auto identity =
                actor->kind == Kind::Citizen ? actor->memory : static_cast<std::uint64_t>(actor->slot);
            duplicates +=
                ++identities[{static_cast<int>(actor->kind), actor->chunk.x, actor->chunk.y, identity}] > 1
                    ? 1
                    : 0;
        }
        unsigned enemySlotLimit = 0;
        for (const auto& chunk : game.chunks)
        {
            for (int slot = 0; slot < 2; ++slot)
            {
                enemySlotLimit += game.EnemySlotEnabled(chunk.first, slot) ? 1 : 0;
            }
        }
        std::ostringstream detail;
        detail << "residents=" << citizens.size() << " npc_peak_64u_cell=" << peakCell
               << " quarry_alive=" << quarryAlive << " quarry_slots=" << hunting.size()
               << " respawn_deferred=" << pending << " citizen_actors=" << citizenActors
               << " quarry_actors=" << quarryActors << " enemy_actors=" << enemyActors
               << " enemy_slot_limit=" << enemySlotLimit << " duplicate_actors=" << duplicates;
        RenderProfiler::Event("population_spacing", detail.str());
        std::printf("[population_spacing] %s\n", detail.str().c_str());
    }
} // namespace Ashen
