#pragma once

#include "Actor.h"
#include <memory>
#include <unordered_map>
#include <utility>
#include <type_traits>

namespace Ashen
{
    class SceneGraph
    {
      public:
        SceneGraph() = default;
        SceneGraph(const SceneGraph&) = delete;
        SceneGraph& operator=(const SceneGraph&) = delete;
        SceneGraph(SceneGraph&&) = delete;
        SceneGraph& operator=(SceneGraph&&) = delete;

        template <class T, class... Args> T& Create(ActorId parent, Args&&... args)
        {
            static_assert(std::is_base_of<Actor, T>::value, "Scene nodes must derive from Actor.");
            auto node = std::make_unique<T>(std::forward<Args>(args)...);
            T& result = *node;
            Insert(std::move(node), parent);
            return result;
        }

        Actor* Find(ActorId id) const;
        bool Reparent(ActorId child, ActorId parent, bool keepWorldPosition = true);
        void Destroy(ActorId id);
        void CollectDestroyed();
        void Update(float seconds);
        void Clear();
        // ID order is stable for gameplay queries. Pointers expire on collection or clear.
        std::vector<Actor*> Snapshot() const;
        // Parent-before-child order, captured at the start of a tick.
        std::vector<Actor*> HierarchySnapshot() const;
        // Visibility inherits from parents; depthAxis projects world position onto camera depth.
        std::vector<const Actor*> RenderSnapshot(Vec2 depthAxis) const;

      private:
        void Insert(std::unique_ptr<Actor> actor, ActorId parent);
        std::unordered_map<ActorId, std::unique_ptr<Actor>> actors;
        ActorId nextId = 1;
        bool updating = false;
    };
} // namespace Ashen
