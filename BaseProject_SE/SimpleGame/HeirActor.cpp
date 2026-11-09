#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    HeirActor::HeirActor(Game& owner) : InteractiveActor(owner, Kind::Heir)
    {
    }

    void HeirActor::OnUpdate(float seconds)
    {
        Game& game = Owner();
        auto& actor = *this;
        Vec2 world = actor.WorldPosition();

        const Memory* memory = game.MemoryAt(actor.memory);

        if (!memory)
        {
            return;
        }

        const float phase = static_cast<float>(actor.memory % 97);
        const float range = memory->nature == Nature::Wanderer ? 90.0f : 30.0f;
        Vec2 goal = {actor.home.x + std::sin(game.elapsed * .22f + phase) * range,
                     actor.home.y + std::cos(game.elapsed * .17f + phase) * range * .5f};

        if (memory->nature == Nature::Cautious)
        {
            goal = {384, 355};
        }

        float distance = std::sqrt(DistanceSquared(actor.position, goal));

        if (distance > 4)
        {
            game.Move(actor, {(goal.x - actor.position.x) / distance * seconds * 26,
                              (goal.y - actor.position.y) / distance * seconds * 26});
        }

        actor.position.x = (std::max)(24.0f, (std::min)(744.0f, actor.position.x));
        actor.position.y = (std::max)(24.0f, (std::min)(744.0f, actor.position.y));

        if (memory->nature == Nature::Giver && memory->bond > 0 && actor.cooldown <= 0 &&
            game.health < game.MaxHealth() && DistanceSquared(world, game.hero->position) < 100 * 100)
        {
            game.health = (std::min)(game.MaxHealth(), game.health + 8);
            actor.cooldown = 12;
            actor.flash = .5f;
            game.dirty = true;
            game.Notify(L"전승자가 상처를 돌봐줍니다. 누군가를 도왔던 삶이 당신을 돕습니다.");
        }

        if (memory->nature == Nature::Guardian && actor.cooldown <= 0)
        {
            for (Actor* other : game.scene.Snapshot())
            {
                auto* enemy = dynamic_cast<WorldActor*>(other);

                if (enemy && enemy->IsActive() && enemy->kind == Kind::Enemy && enemy->health > 0 &&
                    DistanceSquared(world, enemy->WorldPosition()) < 140 * 140 &&
                    game.ClearPath(world, enemy->WorldPosition()))
                {
                    actor.cooldown = 1.4f;
                    actor.flash = .2f;
                    const Vec2 target = enemy->WorldPosition();
                    actor.FaceDirection({target.x - world.x, target.y - world.y});
                    enemy->health -= 12;
                    enemy->flash = .2f;

                    if (enemy->health <= 0)
                    {
                        game.Defeat(*enemy);
                    }

                    break;
                }
            }
        }
    }

    bool HeirActor::Interact()
    {
        return Owner().BeginConversation(*this);
    }

    std::wstring HeirActor::InteractionPrompt() const
    {
        const Memory* record = Owner().MemoryAt(memory);
        return record ? L"E 대화 · " + Owner().NatureName(record->nature) + L"  /  G 물자 나누기" : L"";
    }
} // namespace Ashen
