#include "WorldSociety.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <set>

namespace Ashen
{
    void WorldSociety::SpawnVillage(Game& game, ChunkKey village, ActorId parent)
    {
        // Streaming a chunk must also show visitors whose home chunk is not loaded.
        (void)village;
        (void)parent;
        SyncActors(game);
    }

    void WorldSociety::SyncActors(Game& game)
    {
        SyncHomes(game);
        std::set<std::uint64_t> presentCitizens;
        std::set<std::pair<ChunkKey, int>> presentQuarries;
        for (Actor* base : game.scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);
            if (!actor || (actor->kind != Kind::Citizen && actor->kind != Kind::Quarry))
            {
                continue;
            }
            const auto* npc = actor->kind == Kind::Citizen ? Find(actor->memory) : nullptr;
            const auto* ground = actor->kind == Kind::Quarry ? Quarry(actor->chunk, actor->slot) : nullptr;
            if (!npc && !ground)
            {
                game.scene.Destroy(actor->Id());
                continue;
            }
            const Location place =
                npc ? Locate(npc->village, npc->position) : Locate(ground->village, ground->position);
            const auto parent = game.chunks.find(place.chunk);
            if (parent == game.chunks.end())
            {
                game.scene.Destroy(actor->Id());
                continue;
            }
            if (actor->Parent() != parent->second)
            {
                game.scene.Reparent(actor->Id(), parent->second, false);
                RenderProfiler::Event("society_actor_crossing",
                                      "npc=" + std::to_string(actor->memory) +
                                          " chunk=" + std::to_string(place.chunk.x) + "," +
                                          std::to_string(place.chunk.y));
            }
            if (npc)
            {
                presentCitizens.insert(npc->id);
            }
            else
            {
                presentQuarries.insert({ground->village, ground->slot});
            }
            SyncActor(*actor);
        }
        for (const auto& pair : citizens)
        {
            const auto& npc = pair.second;
            const Location place = Locate(npc.village, npc.position);
            const auto parent = game.chunks.find(place.chunk);
            if (parent != game.chunks.end() && !presentCitizens.count(npc.id))
            {
                auto& actor = game.Spawn(Kind::Citizen, npc.village, parent->second, place.local);
                actor.memory = npc.id;
                SyncActor(actor);
            }
        }
        for (const auto& pair : hunting)
        {
            const auto& ground = pair.second;
            const Location place = Locate(ground.village, ground.position);
            const auto parent = game.chunks.find(place.chunk);
            if (parent != game.chunks.end() && !presentQuarries.count(pair.first))
            {
                auto& actor =
                    game.Spawn(Kind::Quarry, ground.village, parent->second, place.local, ground.slot);
                SyncActor(actor);
            }
        }
    }

    void WorldSociety::SyncActor(WorldActor& actor) const
    {
        // actor.chunk stays the persistent owner key. Parent and local position describe physical location.
        if (actor.kind == Kind::Citizen)
        {
            const auto* npc = Find(actor.memory);
            if (!npc)
            {
                return;
            }
            const Location place = Locate(npc->village, npc->position);
            actor.chunk = npc->village;
            actor.position = place.local;
            actor.health = npc->health;
            actor.variant = static_cast<unsigned>(npc->profession);
            actor.visible = npc->health > 0;
            actor.attackVisualSeconds = npc->attackVisualSeconds;
            actor.attackVisualTarget = InFrame(npc->village, npc->attackVisualTarget, place.chunk);
            actor.windup = npc->attackVisualSeconds;
            actor.flash = (std::max)(actor.flash, npc->hitVisualSeconds);
            if (npc->attackVisualSeconds > 0)
            {
                actor.FaceDirection({npc->attackVisualTarget.x - npc->position.x,
                                     npc->attackVisualTarget.y - npc->position.y});
            }
        }
        else if (const auto* ground = Quarry(actor.chunk, actor.slot))
        {
            actor.position = Locate(ground->village, ground->position).local;
            actor.health = ground->health;
            actor.visible = ground->health > 0 && !ground->placementPending;
            actor.flash = (std::max)(actor.flash, ground->hitVisualSeconds);
        }
    }
} // namespace Ashen
