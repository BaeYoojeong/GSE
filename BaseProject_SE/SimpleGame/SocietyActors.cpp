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
        return L"E 주민 상태 · " + std::wstring(WorldSociety::JobName(npc->profession)) +
               (npc->profession == Profession::Merchant ? L"  /  B 구매  N 판매  G 나눔" : L"  /  G 나눔");
    }

    QuarryActor::QuarryActor(Game& owner) : WorldActor(owner, Kind::Quarry)
    {
    }

    void QuarryActor::OnUpdate(float)
    {
        Owner().society.SyncActor(*this);
    }
} // namespace Ashen
