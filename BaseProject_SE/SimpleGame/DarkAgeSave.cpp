#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "DarkAgeGame.h"
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <filesystem>
#include "RenderProfiler.h"
#include <chrono>

namespace
{
    using namespace Ashen;

    bool ValidLocation(const Location& location)
    {
        const auto limit = (std::numeric_limits<std::int64_t>::max)() / 4;
        return location.chunk.x >= -limit && location.chunk.x <= limit && location.chunk.y >= -limit &&
               location.chunk.y <= limit && std::isfinite(location.local.x) &&
               std::isfinite(location.local.y) && location.local.x >= 0 && location.local.x < ChunkSize &&
               location.local.y >= 0 && location.local.y < ChunkSize;
    }

    bool ReadLocation(std::istream& in, Location& location)
    {
        return static_cast<bool>(in >> location.chunk.x >> location.chunk.y >> location.local.x >>
                                 location.local.y) &&
               ValidLocation(location);
    }

    void WriteLocation(std::ostream& out, const Location& location)
    {
        out << location.chunk.x << ' ' << location.chunk.y << ' ' << location.local.x << ' '
            << location.local.y << '\n';
    }

    bool ReadLife(std::istream& in, LifeRecord& life)
    {
        return static_cast<bool>(in >> life.id >> life.distance >> life.kills >> life.gifts >> life.rests >>
                                 life.hope >> life.fear >> life.anger) &&
               life.id > 0 && life.id < (std::numeric_limits<std::uint64_t>::max)() - 18 &&
               std::isfinite(life.distance) && life.distance >= 0 && life.kills >= 0 && life.gifts >= 0 &&
               life.rests >= 0 && life.hope >= 0 && life.fear >= 0 && life.anger >= 0;
    }

    void WriteLife(std::ostream& out, const LifeRecord& life)
    {
        out << life.id << ' ' << life.distance << ' ' << life.kills << ' ' << life.gifts << ' ' << life.rests
            << ' ' << life.hope << ' ' << life.fear << ' ' << life.anger << '\n';
    }

    bool PreparePath(std::wstring& path)
    {
        wchar_t explicitFolder[32768] = {};
        const DWORD overrideLength =
            GetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", explicitFolder, 32768);
        std::wstring folder;
        if (overrideLength && overrideLength < 32768)
        {
            folder = explicitFolder;
            if (!std::filesystem::path(folder).is_absolute())
            {
                return false;
            }
        }
        else
        {
            wchar_t local[32768] = {};
            const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
            if (!length || length >= 32768)
            {
                return false;
            }
            folder = std::wstring(local) + L"\\AshenInheritance";
        }
        std::error_code directoryError;
        std::filesystem::create_directories(folder, directoryError);
        if (directoryError)
        {
            return false;
        }

        path = folder + L"\\World_v1.txt";
        return true;
    }
} // namespace

bool Ashen::Game::Load()
{
    if (!PreparePath(savePath))
    {
        writable = storageHealthy = false;
        return false;
    }

    // Keep one writer per profile for the lifetime of this game instance.
    HANDLE lock = CreateFileW((savePath + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (lock == INVALID_HANDLE_VALUE)
    {
        writable = storageHealthy = false;
        return false;
    }

    profileLock = lock;
    std::string data;
    bool valid = true, fromDatabase = false;
    if (!regionDatabase.Open(savePath + L".sqlite3") || !regionDatabase.ReadCore(data, fromDatabase))
    {
        writable = storageHealthy = false;
        return false;
    }
    if (!fromDatabase)
    {
        HANDLE file = CreateFileW(savePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                writable = storageHealthy = false;
            }

            return false;
        }

        LARGE_INTEGER size = {};
        valid = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 64 * 1024 * 1024;


        if (valid)
        {
            data.resize(static_cast<size_t>(size.QuadPart));
            DWORD read = 0;
            valid = ReadFile(file, &data[0], static_cast<DWORD>(data.size()), &read, nullptr) &&
                    read == data.size();
        }

        CloseHandle(file);
    }
    auto reject = [&]()
    {
        writable = storageHealthy = false;
        return false;
    };

    if (!valid)
    {
        return reject();
    }

    std::istringstream in(data);
    std::string magic;
    std::uint64_t candidateSeed = 0;
    float candidateHealth = 0;
    int candidateSupplies = 0;
    LifeRecord candidateLife;
    Location current;
    Location camp;

    if (!(in >> magic >> candidateSeed >> candidateHealth >> candidateSupplies) ||
        (magic != "ASHEN_WORLD_V1" && magic != "ASHEN_WORLD_V2" && magic != "ASHEN_WORLD_V3" &&
         magic != "ASHEN_WORLD_V4" && magic != "ASHEN_WORLD_V5" && magic != "ASHEN_WORLD_V6") ||
        !std::isfinite(candidateHealth) || candidateHealth < 0 ||
        candidateHealth > 120 + (MaxLevel - 1) * 12 || candidateSupplies < 0 || candidateSupplies > 99 ||
        !ReadLife(in, candidateLife) || !ReadLocation(in, current) || !ReadLocation(in, camp))
    {
        return reject();
    }

    std::map<ChunkKey, Region> loadedRegions;
    std::map<std::uint64_t, Memory> loadedMemories;
    size_t count = 0;

    if (!(in >> count) || count > 1000000)
    {
        return reject();
    }

    for (size_t i = 0; i < count; ++i)
    {
        ChunkKey key;
        Region region;
        int lit = 0;

        if (!(in >> key.x >> key.y >> lit >> region.harvested >> region.defeated >> region.aid >>
              region.lastGiftLife >> region.lastTalkLife) ||
            !ValidLocation({key, {0, 0}}) || lit < 0 || lit > 1 || region.harvested > 63 ||
            region.defeated > 3 || region.aid < 0 || region.lastGiftLife > candidateLife.id ||
            region.lastTalkLife > candidateLife.id)
        {
            return reject();
        }

        if (magic != "ASHEN_WORLD_V1")
        {
            if (!(in >> region.layoutVersion >> region.lootCollected >> region.enemyLoot[0].x >>
                  region.enemyLoot[0].y >> region.enemyLoot[1].x >> region.enemyLoot[1].y) ||
                region.layoutVersion < 1 || region.layoutVersion > 2 ||
                !ValidLocation({key, region.enemyLoot[0]}) || !ValidLocation({key, region.enemyLoot[1]}))
            {
                return reject();
            }
        }
        else
        {
            region.layoutVersion = 1;

            // V1 rewards were already granted directly. Do not generate duplicate loot on migration.
            for (int source = 0; source < 8; ++source)
            {
                const bool cleared = source < 6 ? (region.harvested & (1u << source)) != 0
                                                : (region.defeated & (1u << (source - 6))) != 0;

                if (cleared)
                {
                    region.lootCollected |= std::uint32_t{15} << (source * 4);
                }
            }
        }

        if (magic == "ASHEN_WORLD_V4" || magic == "ASHEN_WORLD_V5" || magic == "ASHEN_WORLD_V6")
        {
            if (!(in >> region.enemyRespawn[0] >> region.enemyRespawn[1]))
            {
                return reject();
            }
        }
        for (int slot = 0; slot < 2; ++slot)
        {
            const bool defeated = (region.defeated & (1u << slot)) != 0;
            if (magic != "ASHEN_WORLD_V4" && magic != "ASHEN_WORLD_V5" && magic != "ASHEN_WORLD_V6")
            {
                region.enemyRespawn[slot] = defeated ? EnemyRespawnSeconds : 0;
            }
            if (!std::isfinite(region.enemyRespawn[slot]) || region.enemyRespawn[slot] < 0 ||
                region.enemyRespawn[slot] > (std::max)(12.0f, EnemyRespawnSeconds) ||
                (defeated != (region.enemyRespawn[slot] > 0)))
            {
                return reject();
            }
        }

        for (float& timer : region.enemyRespawn)
        {
            timer = (std::min)(timer, EnemyRespawnSeconds);
        }
        for (int source = 0; source < 8; ++source)
        {
            const bool cleared = source < 6 ? (region.harvested & (1u << source)) != 0
                                            : (region.defeated & (1u << (source - 6))) != 0;

            if (!cleared && (region.lootCollected & (std::uint32_t{15} << (source * 4))))
            {
                return reject();
            }
        }

        region.lit = lit != 0;

        if (!loadedRegions.emplace(key, region).second)
        {
            return reject();
        }
    }

    auto checkpointState = loadedRegions.find(camp.chunk);

    if (checkpointState == loadedRegions.end() || !checkpointState->second.lit || !(in >> count) ||
        count > 1000000)
    {
        return reject();
    }

    const std::uint64_t expected = candidateLife.id - (candidateHealth > 0 ? 1 : 0);

    if (count != expected)
    {
        return reject();
    }

    for (size_t i = 0; i < count; ++i)
    {
        Memory memory;
        int nature = 0;
        int emotion = 0;

        if (!ReadLife(in, memory.life) || !ReadLocation(in, memory.home) ||
            !(in >> nature >> emotion >> memory.bond >> memory.lastGiftLife >> memory.lastTalkLife) ||
            memory.life.id > expected || nature < 0 || nature > 3 || emotion < 0 || emotion > 2 ||
            memory.bond < 0 || memory.lastGiftLife > candidateLife.id ||
            memory.lastTalkLife > candidateLife.id)
        {
            return reject();
        }

        memory.nature = static_cast<Nature>(nature);
        memory.emotion = static_cast<Emotion>(emotion);

        if (!loadedMemories.emplace(memory.life.id, memory).second)
        {
            return reject();
        }
    }

    Progression candidateGrowth;

    if (magic != "ASHEN_WORLD_V1")
    {
        std::string marker;

        if (!(in >> marker >> candidateGrowth.level >> candidateGrowth.experience >> candidateGrowth.weapon >>
              candidateGrowth.magnetSeconds >> candidateGrowth.lessons >> candidateGrowth.itemsSeen) ||
            marker != "GROWTH" || candidateGrowth.level < 1 || candidateGrowth.level > MaxLevel ||
            candidateGrowth.experience < 0 ||
            candidateGrowth.experience >= 35 + (candidateGrowth.level - 1) * 15 ||
            (candidateGrowth.level == MaxLevel && candidateGrowth.experience != 0) ||
            candidateGrowth.weapon < 0 || candidateGrowth.weapon > MaxWeapon ||
            !std::isfinite(candidateGrowth.magnetSeconds) || candidateGrowth.magnetSeconds < 0 ||
            candidateGrowth.magnetSeconds > 60 || candidateGrowth.lessons > 7 ||
            candidateGrowth.itemsSeen > 15)
        {
            return reject();
        }
    }

    WorldSociety loadedSociety;
    if (magic == "ASHEN_WORLD_V3" || magic == "ASHEN_WORLD_V4" || magic == "ASHEN_WORLD_V5" ||
        magic == "ASHEN_WORLD_V6")
    {
        if (!loadedSociety.Read(in, candidateLife.id) ||
            loadedSociety.citizens.size() !=
                (loadedSociety.ExpandedWorld() ? TownWorld::InitialPopulation : 18) + loadedMemories.size())
        {
            return reject();
        }
        for (const auto& pair : loadedMemories)
        {
            const auto* citizen = loadedSociety.Find(18 + pair.first);
            if (!citizen || citizen->originLife != pair.first ||
                (!loadedSociety.ExpandedWorld() && !(citizen->village == pair.second.home.chunk)))
            {
                return reject();
            }
        }
    }

    ChapterTwo candidateChapter;
    if ((magic == "ASHEN_WORLD_V5" || magic == "ASHEN_WORLD_V6") && !candidateChapter.Read(in))
    {
        return reject();
    }
    if (candidateHealth > 100 + (candidateGrowth.level - 1) * 12 + (candidateChapter.Complete() ? 20 : 0))
    {
        return reject();
    }
    const auto stage = candidateChapter.CurrentStage();
    if (stage != ChapterTwo::Stage::Tutorial &&
        ((candidateGrowth.lessons & 7u) != 7u || candidateGrowth.itemsSeen != 15u))
    {
        return reject();
    }
    if (stage > ChapterTwo::Stage::PilgrimRoad)
    {
        const auto road = loadedRegions.find(ChapterTwo::Road().chunk);
        if (road == loadedRegions.end() || !road->second.lit)
        {
            return reject();
        }
    }
    if (candidateChapter.Complete())
    {
        const auto tower = loadedRegions.find(ChapterTwo::Altar().chunk);
        if (tower == loadedRegions.end() || !tower->second.lit)
        {
            return reject();
        }
    }
    std::string end;

    if (!(in >> end) || end != "END")
    {
        return reject();
    }

    in >> std::ws;

    if (!in.eof())
    {
        return reject();
    }

    if (!fromDatabase)
    {
        const std::wstring backup = savePath + L".pre-world-v6.bak";
        if (!CopyFileW(savePath.c_str(), backup.c_str(), TRUE) && GetLastError() != ERROR_FILE_EXISTS)
        {
            return reject();
        }
    }
    if (magic == "ASHEN_WORLD_V5")
    {
        std::map<ChunkKey, Region> relocated;
        for (const auto& pair : loadedRegions)
        {
            const auto key = PlanetWorld::MigrateTownChunk(pair.first);
            const auto existing = relocated.find(key);
            if (existing == relocated.end())
            {
                relocated.emplace(key, pair.second);
                continue;
            }
            auto& target = existing->second;
            const auto& incoming = pair.second;
            target.lit |= incoming.lit;
            target.harvested |= incoming.harvested;
            for (unsigned slot = 0; slot < 2; ++slot)
            {
                if (!(target.defeated & (1u << slot)) && (incoming.defeated & (1u << slot)))
                {
                    target.enemyLoot[slot] = incoming.enemyLoot[slot];
                }
                target.enemyRespawn[slot] =
                    (std::max)(target.enemyRespawn[slot], incoming.enemyRespawn[slot]);
            }
            target.defeated |= incoming.defeated;
            target.lootCollected |= incoming.lootCollected;
            target.aid = (std::max)(target.aid, incoming.aid);
            target.lastGiftLife = (std::max)(target.lastGiftLife, incoming.lastGiftLife);
            target.lastTalkLife = (std::max)(target.lastTalkLife, incoming.lastTalkLife);
            target.layoutVersion = (std::min)(target.layoutVersion, incoming.layoutVersion);
        }
        loadedRegions = std::move(relocated);
        current.chunk = PlanetWorld::MigrateTownChunk(current.chunk);
        camp.chunk = PlanetWorld::MigrateTownChunk(camp.chunk);
        for (auto& pair : loadedMemories)
        {
            pair.second.home.chunk = PlanetWorld::MigrateTownChunk(pair.second.home.chunk);
        }
        RenderProfiler::Event("world_layout_migrated", "town_center_spacing=5 legacy_spacing=8");
    }

    society = std::move(loadedSociety);
    chapter = candidateChapter;
    // Commit only a completely validated snapshot. Invalid files are never silently overwritten.
    growth = candidateGrowth;
    seed = candidateSeed;
    health = candidateHealth;
    supplies = candidateSupplies;
    life = candidateLife;
    origin = current.chunk;
    hero->position = current.local;
    checkpoint = camp;
    regions = std::move(loadedRegions);
    memories = std::move(loadedMemories);
    return true;
}

bool Ashen::Game::Save()
{
    const auto saveStarted = std::chrono::steady_clock::now();
    saveTimer = 0;

    if (!writable || regionReadFailed || savePath.empty() || !hero)
    {
        storageHealthy = false;
        return false;
    }

    std::ostringstream out;
    out << std::setprecision(9) << "ASHEN_WORLD_V6 " << seed << ' ' << health << ' ' << supplies << '\n';
    WriteLife(out, life);
    WriteLocation(out, Absolute(hero->position));
    WriteLocation(out, checkpoint);
    if (!RegionAt(checkpoint.chunk) || !RegionAt(ChapterTwo::Road().chunk) ||
        !RegionAt(ChapterTwo::Altar().chunk))
    {
        storageHealthy = false;
        return false;
    }
    std::map<ChunkKey, Region> snapshotRegions;
    for (const auto& pair : regions)
    {
        if (chunks.count(pair.first) || pair.first == checkpoint.chunk ||
            pair.first == ChapterTwo::Road().chunk || pair.first == ChapterTwo::Altar().chunk ||
            pair.first == ChunkKey{})
        {
            snapshotRegions.insert(pair);
        }
    }
    out << snapshotRegions.size() << '\n';

    for (const auto& pair : snapshotRegions)
    {
        const Region& region = pair.second;
        out << pair.first.x << ' ' << pair.first.y << ' ' << region.lit << ' ' << region.harvested << ' '
            << region.defeated << ' ' << region.aid << ' ' << region.lastGiftLife << ' '
            << region.lastTalkLife << ' ' << region.layoutVersion << ' ' << region.lootCollected << ' '
            << region.enemyLoot[0].x << ' ' << region.enemyLoot[0].y << ' ' << region.enemyLoot[1].x << ' '
            << region.enemyLoot[1].y << ' ' << region.enemyRespawn[0] << ' ' << region.enemyRespawn[1]
            << '\n';
    }

    out << memories.size() << '\n';

    for (const auto& pair : memories)
    {
        const Memory& memory = pair.second;
        WriteLife(out, memory.life);
        WriteLocation(out, memory.home);
        out << static_cast<int>(memory.nature) << ' ' << static_cast<int>(memory.emotion) << ' '
            << memory.bond << ' ' << memory.lastGiftLife << ' ' << memory.lastTalkLife << '\n';
    }

    out << "GROWTH " << growth.level << ' ' << growth.experience << ' ' << growth.weapon << ' '
        << growth.magnetSeconds << ' ' << growth.lessons << ' ' << growth.itemsSeen << '\n';
    society.Write(out);
    chapter.Write(out);
    out << "END\n";
    const std::string data = out.str();

    if (data.size() > 64 * 1024 * 1024)
    {
        storageHealthy = false;
        return false;
    }

    const bool success =
        regionDatabase.IsOpen() && regionDatabase.Commit(data, regions, society.simulationSeconds);
    if (success)
    {
        TrimRegions();
    }

    storageHealthy = success;

    if (success)
    {
        dirty = false;
    }

    RenderProfiler::Event(
        "world_save", success ? "success" : "failed", data.size(),
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - saveStarted).count());
    return success;
}

void Ashen::Game::CloseStorage()
{
    regionDatabase.Close();
    if (profileLock)
    {
        CloseHandle(static_cast<HANDLE>(profileLock));
        profileLock = nullptr;
    }
}
