#pragma once
#include <filesystem>
#include <string>

namespace BlenderBridge
{
    void Configure(const std::filesystem::path& root, const std::filesystem::path& manifest,
                   const std::filesystem::path& blend, const std::filesystem::path& executable,
                   bool automatic);
    void Request();
    bool Poll();
    void Shutdown();
    std::filesystem::path Resolve(const std::filesystem::path& root, const std::string& id,
                                  const std::filesystem::path& source);
} // namespace BlenderBridge
