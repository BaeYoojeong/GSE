#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "InfiniteWorld.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <iostream>

namespace
{
    using namespace InfiniteWorld;
    std::map<Key, Chunk> chunks;
    std::wstring saveDirectory;
    bool storageHealthy = false;
    bool progressWritable = true, readHealthy = true;

    std::int64_t FloorDiv(std::int64_t value, int divisor)
    {
        return value / divisor - (value % divisor < 0 ? 1 : 0);
    }

    std::uint64_t Mix(std::uint64_t v)
    {
        v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
        v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
        return v ^ (v >> 31);
    }

    bool Directory(const std::wstring& path)
    {
        if (CreateDirectoryW(path.c_str(), NULL))
        {
            return true;
        }

        DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    std::wstring ChunkPath(Key key)
    {
        return saveDirectory + L"\\chunk_" + std::to_wstring(key.x) + L"_" + std::to_wstring(key.y) +
               L".state";
    }

    bool WriteAtomic(const std::wstring& path, const std::string& data)
    {
        if (saveDirectory.empty())
        {
            return false;
        }

        std::wstring temporary = path + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, NULL);

        if (file == INVALID_HANDLE_VALUE)
        {
            storageHealthy = false;
            return false;
        }

        DWORD written = 0;
        bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, NULL) != FALSE &&
                  written == data.size();

        if (ok)
        {
            ok = FlushFileBuffers(file) != FALSE;
        }

        CloseHandle(file);

        if (ok)
        {
            ok = MoveFileExW(temporary.c_str(), path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        }

        if (!ok)
        {
            std::cerr << "Infinite world: could not save state.\n";
        }

        storageHealthy = ok;
        return ok;
    }

    // 0: not present, 1: read, -1: failed/corrupt. Missing files are untouched generated chunks.
    int Read(const std::wstring& path, std::string& data)
    {
        if (saveDirectory.empty())
        {
            return 0;
        }

        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);

        if (file == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
        }

        LARGE_INTEGER size = {};

        if (!GetFileSizeEx(file, &size) || size.QuadPart < 1 || size.QuadPart > 65536)
        {
            CloseHandle(file);
            return -1;
        }

        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD count = 0;
        bool ok = ReadFile(file, &data[0], static_cast<DWORD>(data.size()), &count, NULL) != FALSE &&
                  count == data.size();
        CloseHandle(file);
        return ok ? 1 : -1;
    }

    bool LinkRoad(Key key, float x, float y)
    {
        // Short authored exits join the starting area's roads to the infinite road lattice.
        if (key.x < -1 || key.x > 6 || key.y < -1 || key.y > 5)
        {
            return false;
        }

        float ax = static_cast<float>(key.x * ChunkSize) + x, ay = static_cast<float>(key.y * ChunkSize) + y;
        return ((ay >= 2850 && ay <= 2990) && ((ax >= -512 && ax <= 600) || (ax >= 5050 && ax <= 6656))) ||
               (ax >= 1320 && ax <= 1480 && ((ay >= -512 && ay <= 1550) || (ay >= 3500 && ay <= 5632)));
    }

    bool ClearFootprint(const Chunk& c, float x, float y, float w, float h)
    {
        int left = static_cast<int>(x / TileSize), top = static_cast<int>(y / TileSize);
        int right = static_cast<int>((x + w) / TileSize), bottom = static_cast<int>((y + h) / TileSize);

        if (left < 1 || top < 1 || right >= 31 || bottom >= 31)
        {
            return false;
        }

        for (int row = top; row <= bottom; ++row)
        {
            for (int col = left; col <= right; ++col)
            {
                auto tile = c.tiles[row * TilesPerSide + col];

                if (tile == Tile::Path || tile == Tile::Water || tile == Tile::Authored)
                {
                    return false;
                }
            }
        }

        return true;
    }

    Chunk Generate(Key key)
    {
        Chunk c;
        c.key = key;
        Key region = {FloorDiv(key.x, 4), FloorDiv(key.y, 4)};
        c.biome = static_cast<Biome>(Hash(region, 71) % 4);
        bool starter = IsStarter(key);
        const auto seed = Hash(key);

        for (int row = 0; row < TilesPerSide; ++row)
        {
            for (int col = 0; col < TilesPerSide; ++col)
            {
                Tile tile = starter ? Tile::Authored : Tile::Grass;

                if (!starter)
                {
                    float dx = col - 7.0f, dy = row - 24.0f;

                    if (c.biome == Biome::Wetland && dx * dx / 36 + dy * dy / 25 < 1)
                    {
                        tile = Tile::Water;
                    }
                    else if (c.biome == Biome::Blossom && Hash(key, row * 32 + col + 500) % 7 == 0)
                    {
                        tile = Tile::Flowers;
                    }

                    // These lanes always meet neighboring chunks at the exact same boundary coordinates.
                    if ((col >= 15 && col <= 17) || (row >= 15 && row <= 17))
                    {
                        tile = Tile::Path;
                    }
                }

                if (LinkRoad(key, col * 32.0f + 16, row * 32.0f + 16))
                {
                    tile = Tile::Path;
                }

                c.tiles[row * TilesPerSide + col] = tile;
            }
        }

        if (!starter)
        {
            bool camp = seed % 9 == 0;

            if (camp)
            {
                if (ClearFootprint(c, 140, 160, 250, 220))
                {
                    c.objects.push_back({Kind::House, 160, 330, 210, 0});
                }

                if (ClearFootprint(c, 660, 650, 250, 230))
                {
                    c.objects.push_back({Kind::House, 680, 820, 210, 0});
                }

                c.objects.push_back({Kind::Lamp, 465, 475, 1, 0});
                c.objects.push_back({Kind::Lamp, 585, 585, 1, 0});
            }

            for (int row = 0; row < 8; ++row)
            {
                for (int col = 0; col < 8; ++col)
                {
                    auto h = Hash(key, 100 + row * 8 + col);

                    if (h % (c.biome == Biome::Forest ? 5 : 3) == 0)
                    {
                        continue;
                    }

                    float x = 70 + col * 120.0f + static_cast<float>(h % 25),
                          y = 85 + row * 120.0f + static_cast<float>((h >> 10) % 22);

                    if (!ClearFootprint(c, x - 40, y - 65, 80, 85))
                    {
                        continue;
                    }

                    if (camp && ((x > 90 && x < 435 && y < 440) || (x > 600 && y > 590)))
                    {
                        continue;
                    }

                    c.objects.push_back({c.biome == Biome::Blossom || h % 11 == 0 ? Kind::Cherry : Kind::Tree,
                                         x, y, .8f + static_cast<float>(h % 4) * .12f, 0});
                }
            }

            // Stable slots: only these eight road-side herb nodes can change state.
            for (unsigned slot = 0; slot < 8; ++slot)
            {
                float x = slot % 2 == 0 ? 452.0f : 594.0f, y = 96 + (slot / 2) * 256.0f;

                if (ClearFootprint(c, x - 12, y - 24, 24, 36))
                {
                    c.objects.push_back({Kind::Herb, x, y, 1, slot});
                }
            }
        }

        std::string data;
        int state = Read(ChunkPath(key), data);

        if (state == 1)
        {
            std::istringstream in(data);
            std::string magic;
            std::uint64_t storedSeed = 0, mask = 0;
            Key stored;
            bool ok = static_cast<bool>(in >> magic >> storedSeed >> stored.x >> stored.y >> mask);

            if (ok && magic == "CHUNK_V1" && storedSeed == WorldSeed && stored == key && mask < 256)
            {
                c.harvested = mask;
            }
            else
            {
                c.stateReadable = false;
            }
        }
        else if (state < 0)
        {
            c.stateReadable = false;
        }

        if (!c.stateReadable)
        {
            readHealthy = false;
            std::cerr << "Infinite world: invalid chunk state; harvesting disabled for this chunk.\n";
        }

        return c;
    }
} // namespace

std::uint64_t InfiniteWorld::Hash(Key key, std::uint64_t salt)
{
    return Mix(static_cast<std::uint64_t>(key.x) ^
               Mix(static_cast<std::uint64_t>(key.y) + 0x9e3779b97f4a7c15ULL) ^ Mix(WorldSeed + salt));
}

bool InfiniteWorld::Offset(Key key, int dx, int dy, Key& result)
{
    const auto lo = (std::numeric_limits<std::int64_t>::min)(),
               hi = (std::numeric_limits<std::int64_t>::max)();

    if ((dx > 0 && key.x > hi - dx) || (dx < 0 && key.x < lo - dx) || (dy > 0 && key.y > hi - dy) ||
        (dy < 0 && key.y < lo - dy))
    {
        return false;
    }

    result = {key.x + dx, key.y + dy};
    return true;
}

InfiniteWorld::Key InfiniteWorld::Resolve(Key origin, float& x, float& y)
{
    int dx = static_cast<int>(std::floor(x / ChunkSize)), dy = static_cast<int>(std::floor(y / ChunkSize));
    Key result;

    if (!Offset(origin, dx, dy, result))
    {
        return origin;
    }

    x -= dx * ChunkSize;
    y -= dy * ChunkSize;
    return result;
}

bool InfiniteWorld::IsStarter(Key key)
{
    return key.x >= 0 && key.x <= 5 && key.y >= 0 && key.y <= 4;
}

void InfiniteWorld::Stream(Key center)
{
    std::vector<Key> desired;

    for (int y = -LoadRadius; y <= LoadRadius; ++y)
    {
        for (int x = -LoadRadius; x <= LoadRadius; ++x)
        {
            Key k;

            if (Offset(center, x, y, k))
            {
                desired.push_back(k);
            }
        }
    }

    for (auto it = chunks.begin(); it != chunks.end();)
    {
        if (std::find(desired.begin(), desired.end(), it->first) == desired.end())
        {
            it = chunks.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (Key k : desired)
    {
        if (chunks.find(k) == chunks.end())
        {
            chunks.emplace(k, Generate(k));
        }
    }
}

const std::map<InfiniteWorld::Key, InfiniteWorld::Chunk>& InfiniteWorld::Active()
{
    return chunks;
}

const InfiniteWorld::Chunk* InfiniteWorld::Find(Key key)
{
    auto it = chunks.find(key);
    return it == chunks.end() ? NULL : &it->second;
}

InfiniteWorld::Tile InfiniteWorld::Sample(Key origin, float x, float y)
{
    Key k = Resolve(origin, x, y);
    const Chunk* c = Find(k);

    if (!c || x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize)
    {
        return Tile::Water;
    }

    return c->tiles[static_cast<int>(y / TileSize) * TilesPerSide + static_cast<int>(x / TileSize)];
}

bool InfiniteWorld::Harvest(Key key, unsigned slot)
{
    if (!progressWritable)
    {
        return false;
    }

    auto it = chunks.find(key);

    if (it == chunks.end() || slot >= 8 || !it->second.stateReadable)
    {
        return false;
    }

    std::uint64_t bit = std::uint64_t(1) << slot;

    if (it->second.harvested & bit)
    {
        return false;
    }

    bool exists = false;

    for (const Object& object : it->second.objects)
    {
        if (object.kind == Kind::Herb && object.slot == slot)
        {
            exists = true;
        }
    }

    if (!exists)
    {
        return false;
    }

    auto mask = it->second.harvested | bit;
    std::ostringstream out;
    out << "CHUNK_V1 " << WorldSeed << ' ' << key.x << ' ' << key.y << ' ' << mask << '\n';

    if (!WriteAtomic(ChunkPath(key), out.str()))
    {
        return false;
    }

    it->second.harvested = mask;
    return true;
}

bool InfiniteWorld::InitializeStorage()
{
    saveDirectory.clear();
    storageHealthy = false;
    progressWritable = true;
    readHealthy = true;
    wchar_t localAppData[32768] = {};
    DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, 32768);

    if (!count || count >= 32768)
    {
        return false;
    }

    std::wstring root = std::wstring(localAppData) + L"\\MoonlitHerbalist";

    if (!Directory(root))
    {
        return false;
    }

    saveDirectory = root + L"\\InfiniteWorld_v1_seed_" + std::to_wstring(WorldSeed);
    storageHealthy = Directory(saveDirectory);
    return storageHealthy;
}

bool InfiniteWorld::LoadProgress(Progress& p)
{
    std::string data;
    int state = Read(saveDirectory + L"\\player.state", data);

    if (state == 0)
    {
        return false;
    }

    Progress candidate;
    std::string magic;
    std::uint64_t seed = 0;
    int requested = 0, delivered = 0;
    std::istringstream in(data);
    bool ok =
        state == 1 && static_cast<bool>(in >> magic >> seed >> candidate.origin.x >> candidate.origin.y >>
                                        candidate.x >> candidate.y >> candidate.herbs >>
                                        candidate.starterPicked >> requested >> delivered);

    if (!ok || magic != "PLAYER_V1" || seed != WorldSeed || !std::isfinite(candidate.x) ||
        !std::isfinite(candidate.y) || candidate.x < 0 || candidate.x >= ChunkSize || candidate.y < 0 ||
        candidate.y >= ChunkSize || candidate.herbs < 0 || candidate.herbs > 999 ||
        candidate.starterPicked > 7 || requested < 0 || requested > 1 || delivered < 0 || delivered > 1)
    {
        progressWritable = false;
        readHealthy = false;
        std::cerr << "Infinite world: invalid player save; original file preserved, saving disabled.\n";
        return false;
    }

    candidate.requested = requested != 0;
    candidate.delivered = delivered != 0;
    p = candidate;
    return true;
}

bool InfiniteWorld::SaveProgress(const Progress& p)
{
    if (!progressWritable)
    {
        return false;
    }

    std::ostringstream out;
    out << std::setprecision(9) << "PLAYER_V1 " << WorldSeed << ' ' << p.origin.x << ' ' << p.origin.y << ' '
        << p.x << ' ' << p.y << ' ' << p.herbs << ' ' << p.starterPicked << ' ' << (p.requested ? 1 : 0)
        << ' ' << (p.delivered ? 1 : 0) << '\n';
    return WriteAtomic(saveDirectory + L"\\player.state", out.str());
}

bool InfiniteWorld::StorageHealthy()
{
    return storageHealthy && readHealthy;
}

void InfiniteWorld::Clear()
{
    chunks.clear();
}

int InfiniteWorld::LoadLevelState(std::string& data)
{
    return Read(saveDirectory + L"\\level_one.state", data);
}

bool InfiniteWorld::SaveLevelState(const std::string& data)
{
    if (!progressWritable)
    {
        return false;
    }

    return WriteAtomic(saveDirectory + L"\\level_one.state", data);
}
