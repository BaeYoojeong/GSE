#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "BlenderBridge.h"
#include "RenderProfiler.h"
#include "ThirdParty/json.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace BlenderBridge
{
    namespace
    {
        namespace fs = std::filesystem;
        fs::path root, manifest, blend, executable, cache, script;
        fs::file_time_type observed = {}, completed = {};
        bool automatic = false, requested = false;
        HANDLE child = nullptr;
        std::chrono::steady_clock::time_point started;

        std::uint64_t Hash(const std::string& value)
        {
            std::uint64_t result = 14695981039346656037ull;
            for (unsigned char c : value)
            {
                result = (result ^ c) * 1099511628211ull;
            }
            return result;
        }

        fs::path Cache(const fs::path& source)
        {
            std::ostringstream id;
            id << std::hex << Hash(source.u8string());
            return RenderProfiler::DataDirectory() / L"RuntimeAssets" / id.str();
        }

        fs::file_time_type Stamp(const fs::path& path)
        {
            std::error_code error;
            auto time = fs::last_write_time(path, error);
            return error ? fs::file_time_type{} : time;
        }

        std::wstring Quote(const fs::path& path)
        {
            // Windows file names cannot contain quotes. All arguments below are paths, without a trailing
            // separator.
            return L"\"" + path.wstring() + L"\"";
        }

        void Launch()
        {
            requested = false;
            if (!fs::is_regular_file(executable) || !fs::is_regular_file(script) ||
                !fs::is_regular_file(blend))
            {
                RenderProfiler::Event("blender_export_unavailable",
                                      "check_blender_executable_source_blend_and_tools");
                return;
            }
            std::error_code error;
            fs::create_directories(cache, error);
            if (error)
            {
                RenderProfiler::Event("blender_export_failed", "cache_directory");
                return;
            }
            std::wstring command = Quote(executable) + L" --background --disable-autoexec " + Quote(blend) +
                                   L" --threads 2 --python-exit-code 1 --python " + Quote(script) +
                                   L" -- --manifest " + Quote(manifest) + L" --output " + Quote(cache);
            STARTUPINFOW startup = {};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process = {};
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, root.c_str(),
                                &startup, &process))
            {
                RenderProfiler::Event("blender_export_failed",
                                      "CreateProcess error=" + std::to_string(GetLastError()));
                return;
            }
            child = process.hProcess;
            CloseHandle(process.hThread);
            started = std::chrono::steady_clock::now();
            observed = Stamp(blend);
            RenderProfiler::Event("blender_export_started",
                                  "source=" + blend.u8string() + " cache=" + cache.u8string());
        }
    } // namespace

    void Configure(const fs::path& source, const fs::path& catalog, const fs::path& file, const fs::path& app,
                   bool autoExport)
    {
        fs::path selectedApp = app;
        if (selectedApp.empty())
        {
            for (const auto* candidate : {L"C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe",
                                          L"C:\\Program Files\\Blender Foundation\\Blender 4.5\\blender.exe"})
            {
                if (fs::is_regular_file(candidate))
                {
                    selectedApp = candidate;
                    break;
                }
            }
        }
        const bool changed =
            root != source || manifest != catalog || blend != file || executable != selectedApp;
        if (changed)
        {
            Shutdown();
            completed = {};
        }
        root = source;
        manifest = catalog;
        blend = file;
        executable = selectedApp;
        automatic = autoExport;
        cache = Cache(root);
        wchar_t module[32768] = {};
        GetModuleFileNameW(nullptr, module, 32768);
        script = fs::path(module).parent_path() / L"Tools" / L"export_asset_snapshot.py";
        if (changed || completed == fs::file_time_type{})
        {
            std::ifstream in(cache / L"current.json");
            try
            {
                if (in)
                {
                    auto doc = nlohmann::json::parse(in);
                    completed = fs::file_time_type(fs::file_time_type::duration(
                        doc.at("source_time").get<fs::file_time_type::duration::rep>()));
                }
            }
            catch (const std::exception&)
            {
                completed = {};
            }
        }
        // Avoid export storms: only launch after the source has stayed unchanged through two polls.
        observed = Stamp(blend);
    }

    void Request()
    {
        requested = true;
    }

    bool Poll()
    {
        bool refresh = false;
        if (child)
        {
            DWORD code = STILL_ACTIVE;
            GetExitCodeProcess(child, &code);
            if (code == STILL_ACTIVE && std::chrono::steady_clock::now() - started > std::chrono::minutes(3))
            {
                TerminateProcess(child, 2);
                WaitForSingleObject(child, 1000);
                code = 2;
            }
            if (code != STILL_ACTIVE)
            {
                CloseHandle(child);
                child = nullptr;
                if (code == 0 && Stamp(blend) == observed)
                {
                    completed = observed;
                    refresh = true;
                    // Use the native file clock representation so a restarted game can avoid duplicate
                    // exports.
                    try
                    {
                        std::ifstream in(cache / L"current.json");
                        auto doc = nlohmann::json::parse(in);
                        in.close();
                        doc["source_time"] = completed.time_since_epoch().count();
                        auto temp = cache / L"current.native.tmp";
                        std::ofstream out(temp);
                        out << doc.dump(2);
                        out.close();
                        MoveFileExW(temp.c_str(), (cache / L"current.json").c_str(),
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                    }
                    catch (const std::exception&)
                    {
                    }
                }
                else
                {
                    completed = observed;
                }
                RenderProfiler::Event(code == 0 ? "blender_export_completed" : "blender_export_failed",
                                      "exit=" + std::to_string(code) +
                                          " report=" + (cache / L"export_report.json").u8string());
            }
        }
        const auto current = Stamp(blend);
        if (!child && (requested || (automatic && current != fs::file_time_type{} && current == observed &&
                                     current != completed)))
        {
            Launch();
        }
        if (!child)
        {
            observed = current;
        }
        return refresh;
    }

    fs::path Resolve(const fs::path& sourceRoot, const std::string& id, const fs::path& source)
    {
        try
        {
            const auto directory = Cache(sourceRoot);
            std::ifstream in(directory / L"current.json", std::ios::binary | std::ios::ate);
            if (!in || in.tellg() > 1024 * 1024)
            {
                return source;
            }
            in.seekg(0);
            const auto doc = nlohmann::json::parse(in);
            if (doc.value("schema", "") != "ashen_runtime_overlay_1" || !doc.at("assets").contains(id))
            {
                return source;
            }
            auto relative = fs::u8path(doc.at("assets").at(id).get<std::string>());
            auto path = (directory / relative).lexically_normal();
            auto within = path.lexically_relative(directory);
            if (relative.is_absolute() || within.empty() || *within.begin() == L".." ||
                path.extension() != L".glb")
            {
                return source;
            }
            if (fs::is_regular_file(path) && (!fs::is_regular_file(source) || Stamp(path) > Stamp(source)))
            {
                return path;
            }
        }
        catch (const std::exception&)
        {
        }
        return source;
    }

    void Shutdown()
    {
        if (child)
        {
            // Only this owned export child is stopped; interactive Blender instances are untouched.
            if (WaitForSingleObject(child, 0) == WAIT_TIMEOUT)
            {
                TerminateProcess(child, 3);
                WaitForSingleObject(child, 1000);
            }
            CloseHandle(child);
            child = nullptr;
        }
        requested = false;
    }
} // namespace BlenderBridge
