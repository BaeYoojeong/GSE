#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    WorldActor* Game::Nearest() const
    {
        WorldActor* result = nullptr;
        float best = 86 * 86;

        for (Actor* base : scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);

            if (!actor || !actor->IsActive() || !actor->visible || !actor->CanInteract())
            {
                continue;
            }

            Vec2 contact = actor->WorldPosition();
            if (actor->kind == Kind::Residence)
            {
                contact.y += 52;
            }
            const float distance = DistanceSquared(contact, hero->position);

            if (distance < best && ClearPath(hero->position, contact))
            {
                result = actor;
                best = distance;
            }
        }

        return result;
    }

    std::wstring Game::NatureName(Nature nature) const
    {
        switch (nature)
        {
        case Nature::Guardian:
            return L"지키는 사람";
        case Nature::Giver:
            return L"나누는 사람";
        case Nature::Cautious:
            return L"불씨를 찾는 사람";
        default:
            return L"길을 걷는 사람";
        }
    }

    std::wstring Game::EmotionName(Emotion emotion) const
    {
        return emotion == Emotion::Fear ? L"두려움" : (emotion == Emotion::Anger ? L"분노" : L"희망");
    }

    std::wstring Game::Description(const WorldActor& actor) const
    {
        return actor.InteractionPrompt();
    }

    std::wstring Game::VillageDirection(int targetTown) const
    {
        const int index = targetTown >= 0 && targetTown < 7 ? targetTown : TownWorld::Nearest(origin);
        const auto& town = TownWorld::Towns()[index];
        const ChunkKey nearest = town.center;
        const double dx = static_cast<double>(nearest.x - origin.x) * ChunkSize + 384 - hero->position.x;
        const double dy = static_cast<double>(nearest.y - origin.y) * ChunkSize + 410 - hero->position.y;
        const double x = CameraCos * dx - CameraSin * dy;
        const double y = CameraDepth * (CameraSin * dx + CameraCos * dy);
        const wchar_t* directions[] = {L"→", L"↘", L"↓", L"↙", L"←", L"↖", L"↑", L"↗"};
        const int sector = static_cast<int>(std::floor(std::atan2(y, x) / .785398163 + .5));
        const auto seconds = static_cast<unsigned long long>(
            (std::min)(1000000000.0, std::hypot(dx, dy) / TownWorld::PlayerSpeed));
        return std::wstring(town.name) + L" " + directions[(sector + 8) % 8] + L" · 중심까지 직선 약 " +
               std::to_wstring((seconds / 5) * 5) + L"초 · 좌표 (" + std::to_wstring(nearest.x) + L"," +
               std::to_wstring(nearest.y) + L") · M 마을 지도 / Q 원정";
    }

    std::wstring Game::LegacyDirection() const
    {
        if (memories.empty())
        {
            return L"아직 전승된 삶이 없습니다";
        }

        const Memory& memory = memories.rbegin()->second;
        double dx = static_cast<double>(memory.home.chunk.x - origin.x) * ChunkSize + memory.home.local.x -
                    hero->position.x;
        double dy = static_cast<double>(memory.home.chunk.y - origin.y) * ChunkSize + memory.home.local.y -
                    hero->position.y;

        if (dx * dx + dy * dy < 100 * 100)
        {
            return L"이전 삶의 흔적 · 이 근처";
        }

        double x = CameraCos * dx - CameraSin * dy;
        double y = CameraDepth * (CameraSin * dx + CameraCos * dy);
        const wchar_t* arrows[] = {L"→", L"↘", L"↓", L"↙", L"←", L"↖", L"↑", L"↗"};
        int sector = static_cast<int>(std::floor(std::atan2(y, x) / .785398163 + .5));
        return std::wstring(L"이전 삶의 흔적  ") + arrows[(sector + 8) % 8];
    }

    void Game::Interact()
    {
        if (Dead())
        {
            return;
        }

        if (!dialogue.empty())
        {
            CloseDialogue();
            return;
        }

        WorldActor* actor = Nearest();

        if (actor && actor->Interact())
        {
            dirty = true;
            Save();
        }
    }

    bool Game::BeginConversation(WorldActor& target)
    {
        const WorldActor* actor = &target;

        conversation = actor->Id();

        if (actor->kind == Kind::Heir || actor->kind == Kind::Grave)
        {
            const Memory* memory = MemoryAt(actor->memory);

            if (!memory)
            {
                return false;
            }

            choiceMade = memory->lastTalkLife == life.id || actor->kind == Kind::Grave;
            dialogue = L"제" + std::to_wstring(memory->life.id) + L"의 삶 · " + NatureName(memory->nature) +
                       L"\n남겨진 감정: " + EmotionName(memory->emotion) + L"  ·  나눔 " +
                       std::to_wstring(memory->life.gifts) + L"회 · 위협 제거 " +
                       std::to_wstring(memory->life.kills) + L"회";

            if (actor->kind == Kind::Heir)
            {
                dialogue += memory->nature == Nature::Giver ? L"\n“누군가의 상처를 그냥 지나칠 수가 없어요.”"
                            : memory->nature == Nature::Guardian
                                ? L"\n“이 길만큼은 지키고 싶어요. 이유는 잘 모르겠지만.”"
                            : memory->nature == Nature::Cautious
                                ? L"\n“불이 있는 곳에 있으면 조금 안심이 돼요.”"
                                : L"\n“저 길 너머가 자꾸 궁금해져요. 당신도 그런가요?”";
                dialogue += L"\n당신과의 신뢰 " + std::to_wstring(memory->bond) +
                            L" · 이 관계는 다음 삶에도 남습니다.";
                dialogue += memory->emotion == Emotion::Fear
                                ? L"\n“괜찮을 거라고 말하고 싶지만, 아직은 조금 두려워요.”"
                            : memory->emotion == Emotion::Anger
                                ? L"\n“이런 세상을 당연하게 받아들이고 싶지는 않아요.”"
                                : L"\n“그래도 우리가 지킬 수 있는 무언가가 있겠지요.”";
            }
            else
            {
                dialogue += L"\n하나의 죽음이 다른 생명의 시작이 되었습니다.";
            }
        }
        else
        {
            Region& region = regions[actor->chunk];
            choiceMade = region.lastTalkLife == life.id;
            dialogue = region.lit ? L"“불이 켜져 있으니, 오늘 밤은 버틸 수 있겠어요.”"
                                  : L"“물자 셋이면 저 불씨를 되살릴 수 있어요.”";
            dialogue += region.aid > 0 ? L"\n“당신들이 나누어 준 것을 기억하고 있어요.”"
                                       : L"\n“새 생명들은 떠난 사람의 습관을 닮는다고 하더군요.”";
            dialogue += L"\n앞으로 어떤 마음으로 살아가고 싶나요?";
        }

        ClearInput();

        return true;
    }

    void Game::ChooseEmotion(Emotion emotion)
    {
        Actor* base = scene.Find(conversation);
        auto* actor = dynamic_cast<WorldActor*>(base);

        if (!actor || !actor->visible || dialogue.empty() || choiceMade || Dead())
        {
            return;
        }

        if (actor->kind == Kind::Citizen)
        {
            const auto* npc = society.Find(actor->memory);
            if (!npc || npc->health <= 0 || npc->lastTalkLife == life.id)
            {
                return;
            }
        }

        if (emotion == Emotion::Hope)
        {
            ++life.hope;
        }

        if (emotion == Emotion::Fear)
        {
            ++life.fear;
        }

        if (emotion == Emotion::Anger)
        {
            ++life.anger;
        }

        if (actor->kind == Kind::Citizen)
        {
            auto& npc = society.citizens.at(actor->memory);
            if (npc.health <= 0)
            {
                return;
            }
            npc.lastTalkLife = life.id;
            if (npc.originLife)
            {
                memories.at(npc.originLife).lastTalkLife = life.id;
            }
        }
        else if (actor->kind == Kind::Heir)
        {
            memories.at(actor->memory).lastTalkLife = life.id;
        }
        else
        {
            regions[actor->chunk].lastTalkLife = life.id;
        }

        choiceMade = true;
        Notify(L"이번 삶에 “" + EmotionName(emotion) + L"”을 기록했습니다.");
        dialogue += L"\n당신은 “" + EmotionName(emotion) + L"”을 이야기했습니다. 이 선택이 삶에 기록됩니다.";
        dirty = true;
        Save();
    }

    void Game::Trade(bool buy)
    {
        if (Dead() || inputBlocked)
        {
            return;
        }
        auto* actor = conversation ? dynamic_cast<WorldActor*>(scene.Find(conversation)) : nullptr;
        if (!conversation)
        {
            // B/N should choose a merchant, not a closer campfire or another profession.
            float best = 110 * 110;
            for (Actor* base : scene.Snapshot())
            {
                auto* candidate = dynamic_cast<WorldActor*>(base);
                const auto* npc =
                    candidate && candidate->kind == Kind::Citizen ? society.Find(candidate->memory) : nullptr;
                if (!npc || npc->profession != Profession::Merchant || npc->health <= 0 ||
                    society.IsHostile(npc->id) || !society.IsAtMarket(*npc))
                {
                    continue;
                }
                const float distance = DistanceSquared(hero->position, candidate->WorldPosition());
                if (distance <= best && ClearPath(hero->position, candidate->WorldPosition()))
                {
                    actor = candidate;
                    best = distance;
                }
            }
        }
        if (!actor || actor->kind != Kind::Citizen ||
            DistanceSquared(actor->WorldPosition(), hero->position) > 110 * 110 ||
            !ClearPath(hero->position, actor->WorldPosition()))
        {
            Notify(L"노점의 금빛 상인에게 가까이 가서 E로 대상을 확인하세요.");
            return;
        }
        if (society.Trade(*this, actor->memory, buy))
        {
            if (!dialogue.empty())
            {
                dialogue = society.Describe(actor->memory);
            }
            Save();
        }
    }

    void Game::Give()
    {
        if (Dead() || !dialogue.empty())
        {
            return;
        }

        WorldActor* actor = Nearest();

        if (actor && actor->kind == Kind::Citizen)
        {
            if (society.Give(*this, actor->memory))
            {
                Save();
            }
            return;
        }

        if (!actor || (actor->kind != Kind::Heir && actor->kind != Kind::Settler))
        {
            return;
        }

        std::uint64_t& last = actor->kind == Kind::Heir ? memories.at(actor->memory).lastGiftLife
                                                        : regions[actor->chunk].lastGiftLife;

        if (last == life.id)
        {
            Notify(L"이 삶에서 이미 마음을 나누었습니다. 그 기억은 남아 있습니다.");
            return;
        }

        if (supplies < 1)
        {
            Notify(L"나눌 물자가 없습니다. 길 주변의 물자를 찾아보세요.");
            return;
        }

        --supplies;
        ++life.gifts;
        ++life.hope;
        last = life.id;

        if (actor->kind == Kind::Heir)
        {
            ++memories.at(actor->memory).bond;
        }
        else
        {
            ++regions[actor->chunk].aid;
        }

        Notify(L"물자를 나누었습니다. 이 관계와 나눔의 성향은 삶을 넘어 남습니다.");
        dirty = true;
        Save();
    }

} // namespace Ashen
