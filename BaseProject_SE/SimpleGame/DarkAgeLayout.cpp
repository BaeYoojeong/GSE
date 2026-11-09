#include "DarkAgeGame.h"

namespace Ashen
{
    Vec2 Game::FarmingPosition(ChunkKey key, int slot) const
    {
        const Region* region = RegionAt(key);
        const Vec2 legacy[] = {{294, 449}, {464, 449}, {248, 530}, {502, 520}, {350, 650}, {430, 672}};

        if (region && region->layoutVersion == 1)
        {
            return legacy[slot];
        }

        // The southern field and the road clearing have no solid obstacles.
        // Separate cells keep every gathering point accessible and readable.
        const Vec2 cells[] = {{294, 440}, {464, 440}, {220, 555}, {535, 555}, {315, 675}, {450, 675}};
        const auto hash = Hash(key, 1600 + slot);
        const int spread = slot < 2 ? 12 : 24;
        const int diameter = spread * 2 + 1;

        return {cells[slot].x + static_cast<int>(hash % diameter) - spread,
                cells[slot].y + static_cast<int>((hash >> 32) % diameter) - spread};
    }

    Vec2 Game::EnemyPosition(ChunkKey key, int slot) const
    {
        const Region* region = RegionAt(key);
        const Vec2 legacy[] = {{160, 566}, {590, 616}};

        if (region && region->layoutVersion == 1)
        {
            return legacy[slot];
        }

        const auto hash = Hash(key, 1700 + slot);

        return {(slot == 0 ? 130.0f : 650.0f) + static_cast<int>(hash % 41) - 20,
                560.0f + static_cast<int>((hash >> 32) % 41) - 20};
    }
} // namespace Ashen
