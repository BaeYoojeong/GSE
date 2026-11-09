#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "LevelOne.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <queue>
#include <sstream>
#include <string>

namespace
{
    using namespace LevelOne;

    const Point Spawn = {1040, 208};
    std::array<Cell, Side * Side> terrain;
    std::array<int, Side * Side> distances;
    std::vector<Resource> resources;
    std::vector<Monster> monsters;
    std::vector<Projectile> projectiles;
    std::vector<Drop> drops;
    Stats stats;
    std::uint64_t seed = 0;
    std::uint64_t randomState = 0;
    float attackTimer = 0;
    float gatherTimer = 0;
    float immunityTimer = 0;
    float vacuumTimer = 0;
    float navigationTimer = 0;
    float feedbackTimer = 0;
    float dirtyTimer = 0;
    int aimTarget = -1;
    int gatherTarget = -1;
    int pendingHerbs = 0;
    bool dirty = false;
    bool writable = true;
    bool saved = true;
    bool awake = false;
    Feedback feedback = Feedback::None;

    float DistanceSquared(Point a, Point b)
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    unsigned Random(unsigned bound)
    {
        randomState ^= randomState >> 12;
        randomState ^= randomState << 25;
        randomState ^= randomState >> 27;
        return static_cast<unsigned>((randomState * 2685821657736338717ULL) % bound);
    }

    int Index(Point p)
    {
        if (p.x < 0 || p.y < 0 || p.x >= Extent || p.y >= Extent)
        {
            return -1;
        }

        return static_cast<int>(p.y / CellSize) * Side + static_cast<int>(p.x / CellSize);
    }

    bool Solid(int index)
    {
        return index < 0 || terrain[index] == Cell::Water || terrain[index] == Cell::Rock;
    }

    bool Walkable(Point point, float radius = 12)
    {
        for (int row = -1; row <= 1; ++row)
        {
            for (int col = -1; col <= 1; ++col)
            {
                if (Solid(Index({point.x + col * radius, point.y + row * radius})))
                {
                    return false;
                }
            }
        }

        return true;
    }

    void Carve(int col, int row, int radius)
    {
        for (int y = (std::max)(0, row - radius); y <= (std::min)(Side - 1, row + radius); ++y)
        {
            for (int x = (std::max)(0, col - radius); x <= (std::min)(Side - 1, col + radius); ++x)
            {
                terrain[y * Side + x] = Cell::Path;
            }
        }
    }

    void Connect(Point p)
    {
        int col = static_cast<int>(p.x / CellSize);
        int row = static_cast<int>(p.y / CellSize);

        for (int x = (std::min)(32, col); x <= (std::max)(32, col); ++x)
        {
            Carve(x, row, 1);
        }

        Carve(col, row, 2);
    }

    void Flood(Point start)
    {
        distances.fill(-1);
        int first = Index(start);

        if (Solid(first))
        {
            return;
        }

        std::queue<int> queue;
        queue.push(first);
        distances[first] = 0;

        while (!queue.empty())
        {
            int current = queue.front();
            queue.pop();
            int x = current % Side;
            int y = current / Side;
            const int dx[] = {-1, 1, 0, 0};
            const int dy[] = {0, 0, -1, 1};

            for (int direction = 0; direction < 4; ++direction)
            {
                int nx = x + dx[direction];
                int ny = y + dy[direction];

                if (nx < 0 || ny < 0 || nx >= Side || ny >= Side)
                {
                    continue;
                }

                int next = ny * Side + nx;

                if (!Solid(next) && distances[next] < 0)
                {
                    distances[next] = distances[current] + 1;
                    queue.push(next);
                }
            }
        }
    }

    void Generate()
    {
        randomState = seed;
        terrain.fill(Cell::Grass);
        resources.clear();
        monsters.clear();
        drops.clear();
        projectiles.clear();

        // Small obstacle groups leave room for wide reserved teaching routes.
        for (int i = 0; i < 18; ++i)
        {
            int cx = 5 + Random(54);
            int cy = 7 + Random(50);
            int rx = 2 + Random(3);
            int ry = 2 + Random(3);

            for (int y = cy - ry; y <= cy + ry; ++y)
            {
                for (int x = cx - rx; x <= cx + rx; ++x)
                {
                    if ((x - cx) * (x - cx) * ry * ry + (y - cy) * (y - cy) * rx * rx <= rx * rx * ry * ry)
                    {
                        terrain[y * Side + x] = Cell::Water;
                    }
                }
            }
        }

        for (int i = 0; i < 250; ++i)
        {
            int index = (4 + Random(56)) * Side + 4 + Random(56);

            if (terrain[index] != Cell::Water)
            {
                terrain[index] = Random(3) ? Cell::Rock : Cell::Flowers;
            }
        }

        // Open perimeter, central avenue, and crossing lanes connect to the surrounding world.
        for (int y = 0; y < Side; ++y)
        {
            for (int x = 0; x < Side; ++x)
            {
                if (x < 3 || y < 3 || x >= Side - 3 || y >= Side - 3 || (x >= 29 && x <= 35) ||
                    (y >= 10 && y <= 14) || (y >= 26 && y <= 30) || (y >= 42 && y <= 46))
                {
                    terrain[y * Side + x] = Cell::Path;
                }
            }
        }

        Carve(32, 6, 5);
        Carve(32, 52, 9);

        const Point plants[] = {{880, 400}, {1200, 432}, {656, 592},  {1392, 624},
                                {688, 944}, {1360, 976}, {720, 1360}, {1392, 1392}};

        for (Point position : plants)
        {
            if (resources.size() >= 2)
            {
                position.x += (static_cast<int>(Random(5)) - 2) * CellSize;
            }

            Connect(position);
            resources.push_back({position, 12, false, true});
        }

        const Point homes[] = {{592, 880},  {1456, 880},  {528, 1200}, {1520, 1200},
                               {848, 1392}, {1200, 1392}, {1040, 1680}};

        for (int i = 0; i < 7; ++i)
        {
            Point home = homes[i];

            if (i < 6)
            {
                home.x += (static_cast<int>(Random(3)) - 1) * CellSize;
            }

            Connect(home);
            Monster monster;
            monster.position = monster.home = home;
            monster.boss = i == 6;
            monster.maxHealth = monster.health = monster.boss ? 300.0f : 42.0f;
            monster.cooldown = 1.0f + i * .12f;
            monsters.push_back(monster);
            resources.push_back({home, monster.boss ? 12 : 6, true, false});
        }

        // Repair disconnected floor components after all obstacles and points of interest are placed.
        // A 32-unit corridor fits the 26-unit player footprint; repaired routes are three tiles wide.
        Flood(Spawn);

        for (int y = 0; y < Side; ++y)
        {
            for (int x = 0; x < Side; ++x)
            {
                int index = y * Side + x;

                if (!Solid(index) && distances[index] < 0)
                {
                    Connect({x * 32.0f + 16, y * 32.0f + 16});
                    Flood(Spawn);
                }
            }
        }
    }

    void Notify(Feedback next)
    {
        feedback = next;
        feedbackTimer = 3.5f;
    }

    void AddExperience(int amount)
    {
        stats.xp += amount;

        while (stats.level < 20 && stats.xp >= NextExperience())
        {
            stats.xp -= NextExperience();
            ++stats.level;
            stats.health = (std::min)(static_cast<float>(MaxHealth()), stats.health + 27);
            Notify(Feedback::LevelUp);
        }

        if (stats.level == 20)
        {
            stats.xp = 0;
        }

        dirty = true;
    }

    bool ClearShot(Point from, Point to)
    {
        float distance = std::sqrt(DistanceSquared(from, to));
        int steps = (std::max)(1, static_cast<int>(std::ceil(distance / 8)));

        for (int i = 1; i <= steps; ++i)
        {
            float t = static_cast<float>(i) / steps;
            int index = Index({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t});

            if (index < 0 || terrain[index] == Cell::Rock)
            {
                return false;
            }
        }

        return true;
    }

    bool Enabled(const Monster& monster)
    {
        return monster.health > 0 && (!monster.boss || awake);
    }

    void Fire(Point from, Point target, float speed, float range, float damage, bool hostile, float angle = 0)
    {
        if (projectiles.size() >= 128)
        {
            return;
        }

        float direction = std::atan2(target.y - from.y, target.x - from.x) + angle;
        projectiles.push_back(
            {from, {std::cos(direction) * speed, std::sin(direction) * speed}, range, damage, hostile});
    }

    void SpawnDrop(Point position, Item type, int amount = 1)
    {
        if (!Walkable(position))
        {
            Point closest = Spawn;
            float best = static_cast<float>(Extent * Extent * 2);
            int cell = Index(position);

            if (cell >= 0)
            {
                for (int y = (std::max)(0, cell / Side - 2); y <= (std::min)(Side - 1, cell / Side + 2); ++y)
                {
                    for (int x = (std::max)(0, cell % Side - 2); x <= (std::min)(Side - 1, cell % Side + 2);
                         ++x)
                    {
                        Point candidate = {x * 32.0f + 16, y * 32.0f + 16};
                        float distance = DistanceSquared(position, candidate);

                        if (Walkable(candidate) && distance < best)
                        {
                            closest = candidate;
                            best = distance;
                        }
                    }
                }
            }

            position = closest;
        }

        if (drops.size() < 64)
        {
            drops.push_back({position, type, amount});
        }
    }

    void Defeat(size_t index)
    {
        Monster& monster = monsters[index];
        monster.health = 0;
        monster.windup = 0;
        Resource& remains = resources[8 + index];
        remains.active = true;
        remains.position = monster.position;

        if (monster.boss)
        {
            stats.complete = true;
            Notify(Feedback::Complete);
            SpawnDrop(monster.position, Item::Experience, 70);
            SpawnDrop({monster.position.x + 16, monster.position.y}, Item::Tool);
            SpawnDrop({monster.position.x - 16, monster.position.y}, Item::Healing);
            SpawnDrop({monster.position.x, monster.position.y + 16}, Item::Vacuum);
        }
        else
        {
            ++stats.kills;
            Notify(Feedback::Kill);
            // The first five enemies demonstrate every drop, without depending on lucky rolls.
            const Item lessons[] = {Item::Tool, Item::Healing, Item::Magnet, Item::Vacuum, Item::Experience};
            SpawnDrop(monster.position, Item::Experience, 12);
            SpawnDrop({monster.position.x + 8, monster.position.y + 8}, lessons[(stats.kills - 1) % 5],
                      stats.kills == 5 ? 24 : 1);
        }

        dirty = true;
    }

    void Move(Monster& monster, Point target, float seconds, float speed)
    {
        float length = std::sqrt(DistanceSquared(monster.position, target));

        if (length < 1)
        {
            return;
        }

        float distance = (std::min)(length, speed * seconds);
        Point next = {monster.position.x + (target.x - monster.position.x) / length * distance,
                      monster.position.y + (target.y - monster.position.y) / length * distance};
        float radius = monster.boss ? 23.0f : 12.0f;

        if (monster.boss && (next.x < 800 || next.x > 1280 || next.y < 1424 || next.y > 1904))
        {
            return;
        }

        if (Walkable({next.x, monster.position.y}, radius))
        {
            monster.position.x = next.x;
        }

        if (Walkable({monster.position.x, next.y}, radius))
        {
            monster.position.y = next.y;
        }
    }

    void UpdateMonsters(Point player, float seconds)
    {
        navigationTimer -= seconds;

        if (navigationTimer <= 0)
        {
            Flood(player);
            navigationTimer = .3f;
        }

        for (Monster& monster : monsters)
        {
            if (!Enabled(monster))
            {
                continue;
            }

            monster.cooldown = (std::max)(0.0f, monster.cooldown - seconds);
            float distance = std::sqrt(DistanceSquared(monster.position, player));
            float range = monster.boss ? 400.0f : 280.0f;

            if (monster.windup > 0)
            {
                monster.windup -= seconds;

                if (monster.windup <= 0)
                {
                    bool enraged = monster.boss && monster.health < monster.maxHealth * .5f;
                    int spread = monster.boss ? (enraged ? 2 : 1) : 0;

                    for (int shot = -spread; shot <= spread; ++shot)
                    {
                        Fire(monster.position, monster.aim, monster.boss ? 245.0f : 210.0f, range,
                             monster.boss ? 14.0f : 7.0f, true, shot * .20f);
                    }

                    monster.cooldown = monster.boss ? (enraged ? 1.0f : 1.6f) : 1.9f;
                }

                continue;
            }

            if (distance > (monster.boss ? 530.0f : 480.0f))
            {
                continue;
            }

            bool sight = ClearShot(monster.position, player);

            if (distance <= range && sight && monster.cooldown <= 0)
            {
                monster.aim = player;
                monster.windup = monster.boss ? .85f : .65f;
            }
            else if (distance > range * .70f || !sight)
            {
                Point destination = player;
                int current = Index(monster.position);

                if (!monster.boss && current >= 0)
                {
                    int x = current % Side;
                    int y = current / Side;
                    int best = current;
                    const int dx[] = {-1, 1, 0, 0};
                    const int dy[] = {0, 0, -1, 1};

                    for (int i = 0; i < 4; ++i)
                    {
                        int nx = x + dx[i];
                        int ny = y + dy[i];

                        if (nx < 0 || ny < 0 || nx >= Side || ny >= Side)
                        {
                            continue;
                        }

                        int next = ny * Side + nx;

                        if (distances[next] >= 0 &&
                            (distances[best] < 0 || distances[next] < distances[best]))
                        {
                            best = next;
                        }
                    }

                    destination = {static_cast<float>(best % Side * 32 + 16),
                                   static_cast<float>(best / Side * 32 + 16)};
                    // Align to the current cell before turning through a narrow corner.
                    float centerX = static_cast<float>(x * CellSize + CellSize / 2);
                    float centerY = static_cast<float>(y * CellSize + CellSize / 2);

                    if (best % Side != x && std::fabs(monster.position.y - centerY) > 1)
                    {
                        destination = {monster.position.x, centerY};
                    }
                    else if (best / Side != y && std::fabs(monster.position.x - centerX) > 1)
                    {
                        destination = {centerX, monster.position.y};
                    }
                }

                Move(monster, destination, seconds, monster.boss ? 55.0f : 75.0f);
            }
        }
    }

    void UpdateProjectiles(Point player, float seconds)
    {
        for (auto it = projectiles.begin(); it != projectiles.end();)
        {
            float speed = std::sqrt(it->velocity.x * it->velocity.x + it->velocity.y * it->velocity.y);
            float travel = (std::min)(it->remaining, speed * seconds);
            int steps = (std::max)(1, static_cast<int>(std::ceil(travel / 6)));
            bool hit = false;

            for (int step = 0; step < steps && !hit; ++step)
            {
                it->position.x += it->velocity.x / speed * travel / steps;
                it->position.y += it->velocity.y / speed * travel / steps;
                int cell = Index(it->position);

                if (cell < 0 || terrain[cell] == Cell::Rock)
                {
                    hit = true;
                }
                else if (it->hostile)
                {
                    if (DistanceSquared(it->position, player) < 20 * 20)
                    {
                        hit = true;

                        if (immunityTimer <= 0)
                        {
                            stats.health = (std::max)(0.0f, stats.health - it->damage);
                            immunityTimer = .55f;
                            dirty = true;
                        }
                    }
                }
                else
                {
                    for (size_t index = 0; index < monsters.size(); ++index)
                    {
                        Monster& monster = monsters[index];
                        float radius = monster.boss ? 36.0f : 22.0f;

                        if (Enabled(monster) &&
                            DistanceSquared(it->position, monster.position) < radius * radius)
                        {
                            monster.health -= it->damage;
                            dirty = true;

                            if (monster.health <= 0)
                            {
                                Defeat(index);
                            }

                            hit = true;
                            break;
                        }
                    }
                }
            }

            it->remaining -= travel;

            if (hit || it->remaining <= 0)
            {
                it = projectiles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void UpdateDrops(Point player, float seconds)
    {
        for (auto it = drops.begin(); it != drops.end();)
        {
            float distance = std::sqrt(DistanceSquared(it->position, player));
            float radius = PickupRadius();

            if (vacuumTimer > 0 && distance < 280 && ClearShot(it->position, player))
            {
                float step = (std::min)(distance, seconds * 480);

                if (distance > .01f)
                {
                    it->position.x += (player.x - it->position.x) / distance * step;
                    it->position.y += (player.y - it->position.y) / distance * step;
                }

                distance -= step;
            }

            if (distance > radius || !ClearShot(it->position, player))
            {
                ++it;
                continue;
            }

            switch (it->type)
            {
            case Item::Tool:
                stats.tool = (std::min)(10, stats.tool + 1);
                Notify(Feedback::Tool);
                break;

            case Item::Healing:
                stats.health = (std::min)(static_cast<float>(MaxHealth()), stats.health + 35);
                Notify(Feedback::Healing);
                break;

            case Item::Experience:
                Notify(Feedback::Experience);
                AddExperience(it->amount);
                break;

            case Item::Magnet:
                stats.magnet = (std::min)(6, stats.magnet + 1);
                Notify(Feedback::Magnet);
                break;

            case Item::Vacuum:
                vacuumTimer = 15;
                Notify(Feedback::Vacuum);
                break;
            }

            dirty = true;
            it = drops.erase(it);
        }
    }

    bool ValidPosition(Point p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 && p.x < Extent &&
               p.y < Extent;
    }

    bool Load(const std::string& data)
    {
        std::istringstream in(data);
        std::string magic;
        Stats candidate;
        int complete = 0;
        std::uint64_t candidateSeed = 0;
        float vacuum = 0;

        if (!(in >> magic >> candidateSeed >> candidate.level >> candidate.xp >> candidate.health >>
              candidate.tool >> candidate.magnet >> candidate.gathered >> candidate.remains >>
              candidate.kills >> complete >> vacuum))
        {
            return false;
        }

        if (magic != "LEVEL_ONE_V1" || candidateSeed == 0 || candidate.level < 1 || candidate.level > 20 ||
            candidate.xp < 0 || candidate.xp >= 25 + (candidate.level - 1) * 15 ||
            !std::isfinite(candidate.health) || candidate.health < 0 ||
            candidate.health > 100 + (candidate.level - 1) * 12 || candidate.tool < 0 ||
            candidate.tool > 10 || candidate.magnet < 0 || candidate.magnet > 6 || candidate.gathered < 0 ||
            candidate.gathered > 96 || candidate.remains < 0 || candidate.remains > 48 ||
            candidate.kills < 0 || candidate.kills > 6 || complete < 0 || complete > 1 ||
            !std::isfinite(vacuum) || vacuum < 0 || vacuum > 15)
        {
            return false;
        }

        seed = candidateSeed;
        Generate();

        for (Resource& resource : resources)
        {
            int active = 0;

            if (!(in >> resource.remaining >> active >> resource.position.x >> resource.position.y) ||
                resource.remaining < 0 || resource.remaining > 12 || active < 0 || active > 1 ||
                !ValidPosition(resource.position) || !Walkable(resource.position))
            {
                return false;
            }

            resource.active = active != 0;
        }

        int dead = 0;

        for (Monster& monster : monsters)
        {
            if (!(in >> monster.health >> monster.position.x >> monster.position.y) ||
                !ValidPosition(monster.position) ||
                !Walkable(monster.position, monster.boss ? 23.0f : 12.0f) || !std::isfinite(monster.health) ||
                monster.health < 0 || monster.health > monster.maxHealth)
            {
                return false;
            }

            if (!monster.boss && monster.health == 0)
            {
                ++dead;
            }
        }

        size_t count = 0;

        if (!(in >> count) || count > 64 || dead != candidate.kills ||
            (monsters.back().health == 0) != (complete != 0))
        {
            return false;
        }

        int harvestedPlants = 0;
        int harvestedRemains = 0;

        for (size_t i = 0; i < resources.size(); ++i)
        {
            if (i < 8)
            {
                if (!resources[i].active)
                {
                    return false;
                }

                harvestedPlants += 12 - resources[i].remaining;
            }
            else
            {
                int capacity = i == 14 ? 12 : 6;

                if (resources[i].remaining > capacity ||
                    resources[i].active != (monsters[i - 8].health == 0) ||
                    (!resources[i].active && resources[i].remaining != capacity))
                {
                    return false;
                }

                harvestedRemains += capacity - resources[i].remaining;
            }
        }

        if (candidate.gathered != harvestedPlants || candidate.remains != harvestedRemains)
        {
            return false;
        }

        for (size_t i = 0; i < count; ++i)
        {
            Drop drop;
            int type = 0;

            if (!(in >> drop.position.x >> drop.position.y >> type >> drop.amount) ||
                !ValidPosition(drop.position) || type < 0 || type > 4 || drop.amount < 1 || drop.amount > 100)
            {
                return false;
            }

            drop.type = static_cast<Item>(type);
            drops.push_back(drop);
        }

        in >> std::ws;

        if (!in.eof())
        {
            return false;
        }

        candidate.complete = complete != 0;
        stats = candidate;
        vacuumTimer = vacuum;
        awake = BossAwake();
        return true;
    }
} // namespace

bool LevelOne::Initialize()
{
    stats = Stats{};
    writable = saved = true;
    dirty = false;
    awake = false;
    pendingHerbs = 0;
    attackTimer = gatherTimer = immunityTimer = vacuumTimer = navigationTimer = feedbackTimer = dirtyTimer =
        0;
    aimTarget = gatherTarget = -1;
    feedback = Feedback::None;

    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    seed = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) ^ time.dwLowDateTime ^ GetTickCount64();

    if (seed == 0)
    {
        seed = 1;
    }

    std::string data;
    int state = InfiniteWorld::LoadLevelState(data);

    if (state == 1 && Load(data))
    {
        return false;
    }

    Generate();
    writable = state == 0;
    saved = writable;
    dirty = writable;
    return state == 0;
}

bool LevelOne::Save()
{
    if (!writable)
    {
        return false;
    }

    std::ostringstream out;
    out << std::setprecision(9) << "LEVEL_ONE_V1 " << seed << ' ' << stats.level << ' ' << stats.xp << ' '
        << stats.health << ' ' << stats.tool << ' ' << stats.magnet << ' ' << stats.gathered << ' '
        << stats.remains << ' ' << stats.kills << ' ' << (stats.complete ? 1 : 0) << ' ' << vacuumTimer
        << '\n';

    for (const Resource& resource : resources)
    {
        out << resource.remaining << ' ' << (resource.active ? 1 : 0) << ' ' << resource.position.x << ' '
            << resource.position.y << '\n';
    }

    for (const Monster& monster : monsters)
    {
        out << monster.health << ' ' << monster.position.x << ' ' << monster.position.y << '\n';
    }

    out << drops.size() << '\n';

    for (const Drop& drop : drops)
    {
        out << drop.position.x << ' ' << drop.position.y << ' ' << static_cast<int>(drop.type) << ' '
            << drop.amount << '\n';
    }

    saved = InfiniteWorld::SaveLevelState(out.str());

    if (saved)
    {
        dirty = false;
    }

    dirtyTimer = 0;
    return saved;
}

bool LevelOne::StorageHealthy()
{
    return writable && saved;
}

bool LevelOne::OwnsChunk(InfiniteWorld::Key key)
{
    return key.x >= 1 && key.x <= 2 && key.y >= 5 && key.y <= 6;
}

bool LevelOne::OffsetFor(InfiniteWorld::Key origin, float& x, float& y)
{
    if (origin.x < -2 || origin.x > 5 || origin.y < 2 || origin.y > 9)
    {
        return false;
    }

    x = static_cast<float>(1 - origin.x) * InfiniteWorld::ChunkSize;
    y = static_cast<float>(5 - origin.y) * InfiniteWorld::ChunkSize;
    return true;
}

bool LevelOne::ToLocal(InfiniteWorld::Key origin, float worldX, float worldY, Point& local)
{
    float dx = 0;
    float dy = 0;

    if (!OffsetFor(origin, dx, dy))
    {
        return false;
    }

    local = {worldX - dx, worldY - dy};
    return ValidPosition(local);
}

bool LevelOne::Sample(InfiniteWorld::Key origin, float x, float y, Cell& cell)
{
    Point local;

    if (!ToLocal(origin, x, y, local))
    {
        return false;
    }

    cell = terrain[Index(local)];
    return true;
}

bool LevelOne::CanStand(InfiniteWorld::Key origin, float x, float y)
{
    for (int row = -1; row <= 1; ++row)
    {
        for (int col = -1; col <= 1; ++col)
        {
            Point local;

            if (ToLocal(origin, x + col * 13, y + row * 8, local) && Solid(Index(local)))
            {
                return false;
            }
        }
    }

    return true;
}

void LevelOne::Entrance(InfiniteWorld::Key& origin, float& x, float& y)
{
    origin = {1, 5};
    x = Spawn.x;
    y = Spawn.y;
    origin = InfiniteWorld::Resolve(origin, x, y);
}

void LevelOne::Update(float seconds, InfiniteWorld::Key origin, float x, float y, bool gather, bool attack,
                      bool paused)
{
    feedbackTimer = (std::max)(0.0f, feedbackTimer - seconds);
    dirtyTimer += seconds;
    Point player;

    if (paused || !ToLocal(origin, x, y, player) || stats.health <= 0)
    {
        ClearInput();
        projectiles.clear();

        for (Monster& monster : monsters)
        {
            monster.windup = 0;
        }

        return;
    }

    immunityTimer = (std::max)(0.0f, immunityTimer - seconds);
    vacuumTimer = (std::max)(0.0f, vacuumTimer - seconds);
    attackTimer = (std::max)(0.0f, attackTimer - seconds);

    if (!awake && BossAwake())
    {
        awake = true;
        Notify(Feedback::BossAwake);
        dirty = true;
    }

    aimTarget = -1;
    float nearest = AttackRange * AttackRange;

    for (size_t i = 0; i < monsters.size(); ++i)
    {
        float distance = DistanceSquared(monsters[i].position, player);

        if (Enabled(monsters[i]) && distance <= nearest && ClearShot(player, monsters[i].position))
        {
            nearest = distance;
            aimTarget = static_cast<int>(i);
        }
    }

    if (attack && aimTarget >= 0 && attackTimer <= 0)
    {
        Fire(player, monsters[aimTarget].position, 560, AttackRange,
             static_cast<float>(12 + stats.level * 2 + stats.tool * 4), false);
        attackTimer = (std::max)(.22f, .55f - stats.tool * .025f);
    }

    int target = -1;
    nearest = GatherRange * GatherRange;

    for (size_t i = 0; i < resources.size(); ++i)
    {
        const Resource& resource = resources[i];
        float distance = DistanceSquared(resource.position, player);

        if (resource.active && resource.remaining > 0 && distance <= nearest &&
            ClearShot(player, resource.position))
        {
            nearest = distance;
            target = static_cast<int>(i);
        }
    }

    if (target != gatherTarget || !gather)
    {
        gatherTimer = 0;
    }

    gatherTarget = target;

    if (gather && gatherTarget >= 0)
    {
        gatherTimer += seconds;

        if (gatherTimer >= GatherSeconds())
        {
            Resource& resource = resources[gatherTarget];
            int quantity = (std::min)(resource.remaining, GatherYield());
            resource.remaining -= quantity;
            gatherTimer = 0;

            if (resource.remains)
            {
                stats.remains += quantity;
            }
            else
            {
                stats.gathered += quantity;
                pendingHerbs += quantity;
            }

            Notify(Feedback::Gather);
            AddExperience(quantity * (resource.remains ? 5 : 4));
            dirty = true;
        }
    }

    UpdateMonsters(player, seconds);
    UpdateProjectiles(player, seconds);

    if (stats.health <= 0)
    {
        Notify(Feedback::Downed);
        ClearInput();
        projectiles.clear();
        dirty = true;
        return;
    }

    UpdateDrops(player, seconds);
}

void LevelOne::Respawn()
{
    if (stats.health > 0)
    {
        return;
    }

    stats.health = static_cast<float>(MaxHealth());
    stats.xp = static_cast<int>(stats.xp * .9f);
    projectiles.clear();

    for (Monster& monster : monsters)
    {
        monster.position = monster.home;
        monster.windup = 0;
        monster.cooldown = 1;

        if (monster.health > 0)
        {
            monster.health = monster.maxHealth;
        }
    }

    immunityTimer = 2;
    Notify(Feedback::Respawn);
    dirty = true;
}

void LevelOne::ClearInput()
{
    gatherTimer = 0;
    gatherTarget = -1;
    aimTarget = -1;
}

bool LevelOne::NeedsSave()
{
    return dirty && dirtyTimer >= 1;
}

int LevelOne::TakeHerbs()
{
    int quantity = pendingHerbs;
    pendingHerbs = 0;
    return quantity;
}

int LevelOne::MaxHealth()
{
    return 100 + (stats.level - 1) * 12;
}

int LevelOne::NextExperience()
{
    return 25 + (stats.level - 1) * 15;
}

int LevelOne::GatherYield()
{
    return (std::min)(6, 1 + (stats.level - 1) / 3);
}

float LevelOne::GatherSeconds()
{
    return 1.6f / (1 + (stats.level - 1) * .08f + stats.tool * .07f);
}

float LevelOne::PickupRadius()
{
    return 26.0f + stats.magnet * 24.0f;
}

float LevelOne::VacuumSeconds()
{
    return vacuumTimer;
}

bool LevelOne::BossAwake()
{
    return stats.gathered >= 6 && stats.kills >= 3 && stats.remains >= 1;
}

int LevelOne::AimTarget()
{
    return aimTarget;
}

int LevelOne::GatherTarget()
{
    return gatherTarget;
}

float LevelOne::GatherProgress()
{
    return gatherTimer / GatherSeconds();
}

LevelOne::Feedback LevelOne::CurrentFeedback()
{
    return feedbackTimer > 0 ? feedback : Feedback::None;
}

const LevelOne::Stats& LevelOne::Player()
{
    return stats;
}

const std::array<LevelOne::Cell, LevelOne::Side * LevelOne::Side>& LevelOne::Terrain()
{
    return terrain;
}

const std::vector<LevelOne::Resource>& LevelOne::Resources()
{
    return resources;
}

const std::vector<LevelOne::Monster>& LevelOne::Monsters()
{
    return monsters;
}

const std::vector<LevelOne::Projectile>& LevelOne::Projectiles()
{
    return projectiles;
}

const std::vector<LevelOne::Drop>& LevelOne::Drops()
{
    return drops;
}
