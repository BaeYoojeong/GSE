#pragma once
#include "WorldTypes.h"
#include <array>

namespace Ashen
{
    struct TownDefinition
    {
        ChunkKey center;
        const wchar_t* name;
        float trade;
        float learning;
        float law;
    };

    namespace TownWorld
    {
        constexpr unsigned Count = 7;
        constexpr unsigned StartingResidents = 30;
        constexpr unsigned InitialPopulation = Count * StartingResidents;
        constexpr std::uint64_t SeedIdBase = 1ull << 40;
        constexpr float PlayerSpeed = 190;
        constexpr int CenterSpacing = 5;
        constexpr float GateDistance = ChunkSize * (CenterSpacing - 3);
        ChunkKey LegacyCenter(unsigned index);
        int LegacyIndex(ChunkKey center);
        const std::array<TownDefinition, Count>& Towns();
        int Index(ChunkKey center);
        int At(ChunkKey district);
        int Nearest(ChunkKey chunk);
        bool Contains(ChunkKey center, ChunkKey district);
        Vec2 House(unsigned lot);
        Vec2 Door(unsigned lot);
        bool SeedId(std::uint64_t id);
    } // namespace TownWorld
} // namespace Ashen
