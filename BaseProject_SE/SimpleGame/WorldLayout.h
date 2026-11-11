#pragma once

#include "WorldTypes.h"

namespace Ashen
{
    class Game;
}

namespace Ashen::WorldLayout
{
    enum class Site : unsigned
    {
        None,
        Camp,
        Ruins,
        Grove
    };

    // Pure queries: map previews never load regions or create simulation state.
    unsigned Biome(const Game& game, ChunkKey chunk);
    bool Road(ChunkKey chunk, int column, int row);
    Site Feature(const Game& game, ChunkKey chunk);
    bool Camp(const Game& game, ChunkKey chunk);
    const wchar_t* BiomeName(unsigned biome);
    const wchar_t* SiteName(Site site);
} // namespace Ashen::WorldLayout
