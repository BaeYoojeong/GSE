#pragma once

#include <cstdint>
#include <vector>

namespace Ashen
{
    using ActorId = std::uint64_t;

    struct Vec2
    {
        float x = 0;
        float y = 0;
    };

    class SceneGraph;

    class Actor
    {
      public:
        virtual ~Actor() = default;
        Actor() = default;
        Actor(const Actor&) = delete;
        Actor& operator=(const Actor&) = delete;

        ActorId Id() const
        {
            return id;
        }

        ActorId Parent() const
        {
            return parent;
        }

        Vec2 WorldPosition() const;
        bool IsActive() const;
        bool IsVisible() const;

        virtual void Update(float seconds)
        {
        }

        Vec2 position;
        bool active = true;
        bool visible = true;
        bool renderable = false;
        float radius = 32;
        int layer = 1;

      private:
        friend class SceneGraph;
        SceneGraph* graph = nullptr;
        ActorId id = 0;
        ActorId parent = 0;
        std::vector<ActorId> children;
        bool removed = false;
    };
} // namespace Ashen
