#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "BlenderAssets.h"
#include "RenderProfiler.h"
#include <cmath>
#include <utility>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>

namespace BlenderAssets
{
    namespace
    {
        struct Asset
        {
            std::vector<MeshCache::Vertex> vertices;
            std::uint32_t revision = 0;
        };

        std::map<std::string, Asset> assets;

        const Asset& Load(const std::string& name)
        {
            const auto found = assets.find(name);
            if (found != assets.end())
            {
                return found->second;
            }
            Asset asset;
            wchar_t executable[32768] = {};
            const auto length = GetModuleFileNameW(nullptr, executable, 32768);
            bool valid = length > 0 && length < 32768 &&
                         name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789") == std::string::npos;
            if (valid)
            {
                const auto path = std::filesystem::path(executable).parent_path() / L"Assets" / L"Blender" /
                                  (name + ".ashmesh");
                std::ifstream in(path);
                in.imbue(std::locale::classic());
                std::string magic;
                size_t count = 0;
                valid = static_cast<bool>(in >> magic >> count) && magic == "ASHEN_MESH_V1" && count > 0 &&
                        count <= 100000 && count % 3 == 0;
                asset.vertices.reserve(valid ? count : 0);
                std::uint32_t hash = 2166136261u;
                for (size_t i = 0; valid && i < count; ++i)
                {
                    MeshCache::Vertex vertex = {};
                    valid =
                        static_cast<bool>(in >> vertex.x >> vertex.y >> vertex.r >> vertex.g >> vertex.b >>
                                          vertex.a >> vertex.u >> vertex.v >> vertex.emission);
                    for (float value : {vertex.x, vertex.y, vertex.r, vertex.g, vertex.b, vertex.a, vertex.u,
                                        vertex.v, vertex.emission})
                    {
                        valid &= std::isfinite(value) && std::fabs(value) <= 10000;
                        std::uint32_t bits = 0;
                        std::memcpy(&bits, &value, sizeof(bits));
                        hash = (hash ^ bits) * 16777619u;
                    }
                    valid &= vertex.a >= 0 && vertex.a <= 1 && vertex.emission >= 0 && vertex.emission <= 16;
                    if (valid)
                    {
                        asset.vertices.push_back(vertex);
                    }
                }
                in >> std::ws;
                valid &= in.eof();
                if (valid)
                {
                    asset.revision = hash ? hash : 1;
                }
            }
            if (!valid)
            {
                asset.vertices.clear();
            }
            RenderProfiler::Event(valid ? "blender_asset_loaded" : "blender_asset_fallback",
                                  "asset=" + name + " vertices=" + std::to_string(asset.vertices.size()) +
                                      " revision=" + std::to_string(asset.revision));
            return assets.emplace(name, std::move(asset)).first->second;
        }
    } // namespace

    bool Append(const std::string& name, std::vector<MeshCache::Vertex>& vertices)
    {
        const auto& asset = Load(name);
        vertices.insert(vertices.end(), asset.vertices.begin(), asset.vertices.end());
        return !asset.vertices.empty();
    }

    std::uint32_t Revision(const std::string& name)
    {
        return Load(name).revision;
    }

    void Clear()
    {
        assets.clear();
    }
} // namespace BlenderAssets
