#include "PlanetWorld.h"
#include "TownWorld.h"
#include <iomanip>
#include <sstream>

namespace Ashen::PlanetWorld
{
    namespace
    {
        void Split(std::int64_t value, std::int64_t size, std::int64_t& partition, std::int64_t& local)
        {
            partition = value / size;
            local = value % size;
            if (local < 0)
            {
                local += size;
                --partition;
            }
            if (local >= size / 2)
            {
                local -= size;
                ++partition;
            }
        }
    } // namespace

    Address Locate(const Location& location)
    {
        Address address;
        Split(location.chunk.x, LongitudeChunks, address.planetX, address.surfaceX);
        Split(location.chunk.y, LatitudeChunks, address.planetY, address.surfaceY);
        address.longitude =
            (static_cast<double>(address.surfaceX) + location.local.x / static_cast<double>(ChunkSize)) *
            360.0 / LongitudeChunks;
        address.latitude =
            -(static_cast<double>(address.surfaceY) + location.local.y / static_cast<double>(ChunkSize)) *
            180.0 / LatitudeChunks;
        return address;
    }

    std::wstring Describe(const Location& location)
    {
        const auto address = Locate(location);
        std::wostringstream text;
        text << L"행성 구획 (" << address.planetX << L"," << address.planetY << L") · 둘레 약 40,075km · "
             << std::fixed << std::setprecision(3) << L"경도 " << address.longitude << L" / 위도 "
             << address.latitude;
        return text.str();
    }

    ChunkKey MigrateTownChunk(ChunkKey key)
    {
        for (unsigned i = 0; i < TownWorld::Count; ++i)
        {
            const auto old = TownWorld::LegacyCenter(i);
            if (TownWorld::Contains(old, key))
            {
                const auto next = TownWorld::Towns()[i].center;
                return {key.x + next.x - old.x, key.y + next.y - old.y};
            }
        }
        return key;
    }
} // namespace Ashen::PlanetWorld
