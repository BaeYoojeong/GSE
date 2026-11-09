#include "ChapterTwo.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>

namespace Ashen
{
    ChapterTwo::Stage ChapterTwo::CurrentStage() const
    {
        if (!(flags & 1u))
        {
            return Stage::Tutorial;
        }
        if (!(flags & 2u))
        {
            return Stage::PilgrimRoad;
        }
        if (!(flags & 4u))
        {
            return Stage::Offering;
        }
        if (!(flags & 8u))
        {
            return Stage::BellKeeper;
        }
        if (!(flags & 16u))
        {
            return Stage::RingBell;
        }
        return Stage::Complete;
    }

    bool ChapterTwo::Complete() const
    {
        return (flags & 16u) != 0;
    }

    float ChapterTwo::BossMaximumHealth() const
    {
        return bossMaximumHealth;
    }

    Location ChapterTwo::Road()
    {
        return {{0, -2}, {384, 384}};
    }

    Location ChapterTwo::Altar()
    {
        return {{0, -4}, {384, 510}};
    }

    Location ChapterTwo::Arena()
    {
        return {{0, -4}, {384, 650}};
    }

    void ChapterTwo::Update(Game& game)
    {
        if (!(flags & 1u) && game.LevelOneComplete())
        {
            flags |= 1u;
            game.dirty = true;
            game.Notify(L"레벨 2 시작 · 꺼진 종탑. 북쪽 순례길의 불씨를 복구하세요. Q 원정 기록");
            RenderProfiler::Event("chapter_unlocked", "chapter=2");
        }
        if ((flags & 1u) && !(flags & 2u) && game.Lit(Road().chunk))
        {
            flags |= 2u;
            game.dirty = true;
            game.Notify(L"순례길이 열렸습니다. 옛 수도원 종탑으로 이동하세요 · 물자 6개 준비");
            RenderProfiler::Event("chapter_road_restored", "chapter=2 chunk=0,-2");
        }
    }

    const WorldActor* ChapterTwo::Boss(const Game& game) const
    {
        for (Actor* base : game.scene.Snapshot())
        {
            const auto* actor = dynamic_cast<const WorldActor*>(base);
            if (actor && actor->kind == Kind::BellKeeper && actor->IsActive())
            {
                return actor;
            }
        }
        return nullptr;
    }

    void ChapterTwo::Spawn(Game& game, ChunkKey chunk, ActorId parent)
    {
        if (!(chunk == Altar().chunk))
        {
            return;
        }
        bool hasAltar = false, hasBoss = false;
        for (Actor* base : game.scene.Snapshot())
        {
            const auto* actor = dynamic_cast<const WorldActor*>(base);
            if (!actor || !(actor->chunk == chunk))
            {
                continue;
            }
            hasAltar |= actor->kind == Kind::BellAltar;
            hasBoss |= actor->kind == Kind::BellKeeper;
        }
        if (!hasAltar)
        {
            game.Spawn(Kind::BellAltar, chunk, parent, Altar().local);
        }
        if (!hasBoss && CurrentStage() == Stage::BellKeeper)
        {
            auto& boss = game.Spawn(Kind::BellKeeper, chunk, parent, Arena().local);
            boss.health = bossMaximumHealth;
            boss.cooldown = 2.5f;
            RenderProfiler::Event("chapter_boss_spawn", "chapter=2 hp=" + std::to_string(boss.health));
        }
    }

    bool ChapterTwo::Interact(Game& game)
    {
        Update(game);
        switch (CurrentStage())
        {
        case Stage::Tutorial:
            game.Notify(L"종탑의 글귀: 먼저 채집·전투·성장·도구의 네 가지 생존 학습을 마치세요.");
            return false;
        case Stage::PilgrimRoad:
            game.Notify(L"꺼진 종: 돌아갈 길의 불씨부터 밝혀야 합니다. 순례길 (0,-2), 물자 3개.");
            return false;
        case Stage::Offering:
            if (game.supplies < 6)
            {
                game.Notify(L"종탑의 봉인을 열려면 물자 6개가 필요합니다. 채집·사냥·상인 구매로 준비하세요.");
                return false;
            }
            game.supplies -= 6;
            flags |= 4u;
            // Snapshot strength once, so collecting upgrades cannot change a live boss's maximum HP.
            bossMaximumHealth = (std::clamp)(game.ShotDamage() / game.ShotCooldown() * 10, 480.0f, 20000.0f);
            if (const auto parent = game.chunks.find(Altar().chunk); parent != game.chunks.end())
            {
                Spawn(game, parent->first, parent->second);
            }
            game.immunity = (std::max)(game.immunity, 3.0f);
            game.Notify(L"봉인이 풀렸습니다. 잿빛 종지기 · 바닥의 붉은 예고를 피하며 Space로 공격하세요.");
            RenderProfiler::Event("chapter_offering",
                                  "chapter=2 spent=6 boss_hp=" + std::to_string(bossMaximumHealth));
            break;
        case Stage::BellKeeper:
            game.Notify(L"잿빛 종지기를 쓰러뜨리세요. 붉은 원은 충격파, 작은 원은 낙하 공격의 예고입니다.");
            return false;
        case Stage::RingBell:
            // Completion and rewards enter the same atomic world save; the bit prevents replay grants.
            flags |= 16u;
            game.society.playerCoins = (std::min)(1000000, game.society.playerCoins + 50);
            if (game.growth.weapon < Game::MaxWeapon)
            {
                ++game.growth.weapon;
            }
            game.AddExperience(100);
            game.health = game.MaxHealth();
            game.regions[Altar().chunk].lit = true;
            game.checkpoint = {Altar().chunk, {384, 428}};
            ++game.life.hope;
            game.Notify(L"종탑 복구 · 영구 최대 체력 +20, 동전 +50, 경험치 +100, 무기 +1(상한 적용)");
            RenderProfiler::Event("chapter_completed", "chapter=2 reward=bell_blessing checkpoint=0,-4");
            break;
        case Stage::Complete:
            game.checkpoint = {Altar().chunk, {384, 428}};
            game.Notify(L"다시 울리는 종. 이 불씨에서 다음 삶을 시작합니다. 복구와 가호는 계속 남습니다.");
            break;
        }
        game.dirty = true;
        return true;
    }

    void ChapterTwo::DamageBoss(Game& game, WorldActor& boss, float amount)
    {
        if (CurrentStage() != Stage::BellKeeper || !boss.IsActive() || !std::isfinite(amount) || amount <= 0)
        {
            return;
        }
        boss.health = (std::max)(0.0f, boss.health - amount);
        boss.flash = .18f;
        if (boss.health > 0)
        {
            return;
        }
        flags |= 8u;
        ++game.life.kills;
        game.scene.Destroy(boss.Id());
        game.dirty = true;
        game.Notify(L"잿빛 종지기가 쓰러졌습니다. 종탑 앞에서 E를 눌러 종을 울리세요.");
        RenderProfiler::Event("chapter_boss_defeated", "chapter=2 life=" + std::to_string(game.life.id));
        game.Save();
    }

    void ChapterTwo::ResetBattle(Game& game, const char* reason)
    {
        if (CurrentStage() != Stage::BellKeeper)
        {
            return;
        }
        for (Actor* base : game.scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);
            if (!actor || actor->kind != Kind::BellKeeper)
            {
                continue;
            }
            actor->health = bossMaximumHealth;
            actor->windup = actor->attackVisualSeconds = 0;
            actor->cooldown = 2.5f;
        }
        RenderProfiler::Event("chapter_boss_reset", std::string("chapter=2 reason=") + reason);
    }

    Location ChapterTwo::Destination() const
    {
        return CurrentStage() == Stage::PilgrimRoad  ? Road()
               : CurrentStage() == Stage::BellKeeper ? Arena()
                                                     : Altar();
    }

    std::wstring ChapterTwo::Objective() const
    {
        switch (CurrentStage())
        {
        case Stage::Tutorial:
            return L"레벨 1의 생존 학습을 마치면 꺼진 종탑 원정이 열립니다.";
        case Stage::PilgrimRoad:
            return L"레벨 2 · 1/4 순례길 (0,-2) 불씨 복구 · E / 물자 3 · Q 기록";
        case Stage::Offering:
            return L"레벨 2 · 2/4 옛 수도원 (0,-4) 종탑 봉인 열기 · E / 물자 6";
        case Stage::BellKeeper:
            return L"레벨 2 · 3/4 잿빛 종지기 격파 · 붉은 예고 회피 / Space 공격";
        case Stage::RingBell:
            return L"레벨 2 · 4/4 종탑 앞에서 E · 불씨와 종소리를 되살리세요";
        default:
            return L"레벨 2 완료 · 종탑의 가호: 최대 체력 +20 · 자유 탐험 / Q 원정 기록";
        }
    }

    std::wstring ChapterTwo::Direction(const Game& game) const
    {
        const Location target = Destination();
        const Vec2 point = game.Relative(target);
        const double dx = static_cast<double>(point.x) - game.hero->position.x;
        const double dy = static_cast<double>(point.y) - game.hero->position.y;
        const double x = CameraCos * dx - CameraSin * dy;
        const double y = CameraDepth * (CameraSin * dx + CameraCos * dy);
        const wchar_t* arrows[] = {L"→", L"↘", L"↓", L"↙", L"←", L"↖", L"↑", L"↗"};
        const int sector = static_cast<int>(std::floor(std::atan2(y, x) / .785398163 + .5));
        const auto steps = static_cast<unsigned long long>((std::min)(1000000000.0, std::hypot(dx, dy) / 32));
        const std::wstring name = CurrentStage() == Stage::PilgrimRoad ? L"순례길 불씨" : L"옛 수도원 종탑";
        return name + L" " + arrows[(sector + 8) % 8] + L" · 약 " + std::to_wstring((steps / 5) * 5) +
               L"걸음 · 세계 좌표 (" + std::to_wstring(target.chunk.x) + L"," +
               std::to_wstring(target.chunk.y) + L") · Q 원정 기록";
    }

    std::wstring ChapterTwo::Journal() const
    {
        const auto line = [&](unsigned bit, const wchar_t* text)
        { return std::wstring((flags & bit) ? L"[완료] " : L"[  ] ") + text + L"\n"; };
        return std::wstring(L"꺼진 종탑 · 죽음 뒤에도 길을 잃지 않도록\n\n") +
               L"북쪽 옛 수도원의 종은 길을 잃은 이들을 불빛으로 인도했습니다.\n" +
               L"사라진 종소리를 되찾아 다음 생명이 돌아올 자리를 만드세요.\n\n" +
               line(1, L"레벨 1: 생존 학습 완료") + line(2, L"순례길 (0,-2): 불씨 복구 · E / 물자 3") +
               line(4, L"옛 수도원 (0,-4): 종탑에 물자 6개 바치기") +
               line(8, L"잿빛 종지기: 예고를 피하고 Space로 격파") +
               line(16, L"종 울리기: E / 가호와 새 부활 거점 획득") +
               L"\n보상: 영구 최대 체력 +20 · 동전 50 · 경험치 100 · 무기 +1(상한 적용)\n" +
               L"죽어도 복구·봉인 해제·보스 처치는 남습니다. 생명을 바칠 필요는 없습니다.\n" +
               L"전투 중 사망·멀리 이탈·재실행 시 보스 체력은 회복됩니다. 물자는 다시 내지 않습니다.";
    }

    bool ChapterTwo::Read(std::istream& in)
    {
        std::string marker;
        if (!(in >> marker >> flags >> bossMaximumHealth) || marker != "CHAPTER_TWO_V1" ||
            (flags != 0 && flags != 1 && flags != 3 && flags != 7 && flags != 15 && flags != 31) ||
            !std::isfinite(bossMaximumHealth) || bossMaximumHealth < 480 || bossMaximumHealth > 20000)
        {
            return false;
        }
        return true;
    }

    void ChapterTwo::Write(std::ostream& out) const
    {
        out << "CHAPTER_TWO_V1 " << flags << ' ' << bossMaximumHealth << '\n';
    }
} // namespace Ashen
