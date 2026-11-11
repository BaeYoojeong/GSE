#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    float Game::MaxHealth() const
    {
        return 100.0f + (growth.level - 1) * 12 + (chapter.Complete() ? 20 : 0);
    }

    float Game::ShotDamage() const
    {
        // Keep growth meaningful without outscaling the fixed 72/90 HP overworld enemies.
        // The strongest staff at Lv.30/+20 remains below half a full-health enemy's HP.
        return DamageForWeapon(inventory.Equipped());
    }

    float Game::ShotCooldown() const
    {
        // Damage and attack speed both increase DPS, so neither bonus can grow unbounded.
        return (std::max)(.36f, .72f / (1 + (growth.level - 1) * .02f + growth.weapon * .02f));
    }

    float Game::PickupRange() const
    {
        return growth.magnetSeconds > 0 ? 320.0f : 100.0f;
    }

    int Game::ExperienceNeeded() const
    {
        return 35 + (growth.level - 1) * 15;
    }

    void Game::AddExperience(int amount)
    {
        if (amount <= 0 || growth.level >= MaxLevel)
        {
            return;
        }

        growth.experience += amount;

        while (growth.level < MaxLevel && growth.experience >= ExperienceNeeded())
        {
            growth.experience -= ExperienceNeeded();
            ++growth.level;
            growth.lessons |= 4u;

            // Preserve damage already taken; the extra maximum health is immediately usable.
            health = (std::min)(MaxHealth(), health + 12);
            hero->flash = .7f;
            Notify(L"성장 · Lv." + std::to_wstring(growth.level) + L"  체력·피해량 증가 / 발사 간격 감소");
        }

        if (growth.level == MaxLevel)
        {
            growth.experience = 0;
        }

        dirty = true;
    }

    void Game::SpawnLoot(ChunkKey key, int source)
    {
        const Region* region = RegionAt(key);
        const auto parent = chunks.find(key);

        if (!region || parent == chunks.end() || source < 0 || source >= 8)
        {
            return;
        }

        const bool available = source < 6 ? (region->harvested & (1u << source)) != 0
                                          : (region->defeated & (1u << (source - 6))) != 0;

        if (!available)
        {
            return;
        }

        const Vec2 position = source < 6 ? FarmingPosition(key, source) : region->enemyLoot[source - 6];
        const ItemKind bonuses[] = {ItemKind::Weapon, ItemKind::Healing, ItemKind::Magnet};
        const ItemKind items[] = {ItemKind::Experience, bonuses[source % 3]};

        for (int i = 0; i < 2; ++i)
        {
            const unsigned type = static_cast<unsigned>(items[i]);
            const int slot = source * 4 + static_cast<int>(type);

            if (region->lootCollected & (std::uint32_t{1} << slot))
            {
                continue;
            }

            auto& drop = Spawn(Kind::Drop, key, parent->second, position, slot);
            drop.variant = type;
            drop.cooldown = .4f;
        }
    }

    void Game::CollectDrop(WorldActor& drop)
    {
        Region& region = regions[drop.chunk];
        const std::uint32_t bit = std::uint32_t{1} << drop.slot;

        if (region.lootCollected & bit)
        {
            scene.Destroy(drop.Id());
            return;
        }

        // Full potion stacks leave the drop in the world rather than destroying its contents.
        if (static_cast<ItemKind>(drop.variant) == ItemKind::Healing &&
            !inventory.CanAdd(ItemId::HealingPotion))
        {
            return;
        }
        region.lootCollected |= bit;
        growth.itemsSeen |= 1u << drop.variant;

        switch (static_cast<ItemKind>(drop.variant))
        {
        case ItemKind::Weapon:
            if (growth.weapon < MaxWeapon)
            {
                ++growth.weapon;
                Notify(L"무기 강화 +" + std::to_wstring(growth.weapon) + L" · 피해량 증가 / 발사 간격 감소");
            }
            else
            {
                AddExperience(20);
                Notify(L"무기 강화 최대치 · 강화 재료를 경험치 20으로 전환했습니다.");
            }

            break;

        case ItemKind::Healing:
            inventory.Add(ItemId::HealingPotion);
            Notify(L"회복 물약 +1 · I 인벤토리 / H 사용");
            break;

        case ItemKind::Experience:
            AddExperience(15);
            break;

        case ItemKind::Magnet:
            growth.magnetSeconds = (std::min)(60.0f, growth.magnetSeconds + 20);
            Notify(L"자석 부적 · 습득 범위 320, 지속시간 +20초");
            break;
        }

        scene.Destroy(drop.Id());
        dirty = true;
    }

    bool Game::LevelOneComplete() const
    {
        return (growth.lessons & 7u) == 7u && growth.itemsSeen == 15u;
    }

    std::wstring Game::Lesson() const
    {
        if (!(growth.lessons & 1u))
        {
            return L"레벨 1 · 1/4  남쪽 들판의 약초에 다가가 E로 채집하세요";
        }

        if (!(growth.lessons & 2u))
        {
            return L"레벨 1 · 2/4  적 가까이에서 Space 유지 · 자동 조준 원거리 공격";
        }

        if (!(growth.lessons & 4u))
        {
            return L"레벨 1 · 3/4  푸른 보석을 모아 Lv.2로 성장하세요";
        }

        if (growth.itemsSeen != 15u)
        {
            return L"레벨 1 · 4/4  채집을 계속하며 강화·회복·경험치·자석을 모두 습득하세요";
        }

        return chapter.Objective();
    }
} // namespace Ashen
