#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    DropActor::DropActor(Game& owner) : WorldActor(owner, Kind::Drop)
    {
    }

    void DropActor::OnUpdate(float seconds)
    {
        Game& game = Owner();
        auto& drop = *this;

        const Vec2 world = drop.WorldPosition();
        const float distance = std::sqrt(DistanceSquared(world, game.hero->position));

        if (drop.cooldown > 0 || distance > game.PickupRange() || !game.ClearPath(world, game.hero->position))
        {
            return;
        }

        if (distance <= 24)
        {
            game.CollectDrop(drop);
            return;
        }

        const float travel = (std::min)(distance - 16, seconds * (game.growth.magnetSeconds > 0 ? 430 : 240));
        const Vec2 step = {(game.hero->position.x - world.x) / distance * travel,
                           (game.hero->position.y - world.y) / distance * travel};
        game.Move(drop, step);
    }
} // namespace Ashen
