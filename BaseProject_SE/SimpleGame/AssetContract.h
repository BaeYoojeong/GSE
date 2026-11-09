#pragma once
#include "GltfModel.h"
#include <filesystem>

namespace AssetContract
{
    struct Rules
    {
        std::map<std::string, std::vector<std::string>> bindings;
        std::map<std::string, std::string> clips = {
            {"idle", "Idle"}, {"walk", "Walk"},          {"run", "Run"},
            {"jump", "Jump"}, {"turn_left", "TurnLeft"}, {"turn_right", "TurnRight"}};
        std::map<std::string, float> durations = {{"idle", 2.f},  {"walk", 1.f},      {"run", .8f},
                                                  {"jump", 1.2f}, {"turn_left", 1.f}, {"turn_right", 1.f}};
        float scale = 30, poseFps = 12;
        GltfModel::Limits limits;
        std::string rootBone = "root";
    };

    Rules Read(const std::vector<std::filesystem::path>& documents, const std::string& manifest, float scale,
               float poseFps);
    std::string Select(const Rules& rules, const std::string& role, std::uint64_t identity);
} // namespace AssetContract
