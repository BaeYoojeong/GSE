#pragma once
#include "MeshCache.h"

namespace ChapterGeometry
{
    void Tower(std::vector<MeshCache::Vertex>& vertices, bool lit);
    void Keeper(std::vector<MeshCache::Vertex>& vertices, bool enraged);
    void Residence(std::vector<MeshCache::Vertex>& vertices, unsigned style);
} // namespace ChapterGeometry
