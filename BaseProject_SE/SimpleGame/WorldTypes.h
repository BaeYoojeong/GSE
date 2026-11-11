#pragma once

#include "Actor.h"

namespace Ashen
{
    constexpr float ChunkSize = 768;
    constexpr float CameraCos = .707106781f;
    constexpr float CameraSin = .707106781f;
    constexpr float CameraDepth = .5f;

    struct ChunkKey
    {
        std::int64_t x = 0;
        std::int64_t y = 0;

        bool operator<(const ChunkKey& other) const
        {
            return x == other.x ? y < other.y : x < other.x;
        }

        bool operator==(const ChunkKey& other) const
        {
            return x == other.x && y == other.y;
        }
    };

    struct Location
    {
        ChunkKey chunk;
        Vec2 local = {384, 400};
    };

    enum class Kind
    {
        Ground,
        House,
        Chapel,
        Tree,
        Rubble,
        Grave,
        Fire,
        Herb,
        Player,
        Enemy,
        Settler,
        Heir,
        Projectile,
        Drop,
        Citizen,
        Quarry,
        MarketStall,
        BellAltar,
        Residence,
        BellKeeper,
        Waypoint,
        Exploration
    };

    enum class Nature
    {
        Wanderer,
        Guardian,
        Giver,
        Cautious
    };

    enum class Emotion
    {
        Hope,
        Fear,
        Anger
    };

    struct LifeRecord
    {
        std::uint64_t id = 1;
        float distance = 0;
        int kills = 0;
        int gifts = 0;
        int rests = 0;
        int hope = 0;
        int fear = 0;
        int anger = 0;
    };

    struct Memory
    {
        LifeRecord life;
        Location home;
        Nature nature = Nature::Wanderer;
        Emotion emotion = Emotion::Hope;
        int bond = 0;
        std::uint64_t lastGiftLife = 0;
        std::uint64_t lastTalkLife = 0;
    };

    struct Region
    {
        bool lit = false;
        unsigned harvested = 0;
        unsigned defeated = 0;
        float enemyRespawn[2] = {};
        std::uint32_t lootCollected = 0;
        int layoutVersion = 2;
        Vec2 enemyLoot[2] = {{160, 566}, {590, 616}};
        int aid = 0;
        std::uint64_t lastGiftLife = 0;
        std::uint64_t lastTalkLife = 0;
    };

    enum class ItemKind
    {
        Weapon,
        Healing,
        Experience,
        Magnet
    };

    struct Progression
    {
        int level = 1;
        int experience = 0;
        int weapon = 0;
        float magnetSeconds = 0;
        unsigned lessons = 0;
        unsigned itemsSeen = 0;
    };

    float DistanceSquared(Vec2 a, Vec2 b);
    Vec2 ScreenToGround(Vec2 direction);
} // namespace Ashen
