#include "TownWorld.h"
#include <cmath>

namespace Ashen::TownWorld
{
    const std::array<TownDefinition, Count>& Towns()
    {
        static const std::array<TownDefinition, Count> towns = {{{{0, 0}, L"재의 안식처", .55f, .45f, .85f},
                                                                 {{5, 0}, L"황동 장터", .95f, .30f, .65f},
                                                                 {{10, 0}, L"검은 나루", .75f, .20f, .15f},
                                                                 {{0, 5}, L"푸른 수림", .35f, .60f, .80f},
                                                                 {{5, 5}, L"흰 종 수도원", .35f, .95f, .90f},
                                                                 {{10, 5}, L"붉은 철벽", .65f, .35f, .45f},
                                                                 {{5, 10}, L"먼별 초원", .45f, .75f, .70f}}};
        return towns;
    }

    ChunkKey LegacyCenter(unsigned index)
    {
        const ChunkKey old[] = {{0, 0}, {8, 0}, {16, 0}, {0, 8}, {8, 8}, {16, 8}, {8, 16}};
        return old[index % Count];
    }

    int LegacyIndex(ChunkKey center)
    {
        for (unsigned i = 0; i < Count; ++i)
        {
            if (LegacyCenter(i) == center)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int Index(ChunkKey center)
    {
        for (unsigned i = 0; i < Count; ++i)
        {
            if (Towns()[i].center == center)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool Contains(ChunkKey center, ChunkKey district)
    {
        return district.x >= center.x - 1 && district.x <= center.x + 1 && district.y >= center.y - 1 &&
               district.y <= center.y + 1;
    }

    int At(ChunkKey district)
    {
        for (unsigned i = 0; i < Count; ++i)
        {
            if (Contains(Towns()[i].center, district))
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int Nearest(ChunkKey chunk)
    {
        long double best = -1;
        int result = 0;
        for (unsigned i = 0; i < Count; ++i)
        {
            const long double dx = static_cast<long double>(chunk.x) - Towns()[i].center.x;
            const long double dy = static_cast<long double>(chunk.y) - Towns()[i].center.y;
            const long double distance = dx * dx + dy * dy;
            if (best < 0 || distance < best)
            {
                best = distance;
                result = static_cast<int>(i);
            }
        }
        return result;
    }

    Vec2 House(unsigned lot)
    {
        unsigned district = lot / 8;
        int dx = 0, dy = 0;
        if (district < 9)
        {
            dx = static_cast<int>(district % 3) - 1;
            dy = static_cast<int>(district / 3) - 1;
        }
        else
        {
            // Additional inherited lives can own distinct outlying homes instead of sharing a plot.
            district -= 9;
            for (int ring = 2;; ++ring)
            {
                if (district >= static_cast<unsigned>(8 * ring))
                {
                    district -= 8 * ring;
                    continue;
                }
                const int side = static_cast<int>(district) / (2 * ring);
                const int offset = static_cast<int>(district) % (2 * ring);
                if (side == 0)
                {
                    dx = -ring + offset;
                    dy = -ring;
                }
                if (side == 1)
                {
                    dx = ring;
                    dy = -ring + offset;
                }
                if (side == 2)
                {
                    dx = ring - offset;
                    dy = ring;
                }
                if (side == 3)
                {
                    dx = -ring;
                    dy = ring - offset;
                }
                break;
            }
        }
        static constexpr float rows[] = {112, 240, 528, 656};
        return {dx * ChunkSize + (lot % 2 ? 624.0f : 144.0f), dy * ChunkSize + rows[(lot % 8) / 2]};
    }

    Vec2 Door(unsigned lot)
    {
        const Vec2 house = House(lot);
        return {house.x, house.y + 52};
    }

    bool SeedId(std::uint64_t id)
    {
        return (id >= 1 && id <= 18) || (id >= SeedIdBase + 18 && id < SeedIdBase + InitialPopulation);
    }
} // namespace Ashen::TownWorld
