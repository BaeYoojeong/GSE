#pragma once
#include "MeshCache.h"
#include <string>

namespace BlenderAssets
{
    // Versioned exported meshes, shared by every instance. Missing assets use procedural fallbacks.
    bool Append(const std::string& name, std::vector<MeshCache::Vertex>& vertices);
    std::uint32_t Revision(const std::string& name);
    void Clear();
} // namespace BlenderAssets
