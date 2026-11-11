#include "ItemCatalog.h"

namespace Ashen
{
    const std::array<ItemDefinition, ItemTypeCount>& Items()
    {
        static const std::array<ItemDefinition, ItemTypeCount> items = {
            {{ItemId::TravelerStaff, L"여행자의 지팡이", L"처음부터 가진 원거리 무기", ItemCategory::Weapon,
              0, 16, 1},
             {ItemId::IronStaff, L"철제 지팡이", L"여행자의 지팡이보다 강한 무기", ItemCategory::Weapon, 18,
              18, 1},
             {ItemId::EmberStaff, L"잿불 지팡이", L"불씨의 힘을 담은 상급 무기", ItemCategory::Weapon, 60,
              19.5f, 1},
             {ItemId::HealingPotion, L"회복 물약", L"사용하면 최대 체력의 35% 회복", ItemCategory::Consumable,
              6, 0, 99},
             {ItemId::Supplies, L"물자", L"불씨 복구·봉헌·나눔에 사용", ItemCategory::Resource, 3, 0, 99}}};
        return items;
    }

    const ItemDefinition* FindItem(ItemId id)
    {
        const auto index = static_cast<std::size_t>(id);
        return index < Items().size() ? &Items()[index] : nullptr;
    }
} // namespace Ashen
