#include "WorldScene.h"
#include "DarkAgeGame.h"
#include "DarkAgeRenderer.h"
#include "PostProcessing.h"
#include "AssetCatalog.h"
#include "RenderProfiler.h"
#include <algorithm>
#include "WindowInput.h"
#include <sstream>

namespace
{
    Ashen::Game game;
    bool ready = false;
    bool journal = false;
    bool census = false;
    bool objectives = false;
    bool atlas = false;
    int targetTown = -1;
    unsigned censusPage = 0;
    bool stressRun = false;
    const AshenRenderer::Color PanelColor = {.035f, .045f, .055f, .92f};
    const AshenRenderer::Color Gold = {.91f, .70f, .40f, 1};

    void Lines(float x, float y, const std::wstring& text, int size = 18)
    {
        std::wistringstream stream(text);
        std::wstring line;

        while (std::getline(stream, line))
        {
            AshenRenderer::Text(x, y, line, size);
            y += size + 10;
        }
    }

    void Interface()
    {
        using namespace AshenRenderer;
        Panel(18, 16, 330, 112, PanelColor);
        Text(30, 23,
             L"제" + std::to_wstring(game.life.id) + L"의 삶  ·  물자 " + std::to_wstring(game.supplies), 19);
        Panel(30, 56, 218, 10, {.20f, .14f, .15f, 1});
        Panel(30, 56, 218 * game.health / game.MaxHealth(), 10, {.69f, .31f, .28f, 1});
        Text(256, 50,
             std::to_wstring(static_cast<int>(game.health)) + L" / " +
                 std::to_wstring(static_cast<int>(game.MaxHealth())),
             14);
        Text(30, 77,
             L"Lv." + std::to_wstring(game.growth.level) + L"  XP " +
                 (game.growth.level == Ashen::Game::MaxLevel
                      ? std::wstring(L"MAX")
                      : std::to_wstring(game.growth.experience) + L" / " +
                            std::to_wstring(game.ExperienceNeeded())) +
                 L"  무기 +" + std::to_wstring(game.growth.weapon),
             15, Gold);
        Panel(30, 107, 296, 5, {.14f, .19f, .24f, 1});
        Panel(30, 107,
              296 * (game.growth.level == Ashen::Game::MaxLevel
                         ? 1.0f
                         : static_cast<float>(game.growth.experience) / game.ExperienceNeeded()),
              5, {.31f, .66f, .85f, 1});

        Panel(360, 16, 320, 67, PanelColor);
        Text(372, 23,
             L"피해 " + std::to_wstring(static_cast<int>(game.ShotDamage())) + L"  간격 " +
                 std::to_wstring(static_cast<int>(game.ShotCooldown() * 1000)) + L"ms  사거리 360",
             15);
        Text(372, 50,
             game.growth.magnetSeconds > 0
                 ? L"자석 " + std::to_wstring(static_cast<int>(game.growth.magnetSeconds) + 1) +
                       L"초 · 범위 320"
                 : L"가까운 아이템 자동 습득 · 범위 100",
             15, Gold);

        Panel(695, 16, 286, 67, PanelColor);
        Text(708, 23, game.LegacyDirection(), 17, Gold);
        Text(708, 50, L"남겨진 생명 " + std::to_wstring(game.memories.size()) + L"  ·  Tab 삶의 기록", 15);

        Panel(360, 90, 620, 37, PanelColor);
        Text(372, 100,
             L"동전 " + std::to_wstring(game.society.playerCoins) + L" · 악명 " +
                 std::to_wstring(game.society.notoriety) + (game.pkEnabled ? L" · PK ON" : L" · PK OFF") +
                 L" · V 주민 현황",
             16, Gold);

        Panel(18, 134, 964, 30, PanelColor);
        Text(30, 141,
             (targetTown >= 0 || !game.LevelOneComplete() || game.chapter.Complete()
                  ? game.VillageDirection(targetTown)
                  : game.chapter.Direction(game)),
             15, Gold);

        if (!game.StorageHealthy())
        {
            Panel(18, 172, 760, 32, PanelColor);
            Text(28, 178, L"저장 오류 · 기록 보호를 위해 다음 삶으로의 진행이 제한됩니다. F8 재시도", 16,
                 Gold);
        }

        if (!game.notice.empty())
        {
            Panel(30, 530, 940, 42, PanelColor);
            Text(44, 541, game.notice, 16, Gold);
        }

        if (!game.Dead() && game.dialogue.empty() && !journal && !census && !objectives && !atlas)
        {
            if (const auto* nearest = game.Nearest())
            {
                Panel(190, 580, 620, 35, PanelColor);
                Text(204, 588, game.Description(*nearest), 17, Gold);
            }
        }

        Panel(0, 637, 1000, 63, PanelColor);
        Text(22, 646,
             L"방향키 이동  C 달리기  J 점프  Space 공격  E 대화  B/N 거래  P PK  V 주민  M 지도  Q 원정",
             15);
        Text(22, 675, game.Lesson(), 14, Gold);

        if (!game.dialogue.empty())
        {
            Panel(54, 282, 892, 239, PanelColor);
            Panel(54, 282, 4, 239, Gold);
            const auto* partner =
                dynamic_cast<const Ashen::WorldActor*>(game.Scene().Find(game.conversation));
            const bool citizen = partner && partner->kind == Ashen::Kind::Citizen;
            Lines(76, 299, citizen ? game.society.Describe(partner->memory) : game.dialogue, 17);
            Text(76, 488,
                 game.choiceMade ? L"E / Esc 대화 닫기"
                                 : L"1 희망을 말한다   2 두려움을 말한다   3 분노를 말한다   E / Esc 닫기",
                 15, Gold);
        }

        if (journal)
        {
            Panel(75, 104, 850, 414, PanelColor);
            Text(100, 125, L"살아온 방식이 다음 생명을 만듭니다", 25, Gold);
            Text(100, 169,
                 L"이번 삶 · 나눔 " + std::to_wstring(game.life.gifts) + L"  /  위협 제거 " +
                     std::to_wstring(game.life.kills) + L"  /  여행 " +
                     std::to_wstring(static_cast<int>(game.life.distance / 32)) + L"걸음",
                 18);
            Text(100, 202,
                 L"마음의 기록 · 희망 " + std::to_wstring(game.life.hope) + L"  /  두려움 " +
                     std::to_wstring(game.life.fear) + L"  /  분노 " + std::to_wstring(game.life.anger),
                 18);
            Text(100, 250, L"세계에 남은 최근의 삶", 19, Gold);
            int row = 0;

            for (auto it = game.memories.rbegin(); it != game.memories.rend() && row < 5; ++it, ++row)
            {
                const auto& memory = it->second;
                Text(100, 285 + row * 30.0f,
                     L"제" + std::to_wstring(memory.life.id) + L"의 삶 → " + game.NatureName(memory.nature) +
                         L"  ·  " + game.EmotionName(memory.emotion) + L"  ·  신뢰 " +
                         std::to_wstring(memory.bond),
                     17);
            }

            if (game.memories.empty())
            {
                Text(100, 292, L"아직 남겨진 생명이 없습니다. 당신은 어떤 삶을 살게 될까요?", 17);
            }

            Text(100, 478, L"Tab / Esc 닫기 · 세계 시간은 계속 흐릅니다", 15, Gold);
        }

        if (census && !game.Dead())
        {
            Panel(54, 145, 892, 470, PanelColor);
            Text(76, 159, game.society.Summary(), 20, Gold);
            Text(76, 195,
                 L"7개 마을 · 녹색 사냥꾼 / 붉은색 약탈자 / 금색 상인 / 청색 구도자 · 편지 표식: 전령·이사",
                 15);
            Lines(76, 224, game.society.Ledger(censusPage), 16);
            if (!game.society.recentEvents.empty())
            {
                Text(76, 457, game.society.recentEvents.front(), 16, Gold);
            }
            Text(76, 495,
                 L"현재 페이지 " + std::to_wstring(censusPage % game.society.PageCount() + 1) + L"/" +
                     std::to_wstring(game.society.PageCount()) + L" · [ / ] 이전·다음",
                 16);
            Text(76, 532, L"주민은 화면 밖·대화·기록 열람 중에도 생활합니다. 안전한 곳에서 살펴보세요.", 16);
            Text(76, 570, L"V / Esc 닫기 · 주민은 즉시 부활합니다 · 부활 직후 3초 보호", 16, Gold);
        }

        if (objectives && !game.Dead())
        {
            Panel(54, 170, 892, 440, PanelColor);
            Lines(76, 185, game.chapter.Journal(), 15);
            Text(76, 580, L"Q / Esc 닫기 · 세계 시간은 계속 흐릅니다", 15, Gold);
        }
        if (atlas && !game.Dead())
        {
            Panel(54, 170, 892, 440, PanelColor);
            Text(76, 188, L"일곱 마을 · 숫자 1~7을 눌러 목적지 선택", 22, Gold);
            for (unsigned i = 0; i < Ashen::TownWorld::Count; ++i)
            {
                const auto& town = Ashen::TownWorld::Towns()[i];
                Text(76, 235 + i * 38.0f,
                     std::to_wstring(i + 1) + L". " + town.name + L" · (" + std::to_wstring(town.center.x) +
                         L"," + std::to_wstring(town.center.y) + L")",
                     18);
            }
            Text(76, 498, game.PlanetStatus(), 13);
            Text(76, 524, L"이웃 마을 중심 사이 약 20초 · 기본 보행 기준 · 길을 따라 자유롭게 이동", 16);
            Text(76, 564, L"0 원정/가까운 마을 안내 복귀 · M / Esc 닫기 · 세계 시간은 계속 흐릅니다", 15,
                 Gold);
        }
        if (game.Dead())
        {
            Panel(0, 0, 1000, 700, {.02f, .025f, .04f, .72f});
            Panel(150, 192, 700, 298, PanelColor);
            Text(190, 223, L"한 생명이 끝나고, 다른 생명이 시작됩니다", 27, Gold);
            const auto* memory = game.MemoryAt(game.life.id);

            if (memory)
            {
                Text(190, 282, L"당신의 삶은 “" + game.NatureName(memory->nature) + L"”에게 전승되었습니다.",
                     20);
                Text(190, 321,
                     L"남겨진 마음: " + game.EmotionName(memory->emotion) + L" · 이 생명은 세계에 남습니다.",
                     18);
            }

            Text(190, 373, L"R 새 여행자로 불씨에서 깨어나기", 21, Gold);
            Text(190, 419,
                 game.StorageHealthy() ? L"성장·무기 강화·인연·되살린 불씨는 다음 삶에도 이어집니다."
                                       : L"기록 저장 실패 · 저장 위치를 확인한 뒤 R로 재시도하세요.",
                 16);
        }
    }
} // namespace

bool WorldScene::Initialize(bool benchmark, bool stress)
{
    journal = objectives = atlas = false;
    targetTown = -1;
    census = false;
    censusPage = 0;

    if (!AshenRenderer::Initialize())
    {
        return false;
    }

    stressRun = stress;
    game.headless = benchmark && !stress;
    ready = game.Initialize(benchmark ? 20261110 : 0);
    if (benchmark)
    {
        if (RenderProfiler::Environment("ASHEN_BENCHMARK_WORKERS") != "1")
        {
            game.farSimulation.reset();
        }
    }
    return ready;
}

void WorldScene::Shutdown()
{
    game.Shutdown();
    AshenRenderer::Shutdown();
    ready = false;
}

void WorldScene::Render()
{
    if (!ready)
    {
        return;
    }

    AshenRenderer::World(game);
    Interface();
    AshenRenderer::EndFrame();
}

void WorldScene::Update(float seconds)
{
    if (ready)
    {
        game.inputBlocked = journal || census || objectives || atlas;
        game.Update(seconds);
        if (game.Dead())
        {
            journal = census = objectives = atlas = false;
            game.inputBlocked = false;
        }
    }
}

void WorldScene::Resize(int width, int height)
{
    AshenRenderer::Resize(width, height);
}

void WorldScene::ClearInput()
{
    game.ClearInput();
}

void WorldScene::Arrow(SpecialKey key, bool down)
{
    if (!ready)
    {
        return;
    }

    if (down)
    {
        auto& fx = PostProcessing::Options();

        switch (key)
        {
        case SpecialKey::F1:
            fx.enabled = !fx.enabled;
            game.Notify(fx.enabled ? L"Bloom · 비넷 · 가장자리 블러 켜짐"
                                   : L"연출 효과 꺼짐 · HDR 톤 매핑은 유지합니다.");
            return;
        case SpecialKey::F2:
            fx.bloom = !fx.bloom;
            game.Notify(fx.bloom ? L"Bloom 켜짐" : L"Bloom 꺼짐 · 광원의 중심부만 남습니다.");
            return;
        case SpecialKey::F3:
            fx.vignette = !fx.vignette;
            game.Notify(fx.vignette ? L"비넷 켜짐" : L"비넷 꺼짐");
            return;
        case SpecialKey::F4:
            fx.edgeBlur = !fx.edgeBlur;
            game.Notify(fx.edgeBlur ? L"가장자리 블러 켜짐" : L"가장자리 블러 꺼짐");
            return;
        case SpecialKey::F5:
        case SpecialKey::F6:
            fx.exposure =
                (std::max)(.25f, (std::min)(4.0f, fx.exposure + (key == SpecialKey::F5 ? -.05f : .05f)));
            game.Notify(L"HDR 노출 " + std::to_wstring(static_cast<int>(fx.exposure * 100 + .5f)) + L"%");
            return;
        case SpecialKey::F7:
            PostProcessing::Reset();
            game.Notify(L"HDR · Bloom · 비넷 · 가장자리 블러 기본값을 복원했습니다.");
            return;
        case SpecialKey::F10:
            AssetCatalog::ExportBlender();
            game.Notify(L"Blender 자산 내보내기를 요청했습니다. 완료 후 자동 반영됩니다.");
            return;
        case SpecialKey::F9:
            AssetCatalog::Reload();
            game.Notify(AssetCatalog::Status());
            return;
        case SpecialKey::F8:
            game.Notify(game.Save() ? L"삶과 세계의 기록을 저장했습니다."
                                    : L"저장 실패 · 저장 폴더를 확인하세요.");
            return;
        }
    }

    if (key >= SpecialKey::Left && key <= SpecialKey::Down)
    {
        game.arrows[static_cast<unsigned>(key) - static_cast<unsigned>(SpecialKey::Left)] =
            down && !journal && !census && !objectives && !atlas && game.dialogue.empty();
    }
}

void WorldScene::Key(unsigned char key, bool down)
{
    if (!ready)
    {
        return;
    }

    if (key == 'c' || key == 'C')
    {
        game.sprint = down && !game.inputBlocked && !journal && !census && !objectives && !atlas &&
                      game.dialogue.empty();
        return;
    }
    if (key == 'j' || key == 'J')
    {
        if (down && !game.inputBlocked && !journal && !census && !objectives && !atlas &&
            game.dialogue.empty())
        {
            game.jumpRequested = true;
        }
        return;
    }
    if (key == ' ')
    {
        game.attack = down && !journal && !census && !objectives && !atlas && game.dialogue.empty();
        return;
    }

    if (!down)
    {
        return;
    }

    if ((key == 'q' || key == 'Q' || key == 'm' || key == 'M') && !game.Dead() && game.dialogue.empty())
    {
        const bool quest = key == 'q' || key == 'Q';
        const bool open = quest ? !objectives : !atlas;
        journal = census = objectives = atlas = false;
        if (quest)
        {
            objectives = open;
        }
        else
        {
            atlas = open;
        }
        game.inputBlocked = open;
        game.ClearInput();
        return;
    }
    if (atlas && key >= '0' && key <= '7')
    {
        targetTown = static_cast<int>(key - '1');
        atlas = false;
        game.inputBlocked = false;
        return;
    }
    if (key == '\t' && !game.Dead() && game.dialogue.empty() && !census)
    {
        objectives = atlas = false;
        journal = !journal;
        game.ClearInput();
        return;
    }

    if (key == 27)
    {
        journal = census = objectives = atlas = false;
        game.inputBlocked = false;

        if (!game.dialogue.empty())
        {
            game.Interact();
        }

        game.ClearInput();
        return;
    }

    if ((key == 'v' || key == 'V') && !game.Dead() && game.dialogue.empty())
    {
        objectives = atlas = false;
        census = !census;
        journal = objectives = atlas = false;
        game.inputBlocked = census;
        game.ClearInput();
        return;
    }
    if (census && (key == '[' || key == ']'))
    {
        const unsigned pages = game.society.PageCount();
        censusPage = (censusPage + (key == ']' ? 1 : pages - 1)) % pages;
        return;
    }
    if (journal || census || objectives || atlas)
    {
        return;
    }
    if (key == 'p' || key == 'P')
    {
        game.TogglePK();
    }
    if (key == 'b' || key == 'B' || key == 'n' || key == 'N')
    {
        game.Trade(key == 'b' || key == 'B');
    }

    if (key == 'e' || key == 'E')
    {
        game.Interact();
    }

    if (key == 'g' || key == 'G')
    {
        game.Give();
    }

    if (key == 'r' || key == 'R')
    {
        game.ContinueLife();
    }

    if (key >= '1' && key <= '3')
    {
        game.ChooseEmotion(static_cast<Ashen::Emotion>(key - '1'));
    }
}

void WorldScene::BenchmarkStep(unsigned frame)
{
    game.health = game.MaxHealth();
    game.ClearInput();
    game.arrows[frame / 180 % 4] = !stressRun;
    game.Update(1.f / 60.f);
    if (stressRun)
    {
        game.BenchmarkTraverse(frame);
    }
}

WorldScene::InputState WorldScene::InspectInput()
{
    return {{{game.arrows[0], game.arrows[1], game.arrows[2], game.arrows[3]}},
            game.attack,
            game.sprint,
            game.jumpRequested};
}

double WorldScene::SimulationTime()
{
    return game.society.simulationSeconds;
}
