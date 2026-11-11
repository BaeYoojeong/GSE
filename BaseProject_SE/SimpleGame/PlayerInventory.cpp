#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <limits>

namespace Ashen
{
    std::uint64_t Game::Shopkeeper(ActorId merchantActor) const
    {
        const auto* actor = dynamic_cast<const WorldActor*>(scene.Find(merchantActor));
        if (!hero || Dead() || !actor || actor->kind != Kind::Citizen || !actor->IsActive())
        {
            return 0;
        }
        const auto* npc = society.Find(actor->memory);
        if (!npc || npc->profession != Profession::Merchant || npc->health <= 0 ||
            npc->journeyKind != JourneyKind::None || society.IsHostile(npc->id) ||
            !society.IsAtMarket(*npc) ||
            DistanceSquared(hero->position, actor->WorldPosition()) > 110 * 110 ||
            !ClearPath(hero->position, actor->WorldPosition()))
        {
            return 0;
        }
        return npc->id;
    }

    bool Game::BuyItem(ActorId shop, ItemId item)
    {
        const auto merchant = Shopkeeper(shop);
        if (tradingMerchant != shop || !merchant)
        {
            Notify(L"시장의 상인 NPC에게 가까이 가서 E로 대화하세요.");
            return false;
        }
        if (!storageHealthy)
        {
            Notify(L"저장 상태를 복구한 뒤 다시 거래하세요. F8 저장 재시도");
            return false;
        }
        if (!society.PurchaseItem(*this, merchant, item))
        {
            return false;
        }
        if (!Save())
        {
            Notify(L"구매했지만 저장에 실패했습니다. F8로 저장을 다시 시도하세요.");
        }
        return true;
    }

    bool Game::SellSupplies(ActorId shop)
    {
        const auto merchant = Shopkeeper(shop);
        if (tradingMerchant != shop || !merchant || !storageHealthy)
        {
            Notify(L"판매 가능한 상점과 저장 상태를 확인하세요.");
            return false;
        }
        if (!society.SellSupplies(*this, merchant))
        {
            return false;
        }
        if (!Save())
        {
            Notify(L"판매했지만 저장에 실패했습니다. F8로 저장을 다시 시도하세요.");
        }
        return true;
    }

    bool Game::UseItem(ItemId itemId)
    {
        const auto* item = FindItem(itemId);
        if (!item || Dead())
        {
            return false;
        }
        if (item->category == ItemCategory::Weapon)
        {
            if (!inventory.Equip(itemId))
            {
                Notify(L"아직 갖고 있지 않은 무기입니다.");
                return false;
            }
            Notify(std::wstring(item->name) + L" 장착 완료");
        }
        else if (itemId == ItemId::HealingPotion)
        {
            if (health >= MaxHealth() || healingCooldown > 0 || inventory.Quantity(itemId) == 0)
            {
                Notify(health >= MaxHealth() ? L"체력이 가득 차 있어 물약을 사용하지 않았습니다."
                       : healingCooldown > 0 ? L"물약 재사용 대기 중입니다."
                                             : L"회복 물약이 없습니다. 상점에서 구매하세요.");
                return false;
            }
            inventory.ConsumePotion();
            health = (std::min)(MaxHealth(), health + MaxHealth() * .35f);
            healingCooldown = 2;
            Notify(L"회복 물약 사용 · 최대 체력의 35% 회복");
        }
        else
        {
            Notify(L"물자는 불씨 복구·봉헌·나눔에 사용하거나 상점에서 N으로 판매합니다.");
            return false;
        }
        dirty = true;
        if (!Save())
        {
            Notify(L"아이템을 사용했지만 저장에 실패했습니다. F8로 다시 시도하세요.");
        }
        return true;
    }

    float Game::DamageForWeapon(ItemId item) const
    {
        const auto* definition = FindItem(item);
        return definition && definition->category == ItemCategory::Weapon
                   ? definition->attack + (growth.level - 1) * .25f + growth.weapon * .45f
                   : 0;
    }
} // namespace Ashen
