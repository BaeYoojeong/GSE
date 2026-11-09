#pragma once
#include "MeshCache.h"
#include <string>

namespace DiskMeshCache
{
    // Bump when model generation or camera-dependent baked geometry changes.
    constexpr std::uint32_t GeometryRevision = 3;
    void Initialize();
    bool Load(const MeshCache::Key& key, std::vector<MeshCache::Vertex>& vertices);
    void Save(const MeshCache::Key& key, const std::vector<MeshCache::Vertex>& vertices);
    std::string KeyName(const MeshCache::Key& key);
} // namespace DiskMeshCache
