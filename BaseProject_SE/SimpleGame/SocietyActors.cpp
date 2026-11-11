#include "WorldActors.h"
#include "DarkAgeGame.h"
#include <cmath>

namespace Ashen
{
    CitizenActor::CitizenActor(Game& owner) : InteractiveActor(owner, Kind::Citizen)
    {
    }

    void CitizenActor::OnUpdate(float seconds)
    {
        Owner().society.SyncActor(*this);
        const auto* npc = Owner().society.Find(memory);
        if (!npc)
        {
            return;
        }
        const float speed = std::sqrt(DistanceSquared(npc->motionVelocity, {}));
        moving = speed > 1;
        if (moving)
        {
            if (attackVisualSeconds <= 0)
            {
                FaceDirection(npc->motionVelocity);
            }
            walkCycle = std::fmod(walkCycle + speed * seconds / 10, 4.0f);
        }
    }

    bool CitizenActor::Interact()
    {
        Game& game = Owner();
        const auto* npc = game.society.Find(memory);

        if (!npc || npc->health <= 0)
        {
            return false;
        }

        if (npc->profession == Profession::Merchant && game.society.IsAtMarket(*npc))
        {
            if (!game.Shopkeeper(Id()))
            {
                game.Notify(L"지금 이 상인과 거래할 수 없습니다.");
                return false;
            }
            game.requestedShop = Id();
            game.ClearInput();
            return true;
        }
        game.conversation = Id();
        game.choiceMade = npc->lastTalkLife == game.life.id;
        game.dialogue = game.society.Describe(memory);
        game.ClearInput();
        return true;
    }

    std::wstring CitizenActor::InteractionPrompt() const
    {
        const auto* npc = Owner().society.Find(memory);
        if (!npc)
        {
            return L"";
        }
        if (npc->profession == Profession::Merchant && Owner().society.IsAtMarket(*npc))
        {
            return L"E 상인과 거래 · 무기·회복 물약 구매";
        }
        return L"E 주민 상태 · " + std::wstring(WorldSociety::JobName(npc->profession)) +
               (npc->profession == Profession::Merchant ? L"  /  시장에서 E 거래  G 나눔" : L"  /  G 나눔");
    }

    MarketStallActor::MarketStallActor(Game& owner) : WorldActor(owner, Kind::MarketStall)
    {
    }

    QuarryActor::QuarryActor(Game& owner) : WorldActor(owner, Kind::Quarry)
    {
    }

    void QuarryActor::OnUpdate(float)
    {
        Owner().society.SyncActor(*this);
    }
} // namespace Ashen
