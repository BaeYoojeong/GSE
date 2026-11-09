#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <vector>
#include <string>

namespace InfiniteWorld
{
    constexpr int ChunkSize = 1024, TileSize = 32, TilesPerSide = 32, LoadRadius = 2;
    constexpr std::uint64_t WorldSeed = 20260908;

    struct Key
    {
        std::int64_t x = 0, y = 0;

        bool operator<(const Key& other) const
        {
            return x < other.x || (x == other.x && y < other.y);
        }

        bool operator==(const Key& other) const
        {
            return x == other.x && y == other.y;
        }

        bool operator!=(const Key& other) const
        {
            return !(*this == other);
        }
    };

    enum class Biome
    {
        Meadow,
        Forest,
        Blossom,
        Wetland
    };

    enum class Tile
    {
        Grass,
        Path,
        Water,
        Flowers,
        Authored
    };

    enum class Kind
    {
        Tree,
        Cherry,
        House,
        Lamp,
        Herb
    };

    struct Object
    {
        Kind kind;
        float x, y, size;
        unsigned slot;
    };

    struct Chunk
    {
        Key key;
        Biome biome = Biome::Meadow;
        std::array<Tile, TilesPerSide * TilesPerSide> tiles;
        std::vector<Object> objects;
        std::uint64_t harvested = 0;
        bool stateReadable = true;
    };

    struct Progress
    {
        Key origin = {1, 2};
        float x = 376, y = 852;
        int herbs = 0;
        unsigned starterPicked = 0;
        bool requested = false, delivered = false;
    };

    bool InitializeStorage();
    bool LoadProgress(Progress& progress);
    bool SaveProgress(const Progress& progress);
    // Generates at most 25 small chunks; called only when the center chunk changes.
    void Stream(Key center);
    const std::map<Key, Chunk>& Active();
    const Chunk* Find(Key key);
    void Clear();
    std::uint64_t Hash(Key key, std::uint64_t salt = 0);
    bool Offset(Key key, int dx, int dy, Key& result);
    Key Resolve(Key origin, float& x, float& y);
    Tile Sample(Key origin, float x, float y);
    bool Harvest(Key key, unsigned slot);
    bool IsStarter(Key key);
    bool StorageHealthy();
    int LoadLevelState(std::string& data);
    bool SaveLevelState(const std::string& data);
} // namespace InfiniteWorld
