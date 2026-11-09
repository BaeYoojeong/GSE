#include "ChapterActors.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    BellAltarActor::BellAltarActor(Game& game) : InteractiveActor(game, Kind::BellAltar)
    {
    }

    bool BellAltarActor::Interact()
    {
        return Owner().chapter.Interact(Owner());
    }

    std::wstring BellAltarActor::InteractionPrompt() const
    {
        switch (Owner().chapter.CurrentStage())
        {
        case ChapterTwo::Stage::Offering:
            return L"E 종탑 봉인 열기 · 물자 6 · 보스 전투 시작";
        case ChapterTwo::Stage::RingBell:
            return L"E 종 울리기 · 종탑 복구와 보상 받기";
        case ChapterTwo::Stage::Complete:
            return L"E 복구한 종탑 · 다음 삶의 거점으로 지정";
        default:
            return L"E 옛 수도원의 종탑 · Q 원정 목표";
        }
    }

    BellKeeperActor::BellKeeperActor(Game& game) : WorldActor(game, Kind::BellKeeper)
    {
    }

    void BellKeeperActor::OnUpdate(float seconds)
    {
        Game& game = Owner();
        attackVisualSeconds = (std::max)(0.0f, attackVisualSeconds - seconds);
        if (game.chapter.CurrentStage() != ChapterTwo::Stage::BellKeeper)
        {
            game.scene.Destroy(Id());
            return;
        }
        const Vec2 world = WorldPosition();
        const Vec2 player = game.hero->WorldPosition();
        if (DistanceSquared(world, player) > 400 * 400)
        {
            if (health < game.chapter.BossMaximumHealth() || windup > 0)
            {
                game.chapter.ResetBattle(game, "leash");
                game.Notify(L"전투 구역에서 벗어났습니다. 종지기가 회복합니다. 봉인은 열린 채로 남습니다.");
            }
            return;
        }
        const bool enraged = health <= game.chapter.BossMaximumHealth() * .5f;
        if (windup > 0)
        {
            windup = (std::max)(0.0f, windup - seconds);
            if (windup > 0)
            {
                return;
            }
            const Vec2 impact = {world.x + attackVisualTarget.x - position.x,
                                 world.y + attackVisualTarget.y - position.y};
            attackVisualSeconds = .3f;
            cooldown = enraged ? 1.0f : 1.6f;
            if (DistanceSquared(player, impact) <= remainingRange * remainingRange && game.immunity <= 0 &&
                game.ClearPath(impact, player))
            {
                game.health = (std::max)(0.0f, game.health - (std::max)(18.0f, game.MaxHealth() *
                                                                                   (enraged ? .18f : .13f)));
                game.immunity = .75f;
                game.hero->flash = .35f;
                game.dirty = true;
                RenderProfiler::Event("chapter_boss_hit", "pattern=" + std::to_string(variant));
            }
            return;
        }
        if (cooldown > 0)
        {
            return;
        }
        FaceDirection({player.x - world.x, player.y - world.y});
        variant = attacks++ % 2;
        attackVisualTarget =
            variant == 0 ? position : Vec2{position.x + player.x - world.x, position.y + player.y - world.y};
        remainingRange = variant == 0 ? (enraged ? 155.0f : 125.0f) : (enraged ? 72.0f : 56.0f);
        windup = variant == 0 ? (enraged ? .85f : 1.2f) : (enraged ? 1.05f : 1.35f);
        RenderProfiler::Event("chapter_boss_windup", "pattern=" + std::to_string(variant) +
                                                         " phase=" + (enraged ? "2" : "1") +
                                                         " radius=" + std::to_string(remainingRange));
    }
} // namespace Ashen
