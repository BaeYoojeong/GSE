#include "WorldLayout.h"
#include "DarkAgeGame.h"

namespace Ashen::WorldLayout
{
    namespace
    {
        std::int64_t Divide(std::int64_t value, std::int64_t size)
        {
            return value / size - (value % size < 0 ? 1 : 0);
        }

        int Remainder(std::int64_t value, int size)
        {
            const int remainder = static_cast<int>(value % size);
            return remainder < 0 ? remainder + size : remainder;
        }

        double Smooth(double value)
        {
            return value * value * (3 - 2 * value);
        }
    } // namespace

    unsigned Biome(const Game& game, ChunkKey chunk)
    {
        constexpr int span = 8;
        const ChunkKey cell = {Divide(chunk.x, span), Divide(chunk.y, span)};
        const double x = Smooth(Remainder(chunk.x, span) / static_cast<double>(span));
        const double y = Smooth(Remainder(chunk.y, span) / static_cast<double>(span));
        const auto sample = [&](int dx, int dy)
        { return (game.Hash({cell.x + dx, cell.y + dy}, 71031) & 0xffffu) / 65535.0; };
        const double top = sample(0, 0) * (1 - x) + sample(1, 0) * x;
        const double bottom = sample(0, 1) * (1 - x) + sample(1, 1) * x;
        const double value = top * (1 - y) + bottom * y;
        return value < .38 ? 1 : value > .62 ? 2 : 0;
    }

    bool Road(ChunkKey chunk, int column, int row)
    {
        const bool village = TownWorld::At(chunk) >= 0;
        const bool vertical = column >= 10 && column <= 13 && (village || Remainder(chunk.x, 4) == 0);
        const bool horizontal = row >= 10 && row <= 13 && (village || Remainder(chunk.y, 4) == 0);
        return vertical || horizontal;
    }

    Site Feature(const Game& game, ChunkKey chunk)
    {
        if (TownWorld::At(chunk) >= 0 || chunk == ChapterTwo::Road().chunk ||
            chunk == ChapterTwo::Altar().chunk)
        {
            return Site::None;
        }
        constexpr int span = 6;
        const ChunkKey cell = {Divide(chunk.x, span), Divide(chunk.y, span)};
        const auto hash = game.Hash(cell, 74019);
        const int offset = Remainder(chunk.y, span) * span + Remainder(chunk.x, span);
        const int first = static_cast<int>(hash % (span * span));
        // Three distinct sites in each macro region, including negative and distant coordinates.
        for (int site = 0; site < 3; ++site)
        {
            if (offset == (first + site * 11) % (span * span))
            {
                return static_cast<Site>(site + 1);
            }
        }
        return Site::None;
    }

    bool Camp(const Game& game, ChunkKey chunk)
    {
        return TownWorld::At(chunk) >= 0 || chunk == ChapterTwo::Road().chunk ||
               chunk == ChapterTwo::Altar().chunk || Feature(game, chunk) == Site::Camp ||
               game.Hash(chunk, 74020) % 17 == 0;
    }

    const wchar_t* BiomeName(unsigned biome)
    {
        return biome == 1 ? L"안개 습지" : biome == 2 ? L"잿빛 황무지" : L"황혼 수림";
    }

    const wchar_t* SiteName(Site site)
    {
        switch (site)
        {
        case Site::Camp:
            return L"방랑자의 야영지";
        case Site::Ruins:
            return L"잊힌 폐허";
        case Site::Grove:
            return L"약초 군락";
        default:
            return L"변경 지대";
        }
    }
} // namespace Ashen::WorldLayout
