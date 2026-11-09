#pragma once

#include "MeshCache.h"

namespace AshenGeometry
{
    enum class CharacterRole
    {
        Traveler,
        Enemy,
        Settler,
        Wanderer,
        Guardian,
        Giver,
        Cautious,
        Merchant,
        Hunter,
        Raider
    };

    // Project local X/Y ground coordinates and Z height into a cached upright mesh.
    // The renderer adds the projected actor anchor exactly once.
    void Building(std::vector<MeshCache::Vertex>& vertices, bool chapel, bool lit, unsigned variant);
    void Rubble(std::vector<MeshCache::Vertex>& vertices);
    void Grave(std::vector<MeshCache::Vertex>& vertices);
    void Player(std::vector<MeshCache::Vertex>& vertices, unsigned frame, int facing, bool dead);
    void Character(std::vector<MeshCache::Vertex>& vertices, CharacterRole role, unsigned frame, int facing,
                   bool dead = false, bool attacking = false);
} // namespace AshenGeometry
