#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "AssetCatalog.h"
#include "AssetContract.h"
#include "BlenderBridge.h"
#include "RenderProfiler.h"
#include "ThirdParty/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace AssetCatalog
{
    namespace
    {
        namespace fs = std::filesystem;
        using Json = nlohmann::json;
        using Clock = std::chrono::steady_clock;

        struct Stamp
        {
            fs::file_time_type time = {};
            std::uintmax_t size = 0;
            bool exists = false;

            bool operator==(const Stamp& value) const
            {
                return time == value.time && size == value.size && exists == value.exists;
            }

            bool operator!=(const Stamp& value) const
            {
                return !(*this == value);
            }
        };

        struct Entry
        {
            fs::path path, sourcePath;
            std::shared_ptr<const GltfModel::Model> model;
            Stamp loaded, observed;
            unsigned stable = 0;
            std::uint64_t lastUse = 0;
            bool attempted = false;
        };

        struct Config
        {
            fs::path root, manifest;
            std::vector<fs::path> guides;
            float interval = 1, poseFps = 12, scale = 30;
            bool enabled = true;
            bool autoExport = true;
            fs::path blend, blender;
            AssetContract::Rules rules;
        };

        struct Catalog
        {
            std::map<std::string, Entry> entries;
            std::map<std::string, std::map<unsigned, std::string>> parts;
        };

        Config config;
        Catalog catalog;
        fs::path configPath;
        Stamp configStamp, manifestStamp;
        std::map<fs::path, Stamp> guideStamps;
        Clock::time_point nextPoll = {};
        std::uint64_t epoch = 1, counter = 0;
        bool initialized = false;
        bool catalogRetry = false;
        Clock::time_point retryCatalogAt = {};
        std::string lastError;
        std::set<std::string> warned;

        Stamp GetStamp(const fs::path& path)
        {
            std::error_code error;
            Stamp result;
            result.exists = fs::is_regular_file(path, error);
            if (result.exists)
            {
                result.time = fs::last_write_time(path, error);
                result.size = fs::file_size(path, error);
                result.exists = !error;
            }
            return result;
        }

        std::string Trim(std::string text)
        {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
            {
                return {};
            }
            const auto end = text.find_last_not_of(" \t\r\n");
            text = text.substr(begin, end - begin + 1);
            if (text.size() > 1 && text.front() == '"' && text.back() == '"')
            {
                text = text.substr(1, text.size() - 2);
            }
            return text;
        }

        float Number(const std::string& text)
        {
            size_t used = 0;
            const float value = std::stof(text, &used);
            if (used != text.size())
            {
                throw std::runtime_error("invalid_config_number");
            }
            return value;
        }

        bool ReadText(const fs::path& path, std::string& text, size_t limit)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in || in.tellg() < 0 || static_cast<std::uint64_t>(in.tellg()) > limit)
            {
                return false;
            }
            text.resize(static_cast<size_t>(in.tellg()));
            in.seekg(0);
            if (!text.empty() && !in.read(text.data(), static_cast<std::streamsize>(text.size())))
            {
                return false;
            }
            if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
            {
                text.erase(0, 3);
            }
            return true;
        }

        fs::path Resolve(const fs::path& base, const std::string& value)
        {
            auto path = fs::u8path(value);
            return fs::weakly_canonical(path.is_absolute() ? path : base / path);
        }

        fs::path Child(const fs::path& root, const std::string& relative)
        {
            auto child = fs::u8path(relative);
            if (child.is_absolute())
            {
                throw std::runtime_error("catalog_path_must_be_relative");
            }
            const auto path = Resolve(root, relative);
            const auto within = path.lexically_relative(root);
            if (within.empty() || *within.begin() == L"..")
            {
                throw std::runtime_error("catalog_path_outside_root");
            }
            return path;
        }

        void Failure(const std::string& code)
        {
            lastError = code;
            if (warned.insert(code).second)
            {
                RenderProfiler::Event("asset_reload_rejected", code + "; retaining_last_good_data=1");
            }
        }

        Config ReadConfig()
        {
            std::string text;
            if (!ReadText(configPath, text, 65536))
            {
                throw std::runtime_error("model_txt_unavailable");
            }
            Config result;
            std::string root, manifest = "asset_manifest.json", blend = "BriarGlen_AssetLibrary.blend",
                              blender;

            std::vector<std::string> guides;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
            {
                line = Trim(line);
                if (line.empty() || line[0] == '#' || line[0] == ';')
                {
                    continue;
                }
                const auto separator = line.find('=');
                if (separator == std::string::npos)
                {
                    if (root.empty())
                    {
                        root = line;
                    }
                    else
                    {
                        throw std::runtime_error("model_txt_expected_key_value");
                    }
                    continue;
                }
                const auto key = Trim(line.substr(0, separator)), value = Trim(line.substr(separator + 1));
                if (key == "root")
                {
                    root = value;
                }
                else if (key == "manifest")
                {
                    manifest = value;
                }
                else if (key == "watch_md" || key == "rig_schema")
                {
                    guides.push_back(value);
                }
                else if (key == "poll_seconds")
                {
                    result.interval = Number(value);
                }
                else if (key == "pose_fps")
                {
                    result.poseFps = Number(value);
                }
                else if (key == "units_per_meter")
                {
                    result.scale = Number(value);
                }
                else if (key == "source_blend")
                {
                    blend = value;
                }
                else if (key == "blender_executable")
                {
                    blender = value;
                }
                else if (key == "auto_export")
                {
                    if (value != "true" && value != "false")
                    {
                        throw std::runtime_error("auto_export_must_be_boolean");
                    }
                    result.autoExport = value == "true";
                }
                else if (key == "enabled")
                {
                    if (value != "true" && value != "false")
                    {
                        throw std::runtime_error("enabled_must_be_true_or_false");
                    }
                    result.enabled = value == "true";
                }
                else
                {
                    throw std::runtime_error("unknown_model_txt_key:" + key);
                }
            }
            if (root.empty() || !std::isfinite(result.interval) || result.interval < .5f ||
                result.interval > 60 || !std::isfinite(result.poseFps) || result.poseFps < 1 ||
                result.poseFps > 30 || !std::isfinite(result.scale) || result.scale < .01f ||
                result.scale > 1000)
            {
                throw std::runtime_error("invalid_model_txt_values");
            }
            result.root = Resolve(configPath.parent_path(), root);
            result.manifest = Child(result.root, manifest);
            result.blend = Child(result.root, blend);
            if (!blender.empty())
            {
                result.blender = Resolve(configPath.parent_path(), blender);
            }
            for (const auto& guide : guides)
            {
                result.guides.push_back(guide.compare(0, 8, "@config/") == 0
                                            ? Resolve(configPath.parent_path(), guide.substr(8))
                                            : Resolve(result.root, guide));
            }
            return result;
        }

        Catalog ReadCatalog(Config& source)
        {
            std::string text;
            if (!ReadText(source.manifest, text, 4 * 1024 * 1024))
            {
                throw std::runtime_error("manifest_unavailable");
            }
            const auto json = Json::parse(text);
            if (json.value("schema", "") != "village_assets_1.0" || json.value("units", "") != "meters")
            {
                throw std::runtime_error("unsupported_manifest_schema");
            }
            source.rules = AssetContract::Read(source.guides, text, source.scale, source.poseFps);
            source.scale = source.rules.scale;
            source.poseFps = source.rules.poseFps;
            Catalog result;
            auto add = [&](const std::string& id, const std::string& file)
            {
                if (id.empty() || id.size() > 128 || result.entries.size() >= 1024)
                {
                    throw std::runtime_error("catalog_id_limit");
                }
                Entry entry;
                entry.sourcePath = Child(source.root, file);
                entry.path = BlenderBridge::Resolve(source.root, id, entry.sourcePath);
                if (entry.path.extension() != L".glb" || !result.entries.emplace(id, std::move(entry)).second)
                {
                    throw std::runtime_error("duplicate_or_non_glb_asset");
                }
            };
            for (const char* group : {"npc_presets", "animals", "environment"})
            {
                const auto found = json.find(group);
                if (found == json.end() || !found->is_array())
                {
                    throw std::runtime_error("missing_catalog_group");
                }
                for (const auto& item : *found)
                {
                    const auto id = item.at("id").get<std::string>();
                    add(id, item.value("file", "GLB/" + id + ".glb"));
                }
            }
            add("NPC_Modular_Library",
                json.value("npc_library_file", std::string("GLB/NPC_Modular_Library.glb")));
            const auto& parts = json.at("npc_parts");
            if (!parts.is_array() || parts.empty() || parts.size() > 1024)
            {
                throw std::runtime_error("invalid_part_array");
            }
            for (const auto& part : parts)
            {
                if (!part.at("index").is_number_unsigned())
                {
                    throw std::runtime_error("invalid_part_index");
                }
                const auto slot = part.at("slot").get<std::string>();
                const auto index = part.at("index").get<unsigned>();
                const auto mesh = part.at("mesh").get<std::string>();
                if (slot.empty() || slot.size() > 64 || index > 65535 || mesh.empty() || mesh.size() > 256 ||
                    !result.parts[slot].emplace(index, mesh).second)
                {
                    throw std::runtime_error("invalid_part_catalog");
                }
            }
            for (const char* required : {"Face", "Hair", "Torso", "Outfit"})
            {
                if (!result.parts.count(required))
                {
                    throw std::runtime_error("required_npc_slot_missing");
                }
            }
            for (const auto& role : source.rules.bindings)
            {
                for (const auto& id : role.second)
                {
                    if (!result.entries.count(id))
                    {
                        throw std::runtime_error("md_binding_unknown_asset:" + id);
                    }
                }
            }
            return result;
        }

        void RefreshCatalog(bool forced)
        {
            try
            {
                Config candidate = ReadConfig();
                if (!candidate.enabled)
                {
                    BlenderBridge::Shutdown();
                    config = std::move(candidate);
                    catalogRetry = false;
                    ++epoch;
                    return;
                }
                Catalog loaded = ReadCatalog(candidate);
                // Reuse unchanged model records across metadata additions; replace catalog atomically.
                {
                    for (auto& pair : loaded.entries)
                    {
                        const auto old = catalog.entries.find(pair.first);
                        if (old != catalog.entries.end())
                        {
                            const auto replacement = pair.second.path;
                            const auto originalSource = pair.second.sourcePath;
                            pair.second = old->second;
                            pair.second.sourcePath = originalSource;
                            if (replacement != pair.second.path)
                            {
                                pair.second.path = replacement;
                                pair.second.loaded = {};
                                pair.second.observed = {};
                                pair.second.stable = 0;
                                pair.second.attempted = false;
                            }
                            if (forced || std::memcmp(&config.rules.limits, &candidate.rules.limits,
                                                      sizeof(GltfModel::Limits)) != 0)
                            {
                                pair.second.attempted = false;
                            }
                        }
                    }
                }
                config = std::move(candidate);
                catalog = std::move(loaded);
                BlenderBridge::Configure(config.root, config.manifest, config.blend, config.blender,
                                         config.autoExport);
                guideStamps.clear();
                for (const auto& path : config.guides)
                {
                    guideStamps[path] = GetStamp(path);
                }
                lastError.clear();
                warned.clear();
                catalogRetry = false;
                ++epoch;
                RenderProfiler::Event("asset_catalog_loaded",
                                      "assets=" + std::to_string(catalog.entries.size()) + " generation=" +
                                          std::to_string(epoch) + " root=" + config.root.u8string());
            }
            catch (const std::exception& error)
            {
                Failure(error.what());
                catalogRetry = true;
                retryCatalogAt = Clock::now() + std::chrono::seconds(5);
            }
        }

        void LoadEntry(const std::string& id, Entry& entry)
        {
            const auto started = Clock::now();
            const auto before = GetStamp(entry.path);
            entry.attempted = true;
            entry.observed = before;
            entry.stable = 0;
            std::string error;
            auto model = GltfModel::Load(entry.path, error, config.rules.limits);
            const auto after = GetStamp(entry.path);
            if (!model || before != after)
            {
                Failure(id + ":" + (before != after ? "file_changed_during_read" : error));
                return;
            }
            if (id == "NPC_Modular_Library")
            {
                for (const auto& slot : catalog.parts)
                {
                    for (const auto& part : slot.second)
                    {
                        if (!std::any_of(model->primitives.begin(), model->primitives.end(),
                                         [&](const GltfModel::Primitive& primitive)
                                         { return primitive.meshName == part.second; }))
                        {
                            Failure(id + ":required_part_node_missing:" + part.second);
                            return;
                        }
                    }
                }
            }
            entry.model = std::move(model);
            entry.loaded = after;
            entry.lastUse = ++counter;
            ++epoch;
            RenderProfiler::Event("gltf_asset_loaded",
                                  "id=" + id + " nodes=" + std::to_string(entry.model->nodes.size()) +
                                      " primitives=" + std::to_string(entry.model->primitives.size()) +
                                      " skins=" + std::to_string(entry.model->skins.size()) +
                                      " clips=" + std::to_string(entry.model->clips.size()) +
                                      " revision=" + std::to_string(entry.model->revision),
                                  before.size,
                                  std::chrono::duration<double, std::milli>(Clock::now() - started).count());
        }

        void TrimModels()
        {
            constexpr size_t Budget = 128 * 1024 * 1024;
            size_t used = 0;
            for (const auto& pair : catalog.entries)
            {
                if (pair.second.model)
                {
                    used += pair.second.model->bytes;
                }
            }
            while (used > Budget)
            {
                auto victim = catalog.entries.end();
                for (auto it = catalog.entries.begin(); it != catalog.entries.end(); ++it)
                {
                    if (it->second.model && it->second.model.use_count() == 1 &&
                        (victim == catalog.entries.end() || it->second.lastUse < victim->second.lastUse))
                    {
                        victim = it;
                    }
                }
                if (victim == catalog.entries.end())
                {
                    break;
                }
                used -= victim->second.model->bytes;
                victim->second.model.reset();
                victim->second.attempted = false;
                RenderProfiler::Event("gltf_model_evicted", victim->first);
            }
        }

        std::uint32_t Hash(const std::string& text)
        {
            std::uint32_t result = 2166136261u;
            for (unsigned char value : text)
            {
                result = (result ^ value) * 16777619u;
            }
            return result;
        }

        std::uint32_t Bits(float value)
        {
            std::uint32_t result;
            std::memcpy(&result, &value, 4);
            return result;
        }
    } // namespace

    void Initialize()
    {
        if (initialized)
        {
            return;
        }
        initialized = true;
        wchar_t configured[32768] = {}, executable[32768] = {};
        const auto explicitLength = GetEnvironmentVariableW(L"ASHEN_MODEL_CONFIG", configured, 32768);
        const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (explicitLength > 0 && explicitLength < 32768)
        {
            configPath = configured;
        }
        else if (length > 0 && length < 32768)
        {
            const fs::path directory = fs::path(executable).parent_path();
            configPath = directory / L"Assets" / L"모델.txt";
            fs::path parent = directory;
            for (unsigned depth = 0; depth < 4; ++depth)
            {
                const auto source = parent / L"SimpleGame" / L"Assets" / L"모델.txt";
                if (GetStamp(source).exists)
                {
                    configPath = source;
                    break;
                }
                if (parent == parent.parent_path())
                {
                    break;
                }
                parent = parent.parent_path();
            }
        }
        if (configPath.empty())
        {
            Failure("config_path_unavailable");
            return;
        }
        RenderProfiler::Event("asset_config_path", configPath.u8string());
        RefreshCatalog(false);
        configStamp = GetStamp(configPath);
        manifestStamp = GetStamp(config.manifest);
        nextPoll = Clock::now();
    }

    void Poll()
    {
        Initialize();
        const auto now = Clock::now();
        if (now < nextPoll)
        {
            return;
        }
        nextPoll = now + std::chrono::milliseconds(static_cast<int>(config.interval * 1000));
        const auto configuration = GetStamp(configPath), manifest = GetStamp(config.manifest);
        if (configuration != configStamp || manifest != manifestStamp ||
            (catalogRetry && now >= retryCatalogAt))
        {
            configStamp = configuration;
            RefreshCatalog(false);
            manifestStamp = GetStamp(config.manifest);
        }
        bool guideChanged = false;
        for (auto& guide : guideStamps)
        {
            const auto stamp = GetStamp(guide.first);
            if (stamp != guide.second)
            {
                guide.second = stamp;
                guideChanged = true;
                RenderProfiler::Event("asset_spec_changed",
                                      guide.first.u8string() + "; reapplying_declarative_contract=1");
            }
        }
        const bool exported = config.enabled && BlenderBridge::Poll();
        bool overlayChanged = false;
        for (const auto& pair : catalog.entries)
        {
            if (BlenderBridge::Resolve(config.root, pair.first, pair.second.sourcePath) != pair.second.path)
            {
                overlayChanged = true;
                break;
            }
        }
        if (guideChanged || exported || overlayChanged)
        {
            RefreshCatalog(false);
        }
        unsigned reloaded = 0;
        for (auto& pair : catalog.entries)
        {
            auto& entry = pair.second;
            if (!entry.attempted)
            {
                continue;
            }
            const auto stamp = GetStamp(entry.path);
            if (stamp != entry.observed)
            {
                entry.observed = stamp;
                entry.stable = 1;
            }
            else if (stamp != entry.loaded && entry.stable > 0 && ++entry.stable >= 2 && reloaded < 2)
            {
                LoadEntry(pair.first, entry);
                ++reloaded;
            }
        }
        TrimModels();
    }

    void Reload()
    {
        Initialize();
        RefreshCatalog(true);
        configStamp = GetStamp(configPath);
        manifestStamp = GetStamp(config.manifest);
    }

    void Shutdown()
    {
        BlenderBridge::Shutdown();
        catalog = {};
        config = {};
        guideStamps.clear();
        warned.clear();
        lastError.clear();
        configPath.clear();
        initialized = false;
        catalogRetry = false;
        configStamp = {};
        manifestStamp = {};
        ++epoch;
    }

    Prepared Prepare(const Request& request)
    {
        Initialize();
        Prepared result;
        if (!config.enabled || !std::isfinite(request.seconds) || !std::isfinite(request.yawRadians) ||
            !std::isfinite(request.maximumFootprint) || !std::isfinite(request.previousSeconds) ||
            !std::isfinite(request.blend))
        {
            return result;
        }
        const auto found = catalog.entries.find(request.asset);
        if (found == catalog.entries.end())
        {
            return result;
        }
        auto& entry = found->second;
        if (!entry.attempted)
        {
            LoadEntry(request.asset, entry);
        }
        if (!entry.model)
        {
            return result;
        }
        entry.lastUse = ++counter;
        result.model = entry.model;
        result.pose.clip = GltfModel::FindClip(*entry.model, request.clip);
        if (result.pose.clip < 0 && !entry.model->clips.empty())
        {
            const std::string warning = request.asset + ":clip_missing:" + request.clip;
            if (warned.insert(warning).second)
            {
                RenderProfiler::Event("asset_clip_fallback", warning);
            }
            result.pose.clip = GltfModel::FindClip(*entry.model, "Idle");
        }
        result.animated = result.pose.clip >= 0;
        const float duration = result.animated ? entry.model->clips[result.pose.clip].duration : 0;
        const float seconds = request.loop && duration > 0
                                  ? std::fmod((std::max)(0.0f, request.seconds), duration)
                                  : (std::clamp)(request.seconds, 0.0f, duration);
        const bool finished = !request.loop && result.animated && seconds >= duration;
        const auto frame = static_cast<std::uint32_t>(finished ? std::ceil(duration * config.poseFps)
                                                               : std::floor(seconds * config.poseFps));
        result.pose.seconds = finished ? duration : frame / config.poseFps;
        result.pose.loop = request.loop;
        result.pose.previousClip = GltfModel::FindClip(*entry.model, request.previousClip);
        result.pose.previousSeconds =
            std::floor((std::max)(0.f, request.previousSeconds) * config.poseFps) / config.poseFps;
        result.pose.previousLoop = request.previousLoop;
        result.pose.blend = std::round((std::clamp)(request.blend, 0.f, 1.f) * 16) / 16;
        if (result.pose.blend >= 1 || result.pose.previousClip < 0)
        {
            result.pose.previousClip = -1;
            result.pose.previousSeconds = 0;
            result.pose.previousLoop = true;
        }
        else
        {
            const float priorDuration = entry.model->clips[result.pose.previousClip].duration;
            if (priorDuration > 0)
            {
                result.pose.previousSeconds = result.pose.previousLoop
                                                  ? std::fmod(result.pose.previousSeconds, priorDuration)
                                                  : (std::min)(result.pose.previousSeconds, priorDuration);
            }
        }
        result.pose.suppressRootRotation = request.suppressRootRotation;
        result.pose.rootBone = config.rules.rootBone;
        result.projection.unitsPerMeter = config.scale;
        result.projection.yawRadians = request.yawRadians;
        result.projection.maximumFootprint = request.maximumFootprint;
        std::string selection;
        if (request.asset == "NPC_Modular_Library")
        {
            for (const auto& slot : catalog.parts)
            {
                unsigned index = slot.second.begin()->first;
                const auto requested = request.parts.find(slot.first);
                if (requested != request.parts.end())
                {
                    index = requested->second;
                }
                const auto part = slot.second.find(index);
                if (part == slot.second.end())
                {
                    Failure("part_index_missing:" + slot.first + ":" + std::to_string(index));
                    return {};
                }
                const bool exists =
                    std::any_of(entry.model->primitives.begin(), entry.model->primitives.end(),
                                [&](const GltfModel::Primitive& primitive)
                                { return primitive.meshName == part->second; });
                if (!exists)
                {
                    Failure("part_mesh_missing:" + part->second);
                    return {};
                }
                result.projection.meshSelection[part->second] = true;
                selection += slot.first + ":" + std::to_string(index) + ";";
            }
        }
        const auto revision = entry.model->revision;
        result.key = {0x474C5446,
                      static_cast<std::uint32_t>(revision),
                      static_cast<std::uint32_t>(revision >> 32),
                      Hash(request.asset),
                      Hash(selection),
                      static_cast<std::uint32_t>(result.pose.clip + 1),
                      frame,
                      Bits(request.yawRadians),
                      Bits(request.maximumFootprint),
                      Hash("projection_v3:" + std::to_string(config.scale) + ":" +
                           std::to_string(config.poseFps) + ":" + std::to_string(result.pose.previousClip) +
                           ":" + std::to_string(result.pose.previousSeconds) + ":" +
                           std::to_string(result.pose.blend) + ":" + std::to_string(request.loop) + ":" +
                           std::to_string(request.previousLoop) + ":" +
                           std::to_string(request.suppressRootRotation) + ":" + config.rules.rootBone)};
        return result;
    }

    std::map<std::string, unsigned> NpcParts(std::uint64_t identity, unsigned outfit)
    {
        Initialize();
        std::map<std::string, unsigned> selected;
        for (const auto& slot : catalog.parts)
        {
            const std::uint64_t variant = slot.first == "Outfit" ? outfit : identity ^ Hash(slot.first);
            auto part = slot.second.begin();
            std::advance(part, static_cast<std::ptrdiff_t>(variant % slot.second.size()));
            selected[slot.first] = part->first;
        }
        return selected;
    }

    std::string RoleAsset(const std::string& role, std::uint64_t identity)
    {
        Initialize();
        return AssetContract::Select(config.rules, role, identity);
    }

    std::string ClipName(const std::string& state)
    {
        Initialize();
        const auto found = config.rules.clips.find(state);
        return found == config.rules.clips.end() ? "Idle" : found->second;
    }

    float ClipDuration(const std::string& state)
    {
        Initialize();
        const auto found = config.rules.durations.find(state);
        return found == config.rules.durations.end() ? 1.f : found->second;
    }

    void ExportBlender()
    {
        Initialize();
        BlenderBridge::Request();
    }

    std::wstring Status()
    {
        return L"외부 자산 " + std::to_wstring(catalog.entries.size()) + L"종 · " +
               (!config.enabled     ? L"외부 자산 꺼짐"
                : lastError.empty() ? L"로더 정상"
                                    : L"일부 로드 실패 · 마지막 정상 모델 유지 / 로그 확인");
    }
} // namespace AssetCatalog
