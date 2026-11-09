#pragma once

#include "InfiniteWorld.h"
#include <array>
#include <vector>

namespace LevelOne
{
    constexpr int Side = 64;
    constexpr int CellSize = 32;
    constexpr int Extent = Side * CellSize;
    constexpr float AttackRange = 320.0f;
    constexpr float GatherRange = 170.0f;

    enum class Cell
    {
        Grass,
        Path,
        Water,
        Rock,
        Flowers
    };

    enum class Item
    {
        Tool,
        Healing,
        Experience,
        Magnet,
        Vacuum
    };

    enum class Feedback
    {
        None,
        Gather,
        Kill,
        Tool,
        Healing,
        Experience,
        Magnet,
        Vacuum,
        LevelUp,
        BossAwake,
        Complete,
        Downed,
        Respawn
    };

    struct Point
    {
        float x = 0;
        float y = 0;
    };

    struct Stats
    {
        int level = 1;
        int xp = 0;
        float health = 100;
        int tool = 0;
        int magnet = 0;
        int gathered = 0;
        int remains = 0;
        int kills = 0;
        bool complete = false;
    };

    struct Resource
    {
        Point position;
        int remaining = 12;
        bool remains = false;
        bool active = true;
    };

    struct Monster
    {
        Point position;
        Point home;
        float health = 40;
        float maxHealth = 40;
        float cooldown = 1;
        float windup = 0;
        Point aim;
        bool boss = false;
    };

    struct Projectile
    {
        Point position;
        Point velocity;
        float remaining = 0;
        float damage = 0;
        bool hostile = false;
    };

    struct Drop
    {
        Point position;
        Item type = Item::Experience;
        int amount = 1;
    };

    // A missing level save starts the tutorial once. Existing world progress is retained.
    bool Initialize();
    bool Save();
    bool StorageHealthy();
    bool OwnsChunk(InfiniteWorld::Key key);
    bool OffsetFor(InfiniteWorld::Key origin, float& x, float& y);
    bool ToLocal(InfiniteWorld::Key origin, float worldX, float worldY, Point& local);
    bool Sample(InfiniteWorld::Key origin, float x, float y, Cell& cell);
    bool CanStand(InfiniteWorld::Key origin, float x, float y);
    void Entrance(InfiniteWorld::Key& origin, float& x, float& y);
    void Update(float seconds, InfiniteWorld::Key origin, float x, float y, bool gather, bool attack,
                bool paused);
    void Respawn();
    void ClearInput();
    bool NeedsSave();
    int TakeHerbs();
    int MaxHealth();
    int NextExperience();
    int GatherYield();
    float GatherSeconds();
    float PickupRadius();
    float VacuumSeconds();
    bool BossAwake();
    int AimTarget();
    int GatherTarget();
    float GatherProgress();
    Feedback CurrentFeedback();
    const Stats& Player();
    const std::array<Cell, Side * Side>& Terrain();
    const std::vector<Resource>& Resources();
    const std::vector<Monster>& Monsters();
    const std::vector<Projectile>& Projectiles();
    const std::vector<Drop>& Drops();
} // namespace LevelOne
