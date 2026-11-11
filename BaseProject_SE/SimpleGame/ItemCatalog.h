#pragma once

#include <array>
#include <cstddef>

namespace Ashen
{
    enum class ItemId : unsigned
    {
        TravelerStaff,
        IronStaff,
        EmberStaff,
        HealingPotion,
        Supplies,
        Count
    };

    enum class ItemCategory
    {
        Weapon,
        Consumable,
        Resource
    };

    struct ItemDefinition
    {
        ItemId id;
        const wchar_t* name;
        const wchar_t* description;
        ItemCategory category;
        int price;
        float attack;
        unsigned stackLimit;
    };

    constexpr std::size_t ItemTypeCount = static_cast<std::size_t>(ItemId::Count);
    const std::array<ItemDefinition, ItemTypeCount>& Items();
    const ItemDefinition* FindItem(ItemId id);
} // namespace Ashen
