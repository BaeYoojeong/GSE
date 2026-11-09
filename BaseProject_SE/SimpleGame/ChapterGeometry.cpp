#include "ChapterGeometry.h"
#include "BlenderAssets.h"
#include "DarkAgeGeometry.h"

namespace ChapterGeometry
{
    void Tower(std::vector<MeshCache::Vertex>& vertices, bool lit)
    {
        if (!BlenderAssets::Append(lit ? "tower_lit" : "tower_dark", vertices))
        {
            AshenGeometry::Building(vertices, true, lit, 0);
        }
    }

    void Keeper(std::vector<MeshCache::Vertex>& vertices, bool enraged)
    {
        if (!BlenderAssets::Append(enraged ? "keeper_enraged" : "keeper_dark", vertices))
        {
            AshenGeometry::Character(vertices, AshenGeometry::CharacterRole::Enemy, 0, 3, false, enraged);
        }
    }

    void Residence(std::vector<MeshCache::Vertex>& vertices, unsigned style)
    {
        if (!BlenderAssets::Append("cottage_" + std::to_string(style % 4), vertices))
        {
            const auto start = vertices.size();
            AshenGeometry::Building(vertices, false, true, style % 4);
            for (auto i = start; i < vertices.size(); ++i)
            {
                vertices[i].x *= .55f;
                vertices[i].y *= .55f;
            }
        }
    }
} // namespace ChapterGeometry
