#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    namespace
    {
        Vec2 MonsterSeparation(const Game& game, const WorldActor& actor, float seconds)
        {
            const Vec2 world = actor.WorldPosition();
            Vec2 push = {};
            constexpr float clearance = 42;

            // Query only while moving or fighting, so idle streamed monsters do not scan the scene.
            for (Actor* base : game.Scene().Snapshot())
            {
                const auto* other = dynamic_cast<const WorldActor*>(base);

                if (!other || other == &actor || other->health <= 0 || !other->IsVisible() ||
                    (other->kind != Kind::Enemy && other->kind != Kind::Quarry &&
                     other->kind != Kind::Citizen))
                {
                    continue;
                }

                const Vec2 center = other->WorldPosition();
                Vec2 away = {world.x - center.x, world.y - center.y};
                float distance = std::sqrt(away.x * away.x + away.y * away.y);

                if (distance >= clearance)
                {
                    continue;
                }

                if (distance < .001f)
                {
                    away = {actor.Id() < other->Id() ? -1.0f : 1.0f, 0};
                    distance = 1;
                }

                const float amount = (clearance - distance) / clearance;
                push.x += away.x / distance * amount;
                push.y += away.y / distance * amount;
            }

            const float length = std::sqrt(push.x * push.x + push.y * push.y);

            if (length > .001f)
            {
                const float scale = (std::min)(1.0f, length) * seconds * 52 / length;
                return {push.x * scale, push.y * scale};
            }

            return {};
        }

        void KeepInsideChunk(WorldActor& actor)
        {
            actor.position.x = (std::clamp)(actor.position.x, 24.0f, 744.0f);
            actor.position.y = (std::clamp)(actor.position.y, 24.0f, 744.0f);
        }
    } // namespace

    EnemyActor::EnemyActor(Game& owner) : WorldActor(owner, Kind::Enemy)
    {
    }

    void EnemyActor::OnUpdate(float seconds)
    {
        Game& game = Owner();
        auto& actor = *this;
        Vec2 world = actor.WorldPosition();

        const Vec2 camp = game.Relative({actor.chunk, {384, 384}});
        const Vec2 home = game.Relative({actor.chunk, actor.home});
        const float distance = std::sqrt(DistanceSquared(world, game.hero->position));
        const float homeDistance = std::sqrt(DistanceSquared(world, home));
        const bool playerProtected =
            game.Lit(actor.chunk) && DistanceSquared(game.hero->position, camp) < 104 * 104;
        const bool pursue = !playerProtected && DistanceSquared(game.hero->position, home) < 240 * 240 &&
                            homeDistance < 260 && game.ClearPath(world, game.hero->position);

        // A chase used to leave monsters permanently parked at the village or a chunk edge.
        // Their spawn slot is also their territory: lost targets send them back into the field.
        if (!pursue)
        {
            actor.windup = 0;

            if (homeDistance > 4)
            {
                const float step = (std::min)(homeDistance, seconds * 72);
                game.Move(actor, {(home.x - world.x) / homeDistance * step,
                                  (home.y - world.y) / homeDistance * step});
                game.Move(actor, MonsterSeparation(game, actor, seconds));
                KeepInsideChunk(actor);
            }

            return;
        }

        if (actor.windup > 0)
        {
            actor.FaceDirection({game.hero->position.x - world.x, game.hero->position.y - world.y});
            actor.windup -= seconds;

            if (actor.windup <= 0)
            {
                actor.cooldown = 1.2f;

                if (distance < 60 && game.immunity <= 0 && game.ClearPath(world, game.hero->position))
                {
                    game.health = (std::max)(0.0f, game.health - 24);
                    game.immunity = .65f;
                    game.hero->flash = .25f;
                    game.dirty = true;
                }
            }
        }
        else if (distance < 48 && actor.cooldown <= 0)
        {
            actor.FaceDirection({game.hero->position.x - world.x, game.hero->position.y - world.y});
            actor.windup = .65f;
        }
        else if (distance < 310 && distance > 40)
        {
            game.Move(actor, {(game.hero->position.x - world.x) / distance * seconds * 88,
                              (game.hero->position.y - world.y) / distance * seconds * 88});
        }

        game.Move(actor, MonsterSeparation(game, actor, seconds));
        KeepInsideChunk(actor);
    }
} // namespace Ashen
