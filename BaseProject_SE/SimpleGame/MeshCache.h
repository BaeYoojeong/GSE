#pragma once

#include "Dependencies/glew.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace MeshCache
{
    struct Vertex
    {
        float x, y, r, g, b, a, u, v, emission;
    };

    using Key = std::array<std::uint32_t, 10>;

    struct Mesh
    {
        std::uint32_t first = 0;
        GLsizei count = 0;
        std::size_t bytes = 0;
        std::uint64_t lastUse = 0;

        Mesh() = default;
        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
    };

    using Handle = std::shared_ptr<const Mesh>;

    struct Statistics
    {
        std::size_t meshes = 0;
        std::size_t bytes = 0;
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
    };

    constexpr std::size_t MaxMeshes = 1024;
    constexpr std::size_t MaxBytes = 64 * 1024 * 1024;

    bool Initialize();
    GLuint AtlasTexture();
    Handle Find(const Key& key, bool allowDisk = true);
    Handle Store(const Key& key, const std::vector<Vertex>& vertices, bool persist = true);
    void BindVertexLayout();
    Statistics GetStatistics();
    // Release queued handles before shutdown, while the GL context is still current.
    void Shutdown();
} // namespace MeshCache
