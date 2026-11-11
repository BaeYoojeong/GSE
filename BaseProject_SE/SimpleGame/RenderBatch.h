#pragma once
#include "MeshCache.h"

namespace RenderBatch
{
    struct Instance
    {
        float x = 0, y = 0, sx = 1, sy = 1;
        float r = 1, g = 1, b = 1, a = 1;
        float projection = 0, opacity = 1, emissionScale = 1, radiance = 1;
        std::uint32_t first = 0, count = 0;
        float localX = 0, localY = 0;
    };

    bool Initialize(GLuint worldProgram);
    void Submit(const MeshCache::Handle& mesh, Instance instance, GLuint texture = 0, int textureMode = 1);
    // Flush before changing framebuffer, shader-wide uniforms, pass, or deleting a queued texture.
    void Flush(const char* reason);
    void Shutdown();
} // namespace RenderBatch
