#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include "WorldActors.h"
#include "ChapterActors.h"
#include "WorldLayout.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>

namespace Ashen
{
    float DistanceSquared(Vec2 a, Vec2 b)
    {
        return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
    }

    Vec2 ScreenToGround(Vec2 direction)
    {
        return {CameraCos * direction.x + CameraSin * direction.y / CameraDepth,
                -CameraSin * direction.x + CameraCos * direction.y / CameraDepth};
    }

    std::uint64_t Game::Hash(ChunkKey key, unsigned salt) const
    {
        std::uint64_t value = static_cast<std::uint64_t>(key.x) * 0x9e3779b97f4a7c15ULL ^
                              static_cast<std::uint64_t>(key.y) * 0xbf58476d1ce4e5b9ULL ^ seed ^ salt;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    bool Game::EnemySlotEnabled(ChunkKey key, int slot) const
    {
        if (slot < 0 || slot >= 2)
        {
            return false;
        }

        // Both legacy slot identities remain bounded and save-compatible.
        return !society.IsVillage(key);
    }

    unsigned Game::Biome(ChunkKey key) const
    {
        return WorldLayout::Biome(*this, key);
    }

    const Region* Game::RegionAt(ChunkKey key) const
    {
        auto it = regions.find(key);
        if (it != regions.end())
        {
            return &it->second;
        }
        if (!regionDatabase.IsOpen())
        {
            return nullptr;
        }
        Region loaded;
        bool found = false;
        if (!regionDatabase.ReadRegion(key, loaded, found, society.simulationSeconds) ||
            (found && (loaded.lastGiftLife > life.id || loaded.lastTalkLife > life.id)))
        {
            regionReadFailed = true;
            const_cast<Game*>(this)->storageHealthy = false;
            RenderProfiler::Event("region_database_read_failed", regionDatabase.Error());
            return nullptr;
        }
        return &regions.emplace(key, loaded).first->second;
    }

    bool Game::Lit(ChunkKey key) const
    {
        const Region* region = RegionAt(key);
        return region && region->lit;
    }

    const Memory* Game::MemoryAt(std::uint64_t id) const
    {
        auto it = memories.find(id);
        return it == memories.end() ? nullptr : &it->second;
    }

    Vec2 Game::Relative(const Location& location) const
    {
        return {static_cast<float>(location.chunk.x - origin.x) * ChunkSize + location.local.x,
                static_cast<float>(location.chunk.y - origin.y) * ChunkSize + location.local.y};
    }

    Location Game::Absolute(Vec2 point) const
    {
        auto dx = static_cast<std::int64_t>(std::floor(point.x / ChunkSize));
        auto dy = static_cast<std::int64_t>(std::floor(point.y / ChunkSize));
        return {{origin.x + dx, origin.y + dy},
                {point.x - static_cast<float>(dx) * ChunkSize, point.y - static_cast<float>(dy) * ChunkSize}};
    }

    WorldActor& Game::Spawn(Kind kind, ChunkKey chunk, ActorId parent, Vec2 position, int slot)
    {
        // A monster slot owns one actor. Streaming and a respawn request must never stack proxies.
        if (kind == Kind::Enemy || kind == Kind::Quarry)
        {
            for (Actor* base : scene.Snapshot())
            {
                auto* existing = dynamic_cast<WorldActor*>(base);

                if (existing && existing->kind == kind && existing->chunk == chunk && existing->slot == slot)
                {
                    RenderProfiler::Event("monster_duplicate_prevented",
                                          "kind=" + std::to_string(static_cast<unsigned>(kind)) +
                                              " chunk=" + std::to_string(chunk.x) + "," +
                                              std::to_string(chunk.y) + " slot=" + std::to_string(slot));
                    return *existing;
                }
            }
        }

        WorldActor* created = nullptr;

        // Select a concrete behavior once, at creation. SceneGraph dispatches updates virtually.
        switch (kind)
        {
        case Kind::Residence:
            created = &scene.Create<ResidenceActor>(parent, *this);
            break;
        case Kind::BellAltar:
            created = &scene.Create<BellAltarActor>(parent, *this);
            break;
        case Kind::BellKeeper:
            created = &scene.Create<BellKeeperActor>(parent, *this);
            break;
        case Kind::Citizen:
            created = &scene.Create<CitizenActor>(parent, *this);
            break;
        case Kind::Quarry:
            created = &scene.Create<QuarryActor>(parent, *this);
            break;
        case Kind::Player:
            created = &scene.Create<PlayerActor>(parent, *this);
            break;

        case Kind::Enemy:
            created = &scene.Create<EnemyActor>(parent, *this);
            break;

        case Kind::Heir:
            created = &scene.Create<HeirActor>(parent, *this);
            break;

        case Kind::Projectile:
            created = &scene.Create<ProjectileActor>(parent, *this);
            break;

        case Kind::Drop:
            created = &scene.Create<DropActor>(parent, *this);
            break;

        case Kind::Herb:
            created = &scene.Create<HerbActor>(parent, *this);
            break;

        case Kind::Fire:
            created = &scene.Create<FireActor>(parent, *this);
            break;

        case Kind::Settler:
            created = &scene.Create<SettlerActor>(parent, *this);
            break;

        case Kind::Exploration:
            created = &scene.Create<ExplorationActor>(parent, *this);
            break;

        case Kind::Waypoint:
            created = &scene.Create<WaypointActor>(parent, *this);
            break;

        case Kind::MarketStall:
            created = &scene.Create<MarketStallActor>(parent, *this);
            break;

        case Kind::Grave:
            created = &scene.Create<GraveActor>(parent, *this);
            break;

        default:
            created = &scene.Create<WorldActor>(parent, *this, kind);
            break;
        }

        auto& actor = *created;
        actor.position = actor.home = position;
        actor.chunk = chunk;
        actor.slot = slot;
        actor.variant = static_cast<unsigned>(Hash(chunk, slot + static_cast<unsigned>(kind) * 97) % 4);
        actor.radius = kind == Kind::Ground ? 1100.0f : 160.0f;
        actor.layer = kind == Kind::Ground ? 0 : 1;
        return actor;
    }

    void Game::SpawnSettlement(ChunkKey key, ActorId root)
    {
        const int index = TownWorld::At(key);
        if (index < 0)
        {
            return;
        }
        const ChunkKey center = TownWorld::Towns()[index].center;
        for (int site = 0; site < 2; ++site)
        {
            const Location place = WorldSociety::Locate(center, WorldSociety::MarketPosition(site));
            if (!(place.chunk == key))
            {
                continue;
            }
            bool exists = false;
            for (Actor* base : scene.Snapshot())
            {
                const auto* actor = dynamic_cast<const WorldActor*>(base);
                exists |=
                    actor && actor->kind == Kind::MarketStall && actor->chunk == key && actor->slot == site;
            }
            if (!exists)
            {
                Spawn(Kind::MarketStall, key, root, {place.local.x, place.local.y - 38}, site);
            }
        }
        if (!(key == center))
        {
            return;
        }
        const unsigned town = static_cast<unsigned>(index);
        bool hasWaypoint = false;
        for (Actor* base : scene.Snapshot())
        {
            const auto* actor = dynamic_cast<const WorldActor*>(base);
            hasWaypoint |= actor && actor->kind == Kind::Waypoint && actor->chunk == key;
        }
        if (!hasWaypoint)
        {
            auto& waypoint = Spawn(Kind::Waypoint, key, root, Waypoints::Position(town).local, index);
            waypoint.variant = town;
        }
        for (Actor* base : scene.Snapshot())
        {
            const auto* actor = dynamic_cast<const WorldActor*>(base);
            if (actor && actor->kind == Kind::Chapel && actor->chunk == key)
            {
                return;
            }
        }
        auto& chapel = Spawn(Kind::Chapel, key, root, {552, 228});
        chapel.solid = true;
        chapel.radius = 60;
        solids.push_back(chapel.Id());
    }

    void Game::BuildChunk(ChunkKey key, ActorId root)
    {
        const Region* stored = RegionAt(key);
        Spawn(Kind::Ground, key, root, {});
        // Remembered fires remain usable after the wilderness generator changes.
        if (WorldLayout::Camp(*this, key) || (stored && stored->lit) || key == checkpoint.chunk)
        {
            Spawn(Kind::Fire, key, root, {384, 384});
        }
        const auto site = WorldLayout::Feature(*this, key);
        if (site != WorldLayout::Site::None)
        {
            auto& landmark = Spawn(Kind::Exploration, key, root,
                                   site == WorldLayout::Site::Camp ? Vec2{440, 440} : Vec2{384, 574});
            landmark.variant = static_cast<unsigned>(site);
        }
        RenderProfiler::Event("world_chunk_generated",
                              "x=" + std::to_string(key.x) + " y=" + std::to_string(key.y) +
                                  " biome=" + std::to_string(Biome(key)) +
                                  " site=" + std::to_string(static_cast<unsigned>(site)));
        SpawnSettlement(key, root);
        society.SpawnVillage(*this, key, root);
        chapter.Spawn(*this, key, root);

        for (int i = 0; i < 18 && !society.IsVillage(key); ++i)
        {
            auto h = Hash(key, 800 + i);
            Vec2 position = {64.0f + static_cast<float>(h % 640),
                             70.0f + static_cast<float>((h >> 16) % 630)};
            if (key == ChapterTwo::Altar().chunk &&
                DistanceSquared(position, ChapterTwo::Altar().local) < 210 * 210)
            {
                continue;
            }
            // A continuous cross of roads and a wide settlement clearing cannot be obstructed.
            if (std::fabs(position.x - 384) < 90 || std::fabs(position.y - 384) < 100 ||
                DistanceSquared(position, {220, 260}) < 115 * 115 ||
                DistanceSquared(position, {552, 228}) < 125 * 125 || position.y > 490)
            {
                continue;
            }

            // Expanded collision circles stay disjoint, leaving a player-width route
            // around every obstacle. Roads, chunk borders and the southern field stay open.
            bool separated = true;
            const float radius = i % 4 == 0 ? 15.0f : 10.0f;

            for (Actor* base : scene.Snapshot())
            {
                auto* obstacle = dynamic_cast<WorldActor*>(base);

                if (!obstacle || !obstacle->solid || obstacle->Parent() != root)
                {
                    continue;
                }

                const float clearance = radius + obstacle->radius + 40;

                if (DistanceSquared(position, obstacle->position) < clearance * clearance)
                {
                    separated = false;
                    break;
                }
            }

            // Newly assigned homes reserve their footprint even in a legacy region.
            if (!separated)
            {
                continue;
            }

            auto& object = Spawn(i % 4 == 0 ? Kind::Rubble : Kind::Tree, key, root, position, i);
            object.solid = true;
            object.radius = i % 4 == 0 ? 15.0f : 10.0f;
        }

        const Region* state = RegionAt(key);

        for (int i = 0; i < 6; ++i)
        {
            if (!state || !(state->harvested & (1u << i)))
            {
                Spawn(Kind::Herb, key, root, FarmingPosition(key, i), i);
            }
        }

        for (int i = 0; i < 2; ++i)
        {
            if (EnemySlotEnabled(key, i) && (!state || !(state->defeated & (1u << i))))
            {
                auto& enemy = Spawn(Kind::Enemy, key, root, EnemyPosition(key, i), i);
                enemy.health = EnemyMaximumHealth;
                enemy.cooldown = .8f + i * .4f;
            }
        }

        for (int source = 0; source < 8; ++source)
        {
            SpawnLoot(key, source);
        }

        for (const auto& pair : memories)
        {
            if (pair.second.home.chunk == key)
            {
                const auto& memory = pair.second;
                auto& grave =
                    Spawn(Kind::Grave, key, root, {memory.home.local.x - 28, memory.home.local.y + 16});
                grave.memory = memory.life.id;
            }
        }
    }

    void Game::SpawnMemory(const Memory& memory)
    {
        auto parent = chunks.find(memory.home.chunk);

        if (parent == chunks.end())
        {
            return;
        }

        SpawnSettlement(memory.home.chunk, parent->second);
        society.SpawnVillage(*this, memory.home.chunk, parent->second);

        for (Actor* base : scene.Snapshot())
        {
            const auto* existing = dynamic_cast<const WorldActor*>(base);

            if (existing && existing->kind == Kind::Grave && existing->memory == memory.life.id)
            {
                return;
            }
        }

        auto& grave = Spawn(Kind::Grave, memory.home.chunk, parent->second,
                            {memory.home.local.x - 28, memory.home.local.y + 16});
        grave.memory = memory.life.id;
    }

    void Game::Stream()
    {
        struct StreamTiming
        {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

            ~StreamTiming()
            {
                RenderProfiler::Event(
                    "chunk_stream", "complete", 0,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count());
            }
        } timing;

        for (auto it = chunks.begin(); it != chunks.end();)
        {
            const ChunkKey key = it->first;

            if (key.x < origin.x - 2 || key.x > origin.x + 2 || key.y < origin.y - 2 || key.y > origin.y + 2)
            {
                scene.Destroy(it->second);
                it = chunks.erase(it);
            }
            else
            {
                auto* root = scene.Find(it->second);
                root->position = {static_cast<float>(key.x - origin.x) * ChunkSize,
                                  static_cast<float>(key.y - origin.y) * ChunkSize};
                ++it;
            }
        }

        scene.CollectDestroyed();

        for (int y = -2; y <= 2; ++y)
        {
            for (int x = -2; x <= 2; ++x)
            {
                ChunkKey key = {origin.x + x, origin.y + y};

                if (chunks.count(key))
                {
                    continue;
                }

                auto& root = scene.Create<Actor>(0);
                root.position = {x * ChunkSize, y * ChunkSize};
                chunks.emplace(key, root.Id());
                BuildChunk(key, root.Id());
            }
        }

        solids.clear();

        for (Actor* base : scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);

            if (actor && actor->solid)
            {
                solids.push_back(actor->Id());
            }
        }
    }

    void Game::TrimRegions()
    {
        if (regions.size() <= 128 || !regionDatabase.IsOpen())
        {
            return;
        }
        for (auto it = regions.begin(); it != regions.end();)
        {
            const auto key = it->first;
            const bool pinned = key == checkpoint.chunk || key == ChapterTwo::Road().chunk ||
                                key == ChapterTwo::Altar().chunk || key == ChunkKey{};
            if (!pinned && !chunks.count(key))
            {
                it = regions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void Game::Rebase()
    {
        Location current = Absolute(hero->position);

        if (current.chunk == origin)
        {
            return;
        }

        const Vec2 shift = {static_cast<float>(current.chunk.x - origin.x) * ChunkSize,
                            static_cast<float>(current.chunk.y - origin.y) * ChunkSize};

        for (Actor* base : scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);

            if (actor && actor->kind == Kind::Projectile)
            {
                actor->position.x -= shift.x;
                actor->position.y -= shift.y;
            }
        }

        camera.x -= shift.x;
        camera.y -= shift.y;
        hero->position = current.local;
        origin = current.chunk;
        Stream();
        if (regions.size() > 128 && initialized)
        {
            Save();
        }
    }

    bool Game::Walkable(Vec2 point, float radius) const
    {
        for (ActorId id : solids)
        {
            auto* actor = static_cast<WorldActor*>(scene.Find(id));

            if (actor && actor->solid && actor->IsActive() &&
                DistanceSquared(point, actor->WorldPosition()) <
                    (radius + actor->radius) * (radius + actor->radius))
            {
                return false;
            }
        }

        return true;
    }

    void Game::Move(WorldActor& actor, Vec2 delta)
    {
        Vec2 current = actor.WorldPosition();

        if (Walkable({current.x + delta.x, current.y}))
        {
            actor.position.x += delta.x;
        }

        current = actor.WorldPosition();

        if (Walkable({current.x, current.y + delta.y}))
        {
            actor.position.y += delta.y;
        }
    }

    bool Game::ClearPath(Vec2 from, Vec2 to) const
    {
        const Vec2 direction = {to.x - from.x, to.y - from.y};
        const float lengthSquared = DistanceSquared(from, to);

        // Test the whole segment so aiming and attraction cannot see through thin obstacles.
        for (ActorId id : solids)
        {
            const auto* obstacle = static_cast<const WorldActor*>(scene.Find(id));

            if (!obstacle || !obstacle->solid || !obstacle->IsActive())
            {
                continue;
            }

            const Vec2 center = obstacle->WorldPosition();
            const float projection =
                lengthSquared > 0
                    ? ((center.x - from.x) * direction.x + (center.y - from.y) * direction.y) / lengthSquared
                    : 0;
            const float t = (std::max)(0.0f, (std::min)(1.0f, projection));
            const Vec2 closest = {from.x + direction.x * t, from.y + direction.y * t};
            const float radius = obstacle->radius + 4;

            if (DistanceSquared(closest, center) < radius * radius)
            {
                return false;
            }
        }

        return true;
    }
} // namespace Ashen

void Ashen::Game::BenchmarkTraverse(unsigned frame)
{
    if (RenderProfiler::Environment("ASHEN_BENCHMARK") != "1")
    {
        return;
    }
    const auto location = WorldSociety::Locate({}, {384.f + frame * (TownWorld::PlayerSpeed / 60.f), 600});
    hero->position = Relative(location);
    Rebase();
    hero->moving = true;
    hero->facing = 2;
    hero->visualYaw = .785398163f;
    hero->motion = hero->previousMotion = WorldActor::Motion::Walk;
    hero->motionSeconds = hero->previousMotionSeconds = frame / 60.f;
    hero->blendSeconds = 1;
}
