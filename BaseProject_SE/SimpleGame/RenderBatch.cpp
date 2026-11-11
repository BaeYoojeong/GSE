#include "RenderBatch.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    GLuint program = 0, vao = 0, buffer = 0, texture = 0;
    int textureMode = 1;
    GLint instancingLocation = -1, textureLocation = -1;
    std::vector<RenderBatch::Instance> instances;
    std::vector<MeshCache::Handle> leases;
    std::uint32_t maximumVertices = 0;
    std::uint64_t usefulVertices = 0;
    bool enabled = true;
    constexpr std::size_t MaxInstances = 1024;
} // namespace

bool RenderBatch::Initialize(GLuint worldProgram)
{
    static_assert(sizeof(Instance) == 64, "Instance layout must match World.vs attributes 4-8.");
    program = worldProgram;
    enabled = RenderProfiler::Environment("ASHEN_BATCHING") != "0";
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &buffer);
    if (!vao || !buffer)
    {
        return false;
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, x)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, r)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, projection)));
    glEnableVertexAttribArray(7);
    glVertexAttribIPointer(7, 2, GL_UNSIGNED_INT, sizeof(Instance),
                           reinterpret_cast<void*>(offsetof(Instance, first)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, localX)));
    for (GLuint attribute = 4; attribute <= 8; ++attribute)
    {
        glVertexAttribDivisor(attribute, 1);
    }
    glBindVertexArray(0);
    instancingLocation = glGetUniformLocation(program, "useInstancing");
    textureLocation = glGetUniformLocation(program, "useTexture");
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "geometryAtlas"), 6);
    instances.reserve(MaxInstances);
    leases.reserve(MaxInstances);
    RenderProfiler::Event(
        "batch_config",
        enabled ? "enabled; ordered heterogeneous atlas instancing; max_instances=1024; max_padding=2"
                : "disabled; one instanced submission per item for comparison");
    return instancingLocation >= 0;
}

void RenderBatch::Submit(const MeshCache::Handle& mesh, Instance instance, GLuint image, int imageMode)
{
    if (!mesh)
    {
        return;
    }
    const auto count = static_cast<std::uint32_t>(mesh->count);
    const auto maximum = (std::max)(maximumVertices, count);
    if (!instances.empty())
    {
        if (image != texture || imageMode != textureMode)
        {
            Flush("texture");
        }
        else if (instances.size() >= MaxInstances)
        {
            Flush("capacity");
        }
        else if (static_cast<std::uint64_t>(maximum) * (instances.size() + 1) > 2 * (usefulVertices + count))
        {
            Flush("vertex_padding");
        }
    }
    instance.first = mesh->first;
    instance.count = count;
    texture = image;
    textureMode = imageMode;
    maximumVertices = (std::max)(maximumVertices, count);
    usefulVertices += count;
    instances.push_back(instance);
    leases.push_back(mesh);
    if (!enabled)
    {
        Flush("comparison_mode");
    }
}

void RenderBatch::Flush(const char* reason)
{
    if (instances.empty())
    {
        return;
    }
    glUseProgram(program);
    glUniform1i(instancingLocation, 1);
    glUniform1i(textureLocation, texture ? textureMode : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_BUFFER, MeshCache::AtlasTexture());
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const auto bytes = instances.size() * sizeof(Instance);
    glBufferData(GL_ARRAY_BUFFER, bytes, instances.data(), GL_STREAM_DRAW);
    RenderProfiler::Uploaded(bytes);
    RenderProfiler::DrawArraysInstanced(GL_TRIANGLES, 0, static_cast<GLsizei>(maximumVertices),
                                        static_cast<GLsizei>(instances.size()));
    RenderProfiler::Batch(reason, instances.size(), usefulVertices,
                          static_cast<std::uint64_t>(maximumVertices) * instances.size());
    instances.clear();
    leases.clear();
    maximumVertices = 0;
    usefulVertices = 0;
    glUniform1i(instancingLocation, 0);
}

void RenderBatch::Shutdown()
{
    // The context is still current; outstanding work should already have been flushed by EndFrame.
    instances.clear();
    leases.clear();
    maximumVertices = 0;
    usefulVertices = 0;
    if (buffer)
    {
        glDeleteBuffers(1, &buffer);
    }
    if (vao)
    {
        glDeleteVertexArrays(1, &vao);
    }
    program = vao = buffer = texture = 0;
    textureMode = 1;
}
