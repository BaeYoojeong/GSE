#pragma once

#include "TownWorld.h"
#include <cstdint>
#include <iosfwd>

namespace Ashen
{
    static_assert(TownWorld::Count < 32, "Waypoint V1 mask supports fewer than 32 towns.");

    class Waypoints
    {
      public:
        bool Active(unsigned town) const;
        bool Activate(unsigned town);
        bool Read(std::istream& in);
        void Write(std::ostream& out) const;
        static Location Position(unsigned town);

      private:
        std::uint32_t active = 0;
    };
} // namespace Ashen
