#pragma once

#include "ItemCatalog.h"
#include <array>
#include <iosfwd>

namespace Ashen
{
    class Inventory
    {
      public:
        Inventory();
        unsigned Quantity(ItemId id) const;
        bool CanAdd(ItemId id, unsigned amount = 1) const;
        bool Add(ItemId id, unsigned amount = 1);
        bool Equip(ItemId id);
        bool ConsumePotion();
        ItemId Equipped() const;
        float WeaponDamage() const;
        bool Read(std::istream& in);
        void Write(std::ostream& out) const;

      private:
        std::array<unsigned, ItemTypeCount> quantities = {};
        ItemId equipped = ItemId::TravelerStaff;
    };
} // namespace Ashen
