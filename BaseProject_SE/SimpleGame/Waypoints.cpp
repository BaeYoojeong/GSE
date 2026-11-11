#include "Waypoints.h"
#include <istream>
#include <ostream>
#include <string>

namespace Ashen
{
    bool Waypoints::Active(unsigned town) const
    {
        return town < TownWorld::Count && (active & (1u << town)) != 0;
    }

    bool Waypoints::Activate(unsigned town)
    {
        if (town >= TownWorld::Count || Active(town))
        {
            return false;
        }
        active |= 1u << town;
        return true;
    }

    Location Waypoints::Position(unsigned town)
    {
        return {TownWorld::Towns()[town % TownWorld::Count].center, {384, 600}};
    }

    bool Waypoints::Read(std::istream& in)
    {
        std::string marker;
        std::uint32_t bits = 0;
        if (!(in >> marker >> bits) || marker != "WAYPOINTS_V1" || bits >= (1u << TownWorld::Count))
        {
            return false;
        }
        active = bits;
        return true;
    }

    void Waypoints::Write(std::ostream& out) const
    {
        out << "WAYPOINTS_V1 " << active << '\n';
    }
} // namespace Ashen
