#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <set>
#include <cmath>
#include "RenderProfiler.h"

namespace Ashen
{
    ResidenceActor::ResidenceActor(Game& owner) : InteractiveActor(owner, Kind::Residence)
    {
    }

    bool ResidenceActor::Interact()
    {
        auto& game = Owner();
        const auto* npc = game.society.Find(memory);
        if (!npc)
        {
            return false;
        }
        game.Notify(L"집 " + std::to_wstring(npc->houseLot + 1) + L"호 · 주민 #" + std::to_wstring(memory) +
                    L" · " + WorldSociety::JobName(npc->profession) + L" · " +
                    WorldSociety::JourneyName(npc->journeyKind));
        return true;
    }

    std::wstring ResidenceActor::InteractionPrompt() const
    {
        auto& game = Owner();
        const auto* npc = game.society.Find(memory);
        return npc ? L"E 집 " + std::to_wstring(npc->houseLot + 1) + L"호 · " +
                         WorldSociety::JobName(npc->profession) + L"의 집"
                   : L"집";
    }

    void WorldSociety::SyncHomes(Game& game)
    {
        std::set<std::uint64_t> present;
        bool changed = false;
        for (Actor* base : game.scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);
            if (!actor || actor->kind != Kind::Residence || !actor->IsActive())
            {
                continue;
            }
            const auto* npc = Find(actor->memory);
            if (!npc)
            {
                game.scene.Destroy(actor->Id());
                changed = true;
                continue;
            }
            const auto place = Locate(npc->village, TownWorld::House(npc->houseLot));
            const auto parent = game.chunks.find(place.chunk);
            if (parent == game.chunks.end())
            {
                game.scene.Destroy(actor->Id());
                changed = true;
                continue;
            }
            if (actor->Parent() != parent->second)
            {
                game.scene.Reparent(actor->Id(), parent->second, false);
                changed = true;
            }
            actor->chunk = npc->village;
            actor->position = place.local;
            present.insert(npc->id);
        }
        for (const auto& pair : citizens)
        {
            const auto& npc = pair.second;
            const auto place = Locate(npc.village, TownWorld::House(npc.houseLot));
            const auto parent = game.chunks.find(place.chunk);
            if (parent == game.chunks.end() || present.count(npc.id))
            {
                continue;
            }
            auto& actor = game.Spawn(Kind::Residence, npc.village, parent->second, place.local);
            actor.memory = npc.id;
            actor.variant = static_cast<unsigned>(npc.id % 4);
            actor.radius = 32;
            actor.solid = true;
            changed = true;
        }
        if (changed)
        {
            game.solids.clear();
            for (Actor* base : game.scene.Snapshot())
            {
                const auto* actor = dynamic_cast<const WorldActor*>(base);
                if (actor && actor->IsActive() && actor->solid)
                {
                    game.solids.push_back(actor->Id());
                }
            }
            const auto safePoint = [&](Vec2 point)
            {
                if (game.Walkable(point, 14))
                {
                    return point;
                }
                for (int ring = 1; ring <= 12; ++ring)
                {
                    for (int side = 0; side < 16; ++side)
                    {
                        const float angle = side * .3926990817f;
                        const Vec2 candidate = {point.x + std::cos(angle) * ring * 24,
                                                point.y + std::sin(angle) * ring * 24};
                        if (game.Walkable(candidate, 14))
                        {
                            RenderProfiler::Event("housing_clearance_recovery",
                                                  "radius=" + std::to_string(ring * 24));
                            return candidate;
                        }
                    }
                }
                return point;
            };
            if (game.hero)
            {
                game.hero->position = safePoint(game.hero->position);
            }
            for (auto& pair : citizens)
            {
                auto& npc = pair.second;
                const auto place = Locate(npc.village, npc.position);
                if (!game.chunks.count(place.chunk))
                {
                    continue;
                }
                const Vec2 world = game.Relative(place);
                const Vec2 safe = safePoint(world);
                npc.position.x += safe.x - world.x;
                npc.position.y += safe.y - world.y;
            }
        }
    }
} // namespace Ashen
