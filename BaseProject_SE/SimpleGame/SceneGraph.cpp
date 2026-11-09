#include "SceneGraph.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Ashen
{
    void SceneGraph::Insert(std::unique_ptr<Actor> actor, ActorId parent)
    {
        if ((parent && !Find(parent)) || nextId == (std::numeric_limits<ActorId>::max)())
        {
            throw std::logic_error("Invalid scene parent or exhausted ActorId space.");
        }

        actor->id = nextId++;
        actor->graph = this;
        actor->parent = parent;
        const ActorId id = actor->id;
        actors.emplace(id, std::move(actor));

        if (parent)
        {
            Find(parent)->children.push_back(id);
        }
    }

    Actor* SceneGraph::Find(ActorId id) const
    {
        auto it = actors.find(id);
        return it != actors.end() && !it->second->removed ? it->second.get() : nullptr;
    }

    bool SceneGraph::Reparent(ActorId child, ActorId parent, bool keepWorldPosition)
    {
        Actor* node = Find(child);

        if (!node || (parent && !Find(parent)))
        {
            return false;
        }

        for (Actor* ancestor = Find(parent); ancestor; ancestor = Find(ancestor->parent))
        {
            if (ancestor == node)
            {
                return false;
            }
        }

        Vec2 world = node->WorldPosition();

        if (Actor* old = Find(node->parent))
        {
            auto& siblings = old->children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
        }

        node->parent = parent;
        Actor* next = Find(parent);

        if (next)
        {
            next->children.push_back(child);
        }

        if (keepWorldPosition)
        {
            Vec2 base = next ? next->WorldPosition() : Vec2{};
            node->position = {world.x - base.x, world.y - base.y};
        }

        return true;
    }

    void SceneGraph::Destroy(ActorId id)
    {
        Actor* actor = Find(id);

        if (!actor)
        {
            return;
        }

        for (ActorId child : actor->children)
        {
            Destroy(child);
        }

        actor->removed = true;
    }

    void SceneGraph::CollectDestroyed()
    {
        if (updating)
        {
            return;
        }

        for (auto& pair : actors)
        {
            auto& children = pair.second->children;
            children.erase(
                std::remove_if(children.begin(), children.end(), [&](ActorId id) { return !Find(id); }),
                children.end());
        }

        for (auto it = actors.begin(); it != actors.end();)
        {
            if (it->second->removed)
            {
                it = actors.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::vector<Actor*> SceneGraph::Snapshot() const
    {
        std::vector<Actor*> result;
        result.reserve(actors.size());

        for (const auto& pair : actors)
        {
            if (!pair.second->removed)
            {
                result.push_back(pair.second.get());
            }
        }

        std::sort(result.begin(), result.end(), [](Actor* a, Actor* b) { return a->Id() < b->Id(); });
        return result;
    }

    std::vector<Actor*> SceneGraph::HierarchySnapshot() const
    {
        const auto nodes = Snapshot();
        std::vector<Actor*> pending;
        std::vector<Actor*> result;
        pending.reserve(nodes.size());
        result.reserve(nodes.size());

        // Reverse roots on a stack to retain their stable ID order (player before chunks).
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        {
            if ((*it)->parent == 0)
            {
                pending.push_back(*it);
            }
        }

        while (!pending.empty())
        {
            Actor* node = pending.back();
            pending.pop_back();
            result.push_back(node);

            for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
            {
                if (Actor* child = Find(*it))
                {
                    pending.push_back(child);
                }
            }
        }

        return result;
    }

    std::vector<const Actor*> SceneGraph::RenderSnapshot(Vec2 depthAxis) const
    {
        struct Entry
        {
            const Actor* actor;
            float depth;
        };

        std::vector<Entry> entries;
        entries.reserve(actors.size());

        for (const auto& pair : actors)
        {
            const Actor* actor = pair.second.get();

            if (actor->renderable && actor->IsVisible())
            {
                const Vec2 world = actor->WorldPosition();
                entries.push_back({actor, world.x * depthAxis.x + world.y * depthAxis.y});
            }
        }

        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b)
                  {
                      if (a.actor->layer != b.actor->layer)
                      {
                          return a.actor->layer < b.actor->layer;
                      }

                      if (a.depth != b.depth)
                      {
                          return a.depth < b.depth;
                      }

                      return a.actor->Id() < b.actor->Id();
                  });

        std::vector<const Actor*> result;
        result.reserve(entries.size());

        for (const Entry& entry : entries)
        {
            result.push_back(entry.actor);
        }

        return result;
    }

    void SceneGraph::Update(float seconds)
    {
        // A fixed snapshot delays newly spawned actors until the next tick. Destruction is deferred.
        auto snapshot = HierarchySnapshot();
        updating = true;

        for (Actor* actor : snapshot)
        {
            if (actor->IsActive())
            {
                actor->Update(seconds);
            }
        }

        updating = false;
        CollectDestroyed();
    }

    void SceneGraph::Clear()
    {
        for (auto& pair : actors)
        {
            pair.second->removed = true;
        }

        CollectDestroyed();
    }
} // namespace Ashen
