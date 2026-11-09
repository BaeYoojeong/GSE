#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <algorithm>

namespace Ashen
{
    InteractiveActor::InteractiveActor(Game& owner, Kind kind) : WorldActor(owner, kind)
    {
    }

    bool InteractiveActor::CanInteract() const
    {
        return true;
    }

    HerbActor::HerbActor(Game& owner) : InteractiveActor(owner, Kind::Herb)
    {
    }

    bool HerbActor::Interact()
    {
        Game& game = Owner();
        auto* actor = this;

        if (game.supplies >= 99)
        {
            game.Notify(L"물자 가방이 가득 찼습니다.");
            return false;
        }

        game.regions[actor->chunk].harvested |= 1u << actor->slot;
        ++game.supplies;
        game.growth.lessons |= 1u;
        game.AddExperience(8);
        game.SpawnLoot(actor->chunk, actor->slot);

        game.scene.Destroy(actor->Id());
        game.Notify(L"채집 완료 · 물자 +1, 경험치 +8. 떨어진 보석과 도구에 다가가세요.");

        return true;
    }

    std::wstring HerbActor::InteractionPrompt() const
    {
        return L"E 채집 · 물자와 경험치 획득 / 떨어진 아이템에 다가가세요";
    }

    FireActor::FireActor(Game& owner) : InteractiveActor(owner, Kind::Fire)
    {
    }

    bool FireActor::Interact()
    {
        Game& game = Owner();
        auto* actor = this;

        Region& region = game.regions[actor->chunk];

        if (!region.lit)
        {
            if (game.supplies < 3)
            {
                game.Notify(L"불씨를 되살리려면 물자 3개가 필요합니다.");
                return false;
            }

            game.supplies -= 3;
            region.lit = true;
            ++game.life.hope;
            game.Notify(L"다시 불이 켜졌습니다. 이곳은 다음 삶에도 안전한 쉼터로 남습니다.");
        }
        else if (game.restTimer <= 0 && game.health < game.MaxHealth())
        {
            game.health = (std::min)(game.MaxHealth(), game.health + 35);
            ++game.life.rests;
            game.restTimer = 5;
            game.Notify(L"불빛 곁에서 숨을 고릅니다. 체력 +35");
        }
        else
        {
            game.Notify(L"이 불씨를 다음 여행의 시작점으로 기억합니다.");
        }

        game.checkpoint = {actor->chunk, {384, 428}};
        game.chapter.Update(game);

        return true;
    }

    std::wstring FireActor::InteractionPrompt() const
    {
        return Owner().Lit(chunk) ? L"E 쉬기 · 이 불씨를 다음 삶의 시작점으로" : L"E 불씨 되살리기 · 물자 3";
    }

    GraveActor::GraveActor(Game& owner) : InteractiveActor(owner, Kind::Grave)
    {
    }

    bool GraveActor::Interact()
    {
        return Owner().BeginConversation(*this);
    }

    std::wstring GraveActor::InteractionPrompt() const
    {
        return L"E 지나간 삶의 흔적 읽기";
    }

    SettlerActor::SettlerActor(Game& owner) : InteractiveActor(owner, Kind::Settler)
    {
    }

    bool SettlerActor::Interact()
    {
        return Owner().BeginConversation(*this);
    }

    std::wstring SettlerActor::InteractionPrompt() const
    {
        return L"E 생존자와 대화  /  G 물자 나누기";
    }

} // namespace Ashen
