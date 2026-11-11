#include "WorldActors.h"
#include "DarkAgeGame.h"
#include "WorldLayout.h"

namespace Ashen
{
    ExplorationActor::ExplorationActor(Game& owner) : InteractiveActor(owner, Kind::Exploration)
    {
    }

    bool ExplorationActor::Interact()
    {
        auto& game = Owner();
        const auto site = static_cast<WorldLayout::Site>(variant);
        const wchar_t* story =
            site == WorldLayout::Site::Camp
                ? L"누군가가 남긴 천막. 꺼진 불씨에 물자를 보태면\n다음 삶에도 다시 찾아올 쉼터가 된다."
            : site == WorldLayout::Site::Ruins
                ? L"무너진 돌 위의 문장: 하나의 끝은 다른 생의 시작이 된다.\n길은 여기서 끝나지 않는다."
                : L"잿더미 속에서도 약초는 자란다. 주변을 채집해 보자.\n채집 기록은 삶이 바뀌어도 이 세계에 "
                  L"남는다.";
        game.dialogue =
            std::wstring(WorldLayout::SiteName(site)) + L"\n\n" + story + L"\n\n" + game.VillageDirection();
        game.conversation = 0;
        game.choiceMade = true;
        game.ClearInput();
        return true;
    }

    std::wstring ExplorationActor::InteractionPrompt() const
    {
        return std::wstring(L"E 살펴보기 · ") +
               WorldLayout::SiteName(static_cast<WorldLayout::Site>(variant));
    }
} // namespace Ashen
