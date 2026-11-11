#include "Inventory.h"
#include <istream>
#include <ostream>
#include <string>

namespace Ashen
{
    Inventory::Inventory()
    {
        quantities[static_cast<std::size_t>(ItemId::TravelerStaff)] = 1;
    }

    unsigned Inventory::Quantity(ItemId id) const
    {
        const auto index = static_cast<std::size_t>(id);
        return index < quantities.size() ? quantities[index] : 0;
    }

    bool Inventory::CanAdd(ItemId id, unsigned amount) const
    {
        const auto* item = FindItem(id);
        return item && item->category != ItemCategory::Resource && amount > 0 && amount <= item->stackLimit &&
               Quantity(id) <= item->stackLimit - amount;
    }

    bool Inventory::Add(ItemId id, unsigned amount)
    {
        if (!CanAdd(id, amount))
        {
            return false;
        }
        quantities[static_cast<std::size_t>(id)] += amount;
        return true;
    }

    bool Inventory::Equip(ItemId id)
    {
        const auto* item = FindItem(id);
        if (!item || item->category != ItemCategory::Weapon || Quantity(id) == 0)
        {
            return false;
        }
        equipped = id;
        return true;
    }

    bool Inventory::ConsumePotion()
    {
        auto& count = quantities[static_cast<std::size_t>(ItemId::HealingPotion)];
        if (count == 0)
        {
            return false;
        }
        --count;
        return true;
    }

    ItemId Inventory::Equipped() const
    {
        return equipped;
    }

    float Inventory::WeaponDamage() const
    {
        const auto* item = FindItem(equipped);
        return item ? item->attack : 16.0f;
    }

    bool Inventory::Read(std::istream& in)
    {
        Inventory candidate;
        std::string marker;
        unsigned weapon = 0;
        if (!(in >> marker >> weapon) || marker != "INVENTORY_V1" || weapon >= ItemTypeCount)
        {
            return false;
        }
        for (const auto& item : Items())
        {
            auto& count = candidate.quantities[static_cast<std::size_t>(item.id)];
            if (!(in >> count) || count > item.stackLimit ||
                (item.category == ItemCategory::Resource && count != 0))
            {
                return false;
            }
        }
        if (candidate.Quantity(ItemId::TravelerStaff) != 1 || !candidate.Equip(static_cast<ItemId>(weapon)))
        {
            return false;
        }
        *this = candidate;
        return true;
    }

    void Inventory::Write(std::ostream& out) const
    {
        out << "INVENTORY_V1 " << static_cast<unsigned>(equipped);
        for (unsigned count : quantities)
        {
            out << ' ' << count;
        }
        out << '\n';
    }
} // namespace Ashen
