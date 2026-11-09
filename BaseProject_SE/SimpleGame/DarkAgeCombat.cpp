#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    bool Game::CombatTarget(const WorldActor& actor) const
    {
        if (!actor.IsActive())
        {
            return false;
        }
        if (actor.kind == Kind::Citizen)
        {
            const auto* npc = society.Find(actor.memory);
            return npc && npc->health > 0 && npc->reviveProtection <= 0 &&
                   (pkEnabled || society.IsHostile(actor.memory));
        }
        if (actor.kind == Kind::Quarry)
        {
            const auto* quarry = society.Quarry(actor.chunk, actor.slot);
            return quarry && quarry->health > 0 && !quarry->placementPending;
        }
        if (actor.kind == Kind::BellKeeper)
        {
            return chapter.CurrentStage() == ChapterTwo::Stage::BellKeeper && actor.health > 0;
        }
        return actor.kind == Kind::Enemy && actor.health > 0;
    }

    void Game::TogglePK()
    {
        if (Dead() || !dialogue.empty() || inputBlocked)
        {
            return;
        }
        pkEnabled = !pkEnabled;
        ClearProjectiles();
        ClearInput();
        Notify(pkEnabled
                   ? L"PK 켜짐 · 가까운 주민도 자동 조준 대상입니다. 주민은 사망 후 마을에서 즉시 부활합니다."
                   : L"PK 꺼짐 · 일반 주민 보호, 적대 주민과 괴물에게만 발사합니다.");
    }

    WorldActor* Game::AimTarget() const
    {
        WorldActor* target = nullptr;
        float best = AttackRange * AttackRange;

        for (Actor* base : scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);

            if (!actor || !CombatTarget(*actor))
            {
                continue;
            }

            const float distance = DistanceSquared(hero->position, actor->WorldPosition());

            if (distance <= best && ClearPath(hero->position, actor->WorldPosition()))
            {
                best = distance;
                target = actor;
            }
        }

        return target;
    }

    void Game::Attack()
    {
        const WorldActor* target = AimTarget();

        if (!target)
        {
            attackTimer = .08f;
            return;
        }

        const Vec2 goal = target->WorldPosition();
        const float length = std::sqrt(DistanceSquared(hero->position, goal));

        if (length < .001f)
        {
            return;
        }

        // Root-owned projectiles are independent of the target and of chunk lifetime.
        auto& projectile = Spawn(Kind::Projectile, origin, 0, hero->position);
        projectile.velocity = {(goal.x - hero->position.x) / length * 480,
                               (goal.y - hero->position.y) / length * 480};
        projectile.remainingRange = AttackRange;
        projectile.damage = ShotDamage();
        projectile.radius = 4;

        attackTimer = ShotCooldown();
        hero->flash = .10f;
    }

    void Game::Defeat(WorldActor& enemy, bool playerReward)
    {
        Region& region = regions[enemy.chunk];
        const unsigned flag = 1u << enemy.slot;

        if (region.defeated & flag)
        {
            return;
        }

        region.defeated |= flag;
        region.enemyRespawn[enemy.slot] = EnemyRespawnSeconds;
        region.enemyLoot[enemy.slot] = enemy.position;
        if (playerReward)
        {
            supplies = (std::min)(99, supplies + 1);
            AddExperience(12);
            SpawnLoot(enemy.chunk, 6 + enemy.slot);
        }
        else
        {
            // NPC hunters receive their own inventory reward, not free player XP or loot.
            region.lootCollected |= std::uint32_t{15} << ((6 + enemy.slot) * 4);
        }
        scene.Destroy(enemy.Id());
        dirty = true;

        if (playerReward)
        {
            Notify(L"위협 제거 · 물자 +1, 경험치 +12. 6초 후 안전한 위치에서 리젠됩니다.");
        }
    }

    void Game::UpdateEnemyRespawns(float seconds)
    {
        for (auto& pair : regions)
        {
            auto& region = pair.second;
            for (int slot = 0; slot < 2; ++slot)
            {
                const unsigned flag = 1u << slot;
                if (!EnemySlotEnabled(pair.first, slot) || !(region.defeated & flag))
                {
                    continue;
                }
                region.enemyRespawn[slot] = (std::max)(0.0f, region.enemyRespawn[slot] - seconds);
                dirty = true;
                if (region.enemyRespawn[slot] > 0)
                {
                    continue;
                }
                const auto parent = chunks.find(pair.first);
                const Vec2 spawn = EnemyPosition(pair.first, slot);
                bool occupied = false;
                if (parent != chunks.end())
                {
                    const Vec2 world = Relative({pair.first, spawn});
                    occupied = !Dead() && DistanceSquared(world, hero->position) < 120 * 120;
                    for (const auto& resident : society.citizens)
                    {
                        const auto& npc = resident.second;
                        const Vec2 residentPosition = Relative({npc.village, npc.position});

                        if (npc.health > 0 && DistanceSquared(world, residentPosition) < 90 * 90)
                        {
                            occupied = true;
                            break;
                        }
                    }
                }
                if (occupied)
                {
                    region.enemyRespawn[slot] = 1;
                    continue;
                }
                // Each spawn slot owns one bounded loot generation. Expire old uncollected drops
                // before clearing the reward bits, so they cannot collect the next generation's loot.
                const int source = 6 + slot;
                WorldActor* existingEnemy = nullptr;

                for (Actor* base : scene.Snapshot())
                {
                    auto* actor = dynamic_cast<WorldActor*>(base);

                    if (actor && actor->kind == Kind::Enemy && actor->chunk == pair.first &&
                        actor->slot == slot)
                    {
                        existingEnemy = actor;
                    }

                    if (actor && actor->kind == Kind::Drop && actor->chunk == pair.first &&
                        actor->slot / 4 == source)
                    {
                        scene.Destroy(actor->Id());
                    }
                }
                region.lootCollected &= ~(std::uint32_t{15} << (source * 4));
                region.defeated &= ~flag;
                if (parent != chunks.end() && !existingEnemy)
                {
                    auto& enemy =
                        Spawn(Kind::Enemy, pair.first, parent->second, EnemyPosition(pair.first, slot), slot);
                    enemy.health = EnemyMaximumHealth;
                    enemy.cooldown = 1;
                }

                RenderProfiler::Event(existingEnemy ? "enemy_respawn_existing_slot" : "enemy_respawn",
                                      "chunk=" + std::to_string(pair.first.x) + "," +
                                          std::to_string(pair.first.y) + " slot=" + std::to_string(slot) +
                                          " loaded=" + (parent != chunks.end() ? "1" : "0"));
            }
        }
    }

    void Game::ClearProjectiles()
    {
        for (Actor* base : scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);

            if (actor && actor->kind == Kind::Projectile)
            {
                scene.Destroy(actor->Id());
            }
        }
    }
} // namespace Ashen
