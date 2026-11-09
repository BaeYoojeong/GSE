#pragma once
#include "WorldTypes.h"
#include <string>

namespace Ashen::PlanetWorld
{
    // Equirectangular planetary partitions; surface chunks stay sparse and locally rendered in 2.5D.
    constexpr std::int64_t LongitudeChunks = 1565440;
    constexpr std::int64_t LatitudeChunks = LongitudeChunks / 2;
    constexpr double UnitsPerMeter = 30;
    constexpr double CircumferenceMeters = LongitudeChunks * static_cast<double>(ChunkSize) / UnitsPerMeter;

    struct Address
    {
        std::int64_t planetX = 0, planetY = 0;
        std::int64_t surfaceX = 0, surfaceY = 0;
        double longitude = 0, latitude = 0;
    };

    Address Locate(const Location& location);
    std::wstring Describe(const Location& location);
    ChunkKey MigrateTownChunk(ChunkKey key);
} // namespace Ashen::PlanetWorld
