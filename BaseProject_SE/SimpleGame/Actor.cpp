#include "Actor.h"
#include "SceneGraph.h"

Ashen::Vec2 Ashen::Actor::WorldPosition() const
{
    Vec2 world = position;

    for (const Actor* node = graph ? graph->Find(parent) : nullptr; node; node = graph->Find(node->parent))
    {
        world.x += node->position.x;
        world.y += node->position.y;
    }

    return world;
}

bool Ashen::Actor::IsActive() const
{
    for (const Actor* node = this; node; node = graph ? graph->Find(node->parent) : nullptr)
    {
        if (!node->active || node->removed)
        {
            return false;
        }
    }

    return true;
}

bool Ashen::Actor::IsVisible() const
{
    for (const Actor* node = this; node; node = graph ? graph->Find(node->parent) : nullptr)
    {
        if (!node->visible || node->removed)
        {
            return false;
        }
    }

    return true;
}
