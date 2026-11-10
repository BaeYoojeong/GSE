#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "DiskMeshCache.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace
{
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    fs::path directory;
    bool enabled = false;
    constexpr std::uint64_t DiskBudget = 256ull * 1024 * 1024;
    constexpr std::uint64_t HeaderBytes = 64;
    constexpr std::uint32_t FormatVersion = 1;
    constexpr std::uint32_t Magic = 0x48534d41;

    struct File
    {
        fs::path path;
        std::uint64_t bytes;
        fs::file_time_type time;
    };

    std::vector<File> files;
    std::uint64_t diskBytes = 0;

    double Ms(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    std::uint64_t Checksum(const std::vector<MeshCache::Vertex>& vertices)
    {
        auto* bytes = reinterpret_cast<const unsigned char*>(vertices.data());
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t i = 0; i < vertices.size() * sizeof(MeshCache::Vertex); ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    template <class T> void Read(std::ifstream& stream, T& value)
    {
        stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    }

    template <class T> void Write(std::ofstream& stream, const T& value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    void Forget(const fs::path& path)
    {
        auto it = std::find_if(files.begin(), files.end(), [&](const File& f) { return f.path == path; });
        if (it != files.end())
        {
            diskBytes -= it->bytes;
            files.erase(it);
        }
    }

    bool MakeRoom(std::uint64_t bytes)
    {
        while (!files.empty() && (diskBytes + bytes > DiskBudget || files.size() >= 4096))
        {
            auto it = std::min_element(files.begin(), files.end(),
                                       [](const File& a, const File& b) { return a.time < b.time; });
            std::error_code error;
            fs::remove(it->path, error);
            if (error)
            {
                RenderProfiler::Event("disk_prune_failed", it->path.filename().u8string());
                return false;
            }
            RenderProfiler::Event("disk_prune", it->path.filename().u8string(), it->bytes);
            diskBytes -= it->bytes;
            files.erase(it);
        }
        return diskBytes + bytes <= DiskBudget;
    }
} // namespace

namespace DiskMeshCache
{
    void InitializeFiles();
    bool LoadFile(const MeshCache::Key&, std::vector<MeshCache::Vertex>&);
    void SaveFile(const MeshCache::Key&, const std::vector<MeshCache::Vertex>&);
} // namespace DiskMeshCache

std::string DiskMeshCache::KeyName(const MeshCache::Key& key)
{
    std::ostringstream name;
    name << std::hex << std::setfill('0');
    for (auto part : key)
    {
        name << std::setw(8) << part;
    }
    return name.str();
}

void DiskMeshCache::InitializeFiles()
{
    static_assert(sizeof(MeshCache::Vertex) == 36, "Disk schema requires nine packed floats.");
    static_assert(sizeof(MeshCache::Key) == 40, "Disk schema requires ten packed 32-bit key fields.");
    files.clear();
    diskBytes = 0;
    enabled = false;
    if (RenderProfiler::Environment("ASHEN_MESH_DISK_CACHE") == "0")
    {
        RenderProfiler::Event("disk_cache", "disabled by environment");
        return;
    }
    auto base = RenderProfiler::DataDirectory();
    if (base.empty())
    {
        RenderProfiler::Event("disk_cache", "LOCALAPPDATA unavailable");
        return;
    }
    directory = base / L"MeshCache" / ("geometry-" + std::to_string(GeometryRevision));
    std::error_code error;
    fs::create_directories(directory, error);
    if (error)
    {
        RenderProfiler::Event("disk_cache", "directory unavailable");
        return;
    }
    enabled = true;
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
    {
        if (it->path().extension() != L".mesh")
        {
            continue;
        }
        auto size = it->file_size(error);
        if (error)
        {
            break;
        }
        auto time = it->last_write_time(error);
        if (error)
        {
            break;
        }
        files.push_back({it->path(), size, time});
        diskBytes += size;
    }
    if (error)
    {
        enabled = false;
        RenderProfiler::Event("disk_cache", "directory scan failed");
        return;
    }
    MakeRoom(0);
    RenderProfiler::Event("disk_cache",
                          directory.u8string() + "; format=1; geometry=" + std::to_string(GeometryRevision),
                          diskBytes);
}

bool DiskMeshCache::LoadFile(const MeshCache::Key& key, std::vector<MeshCache::Vertex>& vertices)
{
    vertices.clear();
    if (!enabled)
    {
        return false;
    }
    const auto start = Clock::now();
    const auto name = KeyName(key);
    const auto path = directory / (name + ".mesh");
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        RenderProfiler::CacheEvent("disk_miss", name, 0, Ms(start));
        return false;
    }
    std::uint32_t magic = 0, version = 0, revision = 0, count = 0;
    MeshCache::Key stored = {};
    std::uint64_t checksum = 0;
    Read(input, magic);
    Read(input, version);
    Read(input, revision);
    Read(input, count);
    Read(input, stored);
    Read(input, checksum);
    std::error_code error;
    const auto size = fs::file_size(path, error);
    bool valid = input && !error && magic == Magic && version == FormatVersion &&
                 revision == GeometryRevision && stored == key && count > 0 && count % 3 == 0 &&
                 count <= MeshCache::MaxBytes / 48 &&
                 size == HeaderBytes + static_cast<std::uint64_t>(count) * sizeof(MeshCache::Vertex);
    if (valid)
    {
        vertices.resize(count);
        input.read(reinterpret_cast<char*>(vertices.data()), count * sizeof(MeshCache::Vertex));
        valid = input && Checksum(vertices) == checksum;
        for (const auto& v : vertices)
        {
            for (float value : {v.x, v.y, v.r, v.g, v.b, v.a, v.u, v.v, v.emission})
            {
                valid = valid && std::isfinite(value);
            }
        }
    }
    if (!valid)
    {
        vertices.clear();
        RenderProfiler::CacheEvent("disk_invalid", name, 0, Ms(start));
        return false;
    }
    const auto time = fs::file_time_type::clock::now();
    fs::last_write_time(path, time, error);
    for (auto& file : files)
    {
        if (file.path == path)
        {
            file.time = time;
        }
    }
    RenderProfiler::CacheEvent("disk_hit", name, size, Ms(start));
    return true;
}

void DiskMeshCache::SaveFile(const MeshCache::Key& key, const std::vector<MeshCache::Vertex>& vertices)
{
    if (!enabled || vertices.empty())
    {
        return;
    }
    const auto start = Clock::now();
    const auto name = KeyName(key);
    const auto target = directory / (name + ".mesh");
    const auto temporary = directory / (name + "." + std::to_string(GetCurrentProcessId()) + ".tmp");
    const std::uint64_t bytes = HeaderBytes + vertices.size() * sizeof(MeshCache::Vertex);
    if (!MakeRoom(bytes))
    {
        RenderProfiler::CacheEvent("disk_full", name);
        return;
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    auto count = static_cast<std::uint32_t>(vertices.size());
    auto hash = Checksum(vertices);
    Write(output, Magic);
    Write(output, FormatVersion);
    Write(output, GeometryRevision);
    Write(output, count);
    Write(output, key);
    Write(output, hash);
    output.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(MeshCache::Vertex));
    output.flush();
    bool ok = static_cast<bool>(output);
    output.close();
    ok = ok && !output.fail();
    if (ok)
    {
        ok = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    }
    if (!ok)
    {
        std::error_code error;
        fs::remove(temporary, error);
        RenderProfiler::CacheEvent("disk_write_failed", name, bytes, Ms(start));
        return;
    }
    Forget(target);
    files.push_back({target, bytes, fs::file_time_type::clock::now()});
    diskBytes += bytes;
    RenderProfiler::CacheEvent("disk_write", name, bytes, Ms(start));
}

namespace
{
    using Vertices = std::vector<MeshCache::Vertex>;

    struct AsyncCache
    {
        struct Task
        {
            MeshCache::Key key;
            std::shared_ptr<const Vertices> data;
        };

        struct Ready
        {
            std::shared_ptr<const Vertices> data;
            std::uint64_t use = 0;
        };

        std::mutex mutex;
        std::condition_variable condition;
        std::deque<Task> tasks;
        std::set<MeshCache::Key> reads, writes;
        std::map<MeshCache::Key, Ready> ready;
        std::vector<RenderProfiler::Diagnostic> observations;
        std::size_t queuedBytes = 0, readyBytes = 0;
        std::uint64_t clock = 0;
        bool stopping = false;
        std::thread thread;

        void Remember(const MeshCache::Key& key, std::shared_ptr<const Vertices> data)
        {
            const auto bytes = data->size() * sizeof(MeshCache::Vertex);
            constexpr std::size_t Limit = 8 * 1024 * 1024;
            if (bytes > Limit)
            {
                return;
            }
            auto found = ready.find(key);
            if (found != ready.end())
            {
                found->second.use = ++clock;
                return;
            }
            while (readyBytes + bytes > Limit && !ready.empty())
            {
                auto victim = std::min_element(ready.begin(), ready.end(), [](const auto& a, const auto& b)
                                               { return a.second.use < b.second.use; });
                readyBytes -= victim->second.data->size() * sizeof(MeshCache::Vertex);
                ready.erase(victim);
            }
            ready.emplace(key, Ready{std::move(data), ++clock});
            readyBytes += bytes;
        }
    };

    std::unique_ptr<AsyncCache> asyncCache;
} // namespace

void DiskMeshCache::Initialize()
{
    Shutdown();
    if (RenderProfiler::Environment("ASHEN_MESH_DISK_CACHE") == "0")
    {
        return;
    }
    asyncCache = std::make_unique<AsyncCache>();
    auto* state = asyncCache.get();
    try
    {
        state->thread = std::thread(
            [state]
            {
                std::vector<RenderProfiler::Diagnostic> initial;
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                RenderProfiler::BeginCapture(initial);
                try
                {
                    InitializeFiles();
                    // Warm a bounded working set without delaying the render thread.
                    auto recent = files;
                    std::sort(recent.begin(), recent.end(),
                              [](const auto& a, const auto& b) { return a.time > b.time; });
                    std::uint64_t prefetched = 0;
                    for (const auto& file : recent)
                    {
                        if (prefetched + file.bytes > 8 * 1024 * 1024)
                        {
                            continue;
                        }
                        MeshCache::Key key = {};
                        std::ifstream header(file.path, std::ios::binary);
                        header.seekg(16);
                        Read(header, key);
                        if (!header || file.path.stem().u8string() != KeyName(key))
                        {
                            continue;
                        }
                        {
                            std::lock_guard<std::mutex> lock(state->mutex);
                            if (state->stopping)
                            {
                                break;
                            }
                            if (state->ready.count(key))
                            {
                                continue;
                            }
                        }
                        auto data = std::make_shared<Vertices>();
                        if (LoadFile(key, *data))
                        {
                            prefetched += file.bytes;
                            std::lock_guard<std::mutex> lock(state->mutex);
                            state->Remember(key, std::move(data));
                        }
                    }
                }
                catch (const std::exception& failure)
                {
                    enabled = false;
                    RenderProfiler::Event("disk_worker_failed", failure.what());
                }
                RenderProfiler::EndCapture();
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->observations = std::move(initial);
                }
                for (;;)
                {
                    AsyncCache::Task task;
                    {
                        std::unique_lock<std::mutex> lock(state->mutex);
                        state->condition.wait(lock, [&] { return state->stopping || !state->tasks.empty(); });
                        if (state->stopping && state->tasks.empty())
                        {
                            break;
                        }
                        task = std::move(state->tasks.front());
                        state->tasks.pop_front();
                        if (task.data)
                        {
                            state->queuedBytes -= task.data->size() * sizeof(MeshCache::Vertex);
                        }
                        else if (state->ready.count(task.key))
                        {
                            state->reads.erase(task.key);
                            continue;
                        }
                    }
                    std::vector<RenderProfiler::Diagnostic> diagnostics;
                    RenderProfiler::BeginCapture(diagnostics);
                    std::shared_ptr<Vertices> read;
                    try
                    {
                        if (task.data)
                        {
                            SaveFile(task.key, *task.data);
                        }
                        else
                        {
                            read = std::make_shared<Vertices>();
                            if (!LoadFile(task.key, *read))
                            {
                                read.reset();
                            }
                        }
                    }
                    catch (const std::exception& error)
                    {
                        RenderProfiler::Event("disk_worker_failed", error.what());
                    }
                    RenderProfiler::EndCapture();
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (task.data)
                        {
                            state->writes.erase(task.key);
                        }
                        else
                        {
                            state->reads.erase(task.key);
                        }
                        if (read)
                        {
                            state->Remember(task.key, std::move(read));
                        }
                        const auto room = 4096 - (std::min)(std::size_t{4096}, state->observations.size());
                        state->observations.insert(state->observations.end(), diagnostics.begin(),
                                                   diagnostics.begin() +
                                                       (std::min)(room, diagnostics.size()));
                    }
                }
            });
    }
    catch (const std::exception& failure)
    {
        asyncCache.reset();
        RenderProfiler::Event("disk_worker_unavailable", failure.what());
    }
}

void DiskMeshCache::Poll()
{
    if (!asyncCache)
    {
        return;
    }
    std::vector<RenderProfiler::Diagnostic> observations;
    {
        std::lock_guard<std::mutex> lock(asyncCache->mutex);
        observations.swap(asyncCache->observations);
    }
    for (const auto& item : observations)
    {
        // Worker duration is not main-thread disk time.
        RenderProfiler::Event("cache_io_worker", item.category + " " + item.detail, item.bytes,
                              item.milliseconds);
    }
}

bool DiskMeshCache::Load(const MeshCache::Key& key, Vertices& vertices)
{
    vertices.clear();
    if (!asyncCache)
    {
        return false;
    }
    std::shared_ptr<const Vertices> ready;
    bool request = false;
    {
        std::lock_guard<std::mutex> lock(asyncCache->mutex);
        const auto found = asyncCache->ready.find(key);
        if (found != asyncCache->ready.end())
        {
            ready = found->second.data;
            found->second.use = ++asyncCache->clock;
        }
        else if (asyncCache->tasks.size() < 64 && asyncCache->reads.insert(key).second)
        {
            asyncCache->tasks.push_back({key, {}});
            request = true;
        }
    }
    if (request)
    {
        asyncCache->condition.notify_one();
    }
    if (ready)
    {
        vertices = *ready;
        RenderProfiler::CacheEvent("disk_hit", KeyName(key));
        return true;
    }
    RenderProfiler::CacheEvent("disk_miss", KeyName(key));
    return false;
}

void DiskMeshCache::Save(const MeshCache::Key& key, const Vertices& vertices)
{
    if (!asyncCache || vertices.empty())
    {
        return;
    }
    const std::size_t bytes = vertices.size() * sizeof(MeshCache::Vertex);
    constexpr std::size_t Limit = 16 * 1024 * 1024;
    {
        std::lock_guard<std::mutex> lock(asyncCache->mutex);
        if (asyncCache->writes.count(key) || bytes + asyncCache->queuedBytes > Limit ||
            asyncCache->tasks.size() >= 64)
        {
            return;
        }
    }
    auto data = std::make_shared<Vertices>(vertices);
    {
        std::lock_guard<std::mutex> lock(asyncCache->mutex);
        asyncCache->Remember(key, data);
        asyncCache->queuedBytes += bytes;
        asyncCache->writes.insert(key);
        asyncCache->tasks.push_back({key, std::move(data)});
    }
    asyncCache->condition.notify_one();
}

void DiskMeshCache::Shutdown()
{
    if (!asyncCache)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(asyncCache->mutex);
        asyncCache->stopping = true;
    }
    asyncCache->condition.notify_all();
    if (asyncCache->thread.joinable())
    {
        asyncCache->thread.join();
    }
    Poll();
    asyncCache.reset();
}
