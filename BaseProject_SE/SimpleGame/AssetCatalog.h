#pragma once
#include "GltfModel.h"
#include <filesystem>
#include <map>
#include <string>

namespace AssetCatalog
{
    struct Request
    {
        std::string asset;
        std::string clip = "Idle";
        float seconds = 0;
        bool loop = true;
        std::string previousClip;
        float previousSeconds = 0, blend = 1;
        bool previousLoop = true, suppressRootRotation = false;
        float yawRadians = 0;
        float maximumFootprint = 0;
        std::map<std::string, unsigned> parts;
    };

    struct Prepared
    {
        std::shared_ptr<const GltfModel::Model> model;
        GltfModel::Pose pose;
        GltfModel::Projection projection;
        MeshCache::Key key = {};
        bool animated = false;

        explicit operator bool() const
        {
            return model != nullptr;
        }
    };

    std::string RoleAsset(const std::string& role, std::uint64_t identity);
    std::string ClipName(const std::string& state);
    float ClipDuration(const std::string& state);
    void ExportBlender();
    void Initialize();
    void Poll();
    void Reload();
    void Shutdown();
    Prepared Prepare(const Request& request);
    std::wstring Status();
    std::map<std::string, unsigned> NpcParts(std::uint64_t identity, unsigned outfit);
} // namespace AssetCatalog
