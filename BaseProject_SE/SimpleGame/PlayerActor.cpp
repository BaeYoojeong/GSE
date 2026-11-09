#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    PlayerActor::PlayerActor(Game& owner) : WorldActor(owner, Kind::Player)
    {
    }

    void PlayerActor::OnUpdate(float seconds)
    {
        Game& game = Owner();
        if (game.inputBlocked || !game.dialogue.empty())
        {
            return;
        }
        sprinting = game.sprint;
        Vec2 direction = {static_cast<float>(game.arrows[2]) - static_cast<float>(game.arrows[0]),
                          static_cast<float>(game.arrows[3]) - static_cast<float>(game.arrows[1])};
        Vec2 delta = ScreenToGround(direction);
        float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        Vec2 previous = position;

        if (length > 0)
        {
            facing = direction.y < 0 ? 1 : (direction.y > 0 ? 3 : (direction.x < 0 ? 0 : 2));
            game.Move(*this,
                      {delta.x / length * seconds * TownWorld::PlayerSpeed * (sprinting ? 1.45f : 1.f),
                       delta.y / length * seconds * TownWorld::PlayerSpeed * (sprinting ? 1.45f : 1.f)});
        }

        float travelled = std::sqrt(DistanceSquared(position, previous));
        moving = travelled > .01f;
        game.life.distance += travelled;
        game.dirty = game.dirty || moving;
        game.Rebase();

        if (game.attack && game.attackTimer <= 0)
        {
            game.Attack();
        }
    }
} // namespace Ashen
