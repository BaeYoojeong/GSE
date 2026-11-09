#include "AssetContract.h"
#include "ThirdParty/json.hpp"
#include <fstream>
#include <set>
#include <cmath>
#include <stdexcept>

namespace AssetContract
{
    namespace
    {
        using Json = nlohmann::json;

        void Require(bool good, const char* message)
        {
            if (!good)
            {
                throw std::runtime_error(message);
            }
        }

        void Apply(Rules& rules, const Json& data)
        {
            Require(data.is_object(), "invalid_md_contract_object");
            const std::set<std::string> keys = {"schema",   "required_features", "bindings",
                                                "clips",    "durations",         "units_per_meter",
                                                "pose_fps", "root_bone",         "limits"};
            for (auto it = data.begin(); it != data.end(); ++it)
            {
                Require(keys.count(it.key()) != 0, "unknown_md_contract_key");
            }
            Require(data.value("schema", "") == "ashen_asset_contract_1", "unsupported_md_contract");
            const std::set<std::string> features = {"glb",     "skeletal",   "animations",
                                                    "modular", "flat_color", "emissive",
                                                    "linear",  "step",       "cubic"};
            for (const auto& item : data.value("required_features", Json::array()))
            {
                Require(features.count(item.get<std::string>()) != 0, "md_requires_unsupported_feature");
            }
            if (data.contains("bindings"))
            {
                for (auto it = data.at("bindings").begin(); it != data.at("bindings").end(); ++it)
                {
                    Require(rules.bindings.count(it.key()) != 0, "unknown_md_asset_role");
                    auto assets = it.value().is_string()
                                      ? std::vector<std::string>{it.value().get<std::string>()}
                                      : it.value().get<std::vector<std::string>>();
                    Require(!assets.empty() && assets.size() <= 1024, "invalid_md_binding");
                    rules.bindings[it.key()] = std::move(assets);
                }
            }
            if (data.contains("clips"))
            {
                for (auto it = data.at("clips").begin(); it != data.at("clips").end(); ++it)
                {
                    Require(rules.clips.count(it.key()) != 0, "unknown_motion_state");
                    const auto name = it.value().get<std::string>();
                    Require(!name.empty() && name.size() <= 128, "invalid_clip_name");
                    rules.clips[it.key()] = name;
                }
            }
            if (data.contains("durations"))
            {
                for (auto it = data.at("durations").begin(); it != data.at("durations").end(); ++it)
                {
                    const float value = it.value().get<float>();
                    Require(rules.clips.count(it.key()) && std::isfinite(value) && value >= .05f &&
                                value <= 60,
                            "invalid_md_duration");
                    rules.durations[it.key()] = value;
                }
            }
            rules.scale = data.value("units_per_meter", rules.scale);
            rules.poseFps = data.value("pose_fps", rules.poseFps);
            rules.rootBone = data.value("root_bone", rules.rootBone);
            if (data.contains("limits"))
            {
                const auto& limits = data.at("limits");
                rules.limits.nodes = limits.value("nodes", rules.limits.nodes);
                rules.limits.joints = limits.value("joints", rules.limits.joints);
                rules.limits.vertices = limits.value("vertices", rules.limits.vertices);
                rules.limits.animations = limits.value("animations", rules.limits.animations);
                rules.limits.keys = limits.value("keys", rules.limits.keys);
            }
            Require(std::isfinite(rules.scale) && rules.scale >= .01f && rules.scale <= 1000 &&
                        std::isfinite(rules.poseFps) && rules.poseFps >= 1 && rules.poseFps <= 30,
                    "invalid_md_projection");
            Require(!rules.rootBone.empty() && rules.rootBone.size() <= 128 && rules.limits.nodes >= 1 &&
                        rules.limits.nodes <= 16384 && rules.limits.joints >= 1 &&
                        rules.limits.joints <= 1024 && rules.limits.vertices >= 3 &&
                        rules.limits.vertices <= 1000000 && rules.limits.animations >= 1 &&
                        rules.limits.animations <= 512 && rules.limits.keys >= 1 &&
                        rules.limits.keys <= 4000000,
                    "invalid_md_limits");
        }
    } // namespace

    Rules Read(const std::vector<std::filesystem::path>& documents, const std::string& manifest, float scale,
               float poseFps)
    {
        Rules rules;
        rules.scale = scale;
        rules.poseFps = poseFps;
        const auto catalog = Json::parse(manifest);
        rules.bindings = {{"player", {"NPC_Modular_Library"}},
                          {"citizen", {"NPC_Modular_Library"}},
                          {"residence", {"ENV_Cottage"}},
                          {"house", {"ENV_Cottage"}},
                          {"chapel", {"ENV_Tavern"}},
                          {"tree", {"ENV_Tree_Oak", "ENV_Tree_Pine"}},
                          {"rubble", {"ENV_Rock_00", "ENV_Rock_01", "ENV_Rock_02"}},
                          {"herb", {"ENV_Grass"}}};
        for (const auto& animal : catalog.at("animals"))
        {
            rules.bindings["enemy"].push_back(animal.at("id").get<std::string>());
        }
        if (catalog.contains("clips"))
        {
            for (const auto& clip : rules.clips)
            {
                const auto found = catalog.at("clips").find(clip.second);
                if (found != catalog.at("clips").end())
                {
                    rules.durations[clip.first] =
                        found->value("duration_seconds", rules.durations[clip.first]);
                }
            }
        }
        for (const auto& path : documents)
        {
            if (path.extension() != L".md")
            {
                continue;
            }
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            Require(static_cast<bool>(in), "watched_md_unavailable");
            Require(in.tellg() >= 0 && in.tellg() <= 4 * 1024 * 1024, "md_size_limit");
            std::string text(static_cast<size_t>(in.tellg()), '\0');
            in.seekg(0);
            Require(static_cast<bool>(in.read(text.data(), static_cast<std::streamsize>(text.size()))),
                    "md_incomplete_read");
            size_t at = 0;
            while ((at = text.find("```ashen-assets", at)) != std::string::npos)
            {
                const auto start = text.find('\n', at),
                           end = start == std::string::npos ? std::string::npos : text.find("```", start + 1);
                Require(start != std::string::npos && end != std::string::npos, "md_unclosed_contract");
                Apply(rules, Json::parse(text.substr(start + 1, end - start - 1)));
                at = end + 3;
            }
        }
        for (const auto& duration : rules.durations)
        {
            Require(std::isfinite(duration.second) && duration.second >= .05f && duration.second <= 60,
                    "invalid_manifest_duration");
        }
        return rules;
    }

    std::string Select(const Rules& rules, const std::string& role, std::uint64_t identity)
    {
        const auto found = rules.bindings.find(role);
        if (found == rules.bindings.end())
        {
            return {};
        }
        std::string selected;
        std::uint64_t best = 0;
        for (const auto& id : found->second)
        {
            std::uint64_t hash = 14695981039346656037ull ^ identity;
            for (unsigned char c : id)
            {
                hash = (hash ^ c) * 1099511628211ull;
            }
            hash ^= hash >> 30;
            hash *= 0xbf58476d1ce4e5b9ull;
            hash ^= hash >> 27;
            hash *= 0x94d049bb133111ebull;
            hash ^= hash >> 31;
            if (selected.empty() || hash > best)
            {
                best = hash;
                selected = id;
            }
        }
        return selected;
    }
} // namespace AssetContract
