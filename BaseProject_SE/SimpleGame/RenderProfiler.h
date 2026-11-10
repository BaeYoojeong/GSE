#pragma once
#include "Dependencies/glew.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace RenderProfiler
{
    enum class Pass
    {
        Ground,
        Shadow,
        World,
        Post,
        UI,
        Other,
        Count
    };

    struct Diagnostic
    {
        std::string category, detail;
        std::uint64_t bytes = 0;
        double milliseconds = 0;
    };

    void BeginCapture(std::vector<Diagnostic>& target);
    void EndCapture();

    struct FrameCounters
    {
        std::uint64_t drawCalls = 0, meshBuilds = 0, uploadBytes = 0;
        std::uint64_t cpuCacheHits = 0, evictions = 0;
    };

    FrameCounters CurrentFrame();
    void Initialize();
    void Shutdown();
    std::filesystem::path DataDirectory();
    std::string Environment(const char* name);
    void BeginLoop();
    void CancelLoop();
    void BeginFrame();
    void EndFrame();
    void SetPass(Pass pass);
    void DrawArrays(GLenum mode, GLint first, GLsizei count);
    void DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances);
    void Event(const char* category, const std::string& detail, std::uint64_t bytes = 0, double ms = 0);
    void CacheEvent(const char* action, const std::string& key, std::uint64_t bytes = 0, double ms = 0);
    void MeshBuilt(std::size_t vertices, double ms);
    void Uploaded(std::size_t bytes);
    void Batch(const char* reason, std::size_t instances, std::uint64_t useful, std::uint64_t submitted);
    void Visibility(bool visible);
    void TextBuilt();
} // namespace RenderProfiler
