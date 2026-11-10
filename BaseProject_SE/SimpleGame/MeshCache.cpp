#include "MeshCache.h"
#include "DiskMeshCache.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    std::map<MeshCache::Key, std::shared_ptr<MeshCache::Mesh>> meshes;

    struct Span
    {
        std::uint32_t first, count;
    };

    std::vector<Span> freeSpans;
    GLuint atlasBuffer = 0, atlasTexture = 0;
    std::size_t allocatedBytes = 0;
    std::uint64_t accessCounter = 0, hits = 0, misses = 0, cpuHits = 0;

    struct CpuMesh
    {
        std::vector<MeshCache::Vertex> vertices;
        std::uint64_t use = 0;
        bool fixed = true;
    };

    std::map<MeshCache::Key, CpuMesh> cpuMeshes;
    std::size_t cpuBytes = 0;
    constexpr std::size_t CpuLimit = 64 * 1024 * 1024;

    void Remember(const MeshCache::Key& key, const std::vector<MeshCache::Vertex>& vertices, bool fixed)
    {
        auto found = cpuMeshes.find(key);
        if (found != cpuMeshes.end())
        {
            found->second.use = ++accessCounter;
            return;
        }
        const auto bytes = vertices.size() * sizeof(MeshCache::Vertex);
        if (bytes > CpuLimit)
        {
            return;
        }
        while ((cpuBytes + bytes > CpuLimit || cpuMeshes.size() >= 4096) && !cpuMeshes.empty())
        {
            auto victim =
                std::min_element(cpuMeshes.begin(), cpuMeshes.end(),
                                 [](const auto& a, const auto& b) { return a.second.use < b.second.use; });
            cpuBytes -= victim->second.vertices.size() * sizeof(MeshCache::Vertex);
            cpuMeshes.erase(victim);
        }
        cpuMeshes.emplace(key, CpuMesh{vertices, ++accessCounter, fixed});
        cpuBytes += bytes;
    }

    void Free(Span span)
    {
        freeSpans.push_back(span);
        std::sort(freeSpans.begin(), freeSpans.end(), [](Span a, Span b) { return a.first < b.first; });
        for (std::size_t i = 1; i < freeSpans.size();)
        {
            auto& previous = freeSpans[i - 1];
            if (previous.first + previous.count == freeSpans[i].first)
            {
                previous.count += freeSpans[i].count;
                freeSpans.erase(freeSpans.begin() + i);
            }
            else
            {
                ++i;
            }
        }
    }

    bool Allocate(std::uint32_t count, std::uint32_t& first, bool fixed)
    {
        for (;;)
        {
            if (meshes.size() < MeshCache::MaxMeshes)
            {
                for (auto it = freeSpans.begin(); it != freeSpans.end(); ++it)
                {
                    if (it->count < count)
                    {
                        continue;
                    }
                    first = it->first;
                    it->first += count;
                    it->count -= count;
                    if (!it->count)
                    {
                        freeSpans.erase(it);
                    }
                    return true;
                }
            }
            auto oldest = meshes.end();
            for (auto it = meshes.begin(); it != meshes.end(); ++it)
            {
                // Queued instances own handles. Their atlas ranges cannot be reused until flushed.
                if (it->second.use_count() == 1 &&
                    (oldest == meshes.end() ||
                     (it->second->staticGeometry == fixed && oldest->second->staticGeometry != fixed) ||
                     ((it->second->staticGeometry == fixed) == (oldest->second->staticGeometry == fixed) &&
                      it->second->lastUse < oldest->second->lastUse)))
                {
                    oldest = it;
                }
            }
            if (oldest == meshes.end())
            {
                return false;
            }
            Free({oldest->second->first, static_cast<std::uint32_t>(oldest->second->count)});
            allocatedBytes -= oldest->second->bytes;
            RenderProfiler::CacheEvent("evict", DiskMeshCache::KeyName(oldest->first), oldest->second->bytes);
            meshes.erase(oldest);
        }
    }
} // namespace

bool MeshCache::Initialize()
{
    if (atlasBuffer)
    {
        return true;
    }
    DiskMeshCache::Initialize();
    GLint texels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &texels);
    if (texels < 3)
    {
        return false;
    }
    const auto capacity =
        static_cast<std::uint32_t>((std::min)(MaxBytes / 48, static_cast<std::size_t>(texels / 3)));
    glGenBuffers(1, &atlasBuffer);
    glGenTextures(1, &atlasTexture);
    glBindBuffer(GL_TEXTURE_BUFFER, atlasBuffer);
    glBufferData(GL_TEXTURE_BUFFER, capacity * 48, nullptr, GL_STATIC_DRAW);
    GLint bytes = 0;
    glGetBufferParameteriv(GL_TEXTURE_BUFFER, GL_BUFFER_SIZE, &bytes);
    if (!atlasBuffer || !atlasTexture || bytes != static_cast<GLint>(capacity * 48))
    {
        Shutdown();
        return false;
    }
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_BUFFER, atlasTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, atlasBuffer);
    glActiveTexture(GL_TEXTURE0);
    freeSpans = {{0, capacity}};
    RenderProfiler::Event("atlas", "RGBA32F; 3 texels/vertex; maximum vertices=" + std::to_string(capacity),
                          capacity * 48);
    return true;
}

GLuint MeshCache::AtlasTexture()
{
    return atlasTexture;
}

MeshCache::Handle MeshCache::Find(const Key& key, bool allowDisk)
{
    auto found = meshes.find(key);
    if (found != meshes.end())
    {
        ++hits;
        found->second->lastUse = ++accessCounter;
        RenderProfiler::CacheEvent("memory_hit", "");
        return found->second;
    }
    ++misses;
    RenderProfiler::CacheEvent("memory_miss", "");
    const auto cpu = cpuMeshes.find(key);
    if (cpu != cpuMeshes.end())
    {
        ++cpuHits;
        cpu->second.use = ++accessCounter;
        auto mesh = Store(key, cpu->second.vertices, false, cpu->second.fixed);
        if (mesh)
        {
            RenderProfiler::CacheEvent("cpu_hit", "");
            return mesh;
        }
    }
    std::vector<Vertex> vertices;
    if (allowDisk && DiskMeshCache::Load(key, vertices))
    {
        return Store(key, vertices, false);
    }
    return {};
}

MeshCache::Handle MeshCache::Store(const Key& key, const std::vector<Vertex>& vertices, bool persist,
                                   bool staticGeometry)
{
    auto found = meshes.find(key);
    if (found != meshes.end())
    {
        return found->second;
    }
    if (vertices.empty() || vertices.size() % 3 || vertices.size() > MaxBytes / 48 || !atlasBuffer)
    {
        return {};
    }
    std::vector<std::array<float, 12>> packed;
    packed.reserve(vertices.size());
    for (const Vertex& v : vertices)
    {
        for (float value : {v.x, v.y, v.r, v.g, v.b, v.a, v.u, v.v, v.emission})
        {
            if (!std::isfinite(value))
            {
                return {};
            }
        }
        packed.push_back({v.x, v.y, v.r, v.g, v.b, v.a, v.u, v.v, v.emission, 0, 0, 0});
    }
    Remember(key, vertices, staticGeometry);
    std::uint32_t first = 0;
    if (!Allocate(static_cast<std::uint32_t>(vertices.size()), first, staticGeometry))
    {
        RenderProfiler::CacheEvent("atlas_full", DiskMeshCache::KeyName(key));
        return {};
    }
    const auto bytes = packed.size() * sizeof(packed[0]);
    glBindBuffer(GL_TEXTURE_BUFFER, atlasBuffer);
    glBufferSubData(GL_TEXTURE_BUFFER, static_cast<GLintptr>(first) * 48, bytes, packed.data());
    RenderProfiler::Uploaded(bytes);
    auto mesh = std::make_shared<Mesh>();
    mesh->first = first;
    mesh->count = static_cast<GLsizei>(vertices.size());
    mesh->bytes = bytes;
    mesh->lastUse = ++accessCounter;
    mesh->staticGeometry = staticGeometry;
    allocatedBytes += bytes;
    meshes.emplace(key, mesh);
    if (persist)
    {
        DiskMeshCache::Save(key, vertices);
    }
    return mesh;
}

void MeshCache::BindVertexLayout()
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, emission)));
}

MeshCache::Statistics MeshCache::GetStatistics()
{
    return {meshes.size(), allocatedBytes, hits, misses, cpuBytes, cpuHits};
}

void MeshCache::Shutdown()
{
    DiskMeshCache::Shutdown();
    cpuMeshes.clear();
    cpuBytes = 0;
    cpuHits = 0;
    meshes.clear();
    freeSpans.clear();
    if (atlasTexture)
    {
        glDeleteTextures(1, &atlasTexture);
    }
    if (atlasBuffer)
    {
        glDeleteBuffers(1, &atlasBuffer);
    }
    atlasTexture = atlasBuffer = 0;
    allocatedBytes = 0;
    accessCounter = hits = misses = 0;
}
