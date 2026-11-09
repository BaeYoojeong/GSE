#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    ProjectileActor::ProjectileActor(Game& owner) : WorldActor(owner, Kind::Projectile)
    {
    }

    void ProjectileActor::OnUpdate(float seconds)
    {
        Game& game = Owner();
        auto& projectile = *this;

        const float speed = std::sqrt(projectile.velocity.x * projectile.velocity.x +
                                      projectile.velocity.y * projectile.velocity.y);
        const float travel = (std::min)(projectile.remainingRange, speed * seconds);

        if (speed <= 0 || travel <= 0)
        {
            game.scene.Destroy(projectile.Id());
            return;
        }

        // Small substeps prevent tunnelling through collision circles, even at the frame-time cap.
        const int steps = (std::max)(1, static_cast<int>(std::ceil(travel / 6)));
        const Vec2 step = {projectile.velocity.x / speed * travel / steps,
                           projectile.velocity.y / speed * travel / steps};
        const auto actors = game.scene.Snapshot();

        for (int i = 0; i < steps; ++i)
        {
            projectile.position.x += step.x;
            projectile.position.y += step.y;

            if (!game.Walkable(projectile.position, projectile.radius))
            {
                game.scene.Destroy(projectile.Id());
                return;
            }

            for (Actor* base : actors)
            {
                auto* enemy = dynamic_cast<WorldActor*>(base);

                if (!enemy || !game.CombatTarget(*enemy) ||
                    DistanceSquared(projectile.position, enemy->WorldPosition()) > 20 * 20)
                {
                    continue;
                }

                if (enemy->kind == Kind::BellKeeper)
                {
                    game.chapter.DamageBoss(game, *enemy, projectile.damage);
                    game.scene.Destroy(projectile.Id());
                    return;
                }
                if (enemy->kind == Kind::Citizen || enemy->kind == Kind::Quarry)
                {
                    if (enemy->kind == Kind::Citizen)
                    {
                        game.society.DamageCitizen(game, enemy->memory, projectile.damage);
                    }
                    else
                    {
                        game.society.DamageQuarry(game, enemy->chunk, enemy->slot, projectile.damage);
                    }
                    game.society.SyncActor(*enemy);
                    enemy->flash = .18f;
                    game.scene.Destroy(projectile.Id());
                    return;
                }
                enemy->health -= projectile.damage;
                enemy->flash = .18f;
                game.scene.Destroy(projectile.Id());

                if (enemy->health <= 0)
                {
                    ++game.life.kills;
                    game.growth.lessons |= 2u;
                    game.Defeat(*enemy);
                }

                return;
            }
        }

        projectile.remainingRange -= travel;

        if (projectile.remainingRange <= .001f)
        {
            game.scene.Destroy(projectile.Id());
        }
    }
} // namespace Ashen
