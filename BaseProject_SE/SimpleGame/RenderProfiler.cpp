#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "RenderProfiler.h"
#include "MeshCache.h"
#include "AsyncLogWriter.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
    using Clock = std::chrono::steady_clock;
    const char* Names[] = {"ground", "shadow", "world", "post", "ui", "other"};
    constexpr std::size_t PassCount = static_cast<std::size_t>(RenderProfiler::Pass::Count);
    std::uint64_t frame = 0;
    bool recording = false, initialized = false;
    unsigned consoleEvery = 0;
    bool consoleTimed = true;
    Clock::time_point lastConsole;
    AsyncLogWriter writer;
    Clock::time_point frameStart, passStart, sessionStart, loopStart;
    bool loopActive = false;
    RenderProfiler::Pass pass = RenderProfiler::Pass::Other;

    struct Counters
    {
        std::uint64_t draws = 0, instanced = 0, instances = 0, useful = 0, submitted = 0, uploads = 0;
        std::uint64_t cpuHits = 0;
        std::uint64_t builds = 0, builtVertices = 0, hits = 0, misses = 0, diskHits = 0, diskMisses = 0;
        std::uint64_t diskWrites = 0, evictions = 0, failures = 0, visible = 0, culled = 0, textBuilds = 0;
        double buildMs = 0, diskMs = 0;
        std::array<std::uint64_t, PassCount> calls = {};
        std::array<double, PassCount> ms = {};
    } counters;

    std::string Quote(const std::string& value)
    {
        std::string result = "\"";
        for (char c : value)
        {
            if (c == '"')
            {
                result += '"';
            }
            result += c;
        }
        return result + '"';
    }

    struct Log
    {
        std::filesystem::path directory;
        std::string name, header;
        unsigned channel = 0;
        bool failed = false;

        void Write(const std::string& row)
        {
            if (!failed)
            {
                writer.Write(channel, row);
            }
        }
    } frames, events, batches;

    double Milliseconds(Clock::time_point from)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
    }

    void FinishPass()
    {
        if (recording)
        {
            counters.ms[static_cast<std::size_t>(pass)] += Milliseconds(passStart);
        }
        passStart = Clock::now();
    }

    void CountDraw(bool instanced)
    {
        if (!recording)
        {
            return;
        }
        ++counters.draws;
        counters.instanced += instanced ? 1 : 0;
        ++counters.calls[static_cast<std::size_t>(pass)];
    }
} // namespace

namespace
{
    thread_local std::vector<RenderProfiler::Diagnostic>* diagnosticCapture = nullptr;
}

void RenderProfiler::BeginCapture(std::vector<Diagnostic>& target)
{
    diagnosticCapture = &target;
}

void RenderProfiler::EndCapture()
{
    diagnosticCapture = nullptr;
}

std::filesystem::path RenderProfiler::DataDirectory()
{
    wchar_t overrideFolder[32768] = {};
    const auto custom = GetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", overrideFolder, 32768);
    if (custom && custom < 32768)
    {
        return std::filesystem::path(overrideFolder);
    }
    wchar_t local[32768] = {};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (!length || length >= 32768)
    {
        return {};
    }
    return std::filesystem::path(local) / L"AshenInheritance";
}

std::string RenderProfiler::Environment(const char* name)
{
    char value[128] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length && length < sizeof(value) ? value : "";
}

RenderProfiler::FrameCounters RenderProfiler::CurrentFrame()
{
    return {counters.draws, counters.builds, counters.uploads, counters.cpuHits, counters.evictions};
}

void RenderProfiler::Initialize()
{
    if (initialized)
    {
        return;
    }
    initialized = true;
    sessionStart = Clock::now();
    consoleTimed = true;
    consoleEvery = 0;
    const auto value = Environment("ASHEN_PROFILE_CONSOLE_EVERY");
    if (!value.empty())
    {
        consoleTimed = false;
        char* end = nullptr;
        const auto interval = std::strtoul(value.c_str(), &end, 10);
        if (end && *end == '\0' && value.front() != '-' && interval <= 1000000)
        {
            consoleEvery = static_cast<unsigned>(interval);
        }
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::ostringstream session;
    session << now.wYear << '-' << now.wMonth << '-' << now.wDay << '_' << now.wHour << '-' << now.wMinute
            << '-' << now.wSecond << '-' << now.wMilliseconds << '_' << GetCurrentProcessId();
    auto base = DataDirectory();
    auto directory = base / L"Profiling" / session.str();
    std::error_code error;
    if (!base.empty())
    {
        std::filesystem::create_directories(directory, error);
    }
    for (Log* log : {&frames, &events, &batches})
    {
        log->directory = directory;
        log->failed = base.empty() || bool(error);
    }
    frames.channel = 0;
    events.channel = 1;
    batches.channel = 2;
    lastConsole = Clock::now();
    frames.name = "frames";
    frames.header = "frame,cpu_render_swap_ms,draw_calls,instanced_calls,instances,useful_vertices,submitted_"
                    "vertices,upload_bytes,mesh_builds,built_vertices,build_ms,memory_hits,memory_misses,"
                    "disk_hits,disk_misses,disk_writes,disk_ms,evictions,cache_failures,visible,culled,text_"
                    "builds,resident_meshes,resident_bytes";
    for (const char* name : Names)
    {
        frames.header += std::string(",") + name + "_calls," + name + "_cpu_ms";
    }
    frames.header += ",update_cpu_ms,total_tick_cpu_ms,cpu_cache_hits,cpu_cache_bytes,log_rows_dropped";
    events.name = "events";
    events.header = "session_ms,frame,pass,category,detail,bytes,cpu_ms";
    batches.name = "batches";
    batches.header = "frame,pass,flush_reason,instances,useful_vertices,submitted_vertices,padding_ratio";
    if (!frames.failed)
    {
        try
        {
            writer.Start(directory, {{{frames.name, frames.header},
                                      {events.name, events.header},
                                      {batches.name, batches.header}}});
        }
        catch (const std::exception&)
        {
            frames.failed = events.failed = batches.failed = true;
        }
    }
    if (frames.failed)
    {
        std::cerr << "[Profile] File logging unavailable; console counters remain active.\n";
    }
    else
    {
        std::cout << "[Profile] Logs: " << directory.u8string() << '\n';
    }
    Event("session", "schema=3; API=OpenGL3.3; full tick includes update; async file logging; CPU timings "
                     "include swap, not GPU time; build=" __DATE__ " " __TIME__);
    for (auto token : {GL_VENDOR, GL_RENDERER, GL_VERSION})
    {
        const auto* value = glGetString(token);
        if (value)
        {
            Event("gpu", reinterpret_cast<const char*>(value));
        }
    }
}

void RenderProfiler::BeginLoop()
{
    loopStart = Clock::now();
    loopActive = true;
    ++frame;
    counters = {};
    recording = true;
    pass = Pass::Other;
    passStart = frameStart = loopStart;
}

void RenderProfiler::CancelLoop()
{
    loopActive = false;
    recording = false;
}

void RenderProfiler::BeginFrame()
{
    if (!loopActive)
    {
        ++frame;
        counters = {};
        loopStart = Clock::now();
    }
    recording = true;
    pass = Pass::Other;
    frameStart = passStart = Clock::now();
}

void RenderProfiler::SetPass(Pass value)
{
    FinishPass();
    pass = value;
}

void RenderProfiler::EndFrame()
{
    if (!recording)
    {
        return;
    }
    FinishPass();
    const double ms = Milliseconds(frameStart);
    const double total = Milliseconds(loopStart);
    const double update = std::chrono::duration<double, std::milli>(frameStart - loopStart).count();
    loopActive = false;
    recording = false;
    const auto cache = MeshCache::GetStatistics();
    std::ostringstream row;
    row << std::fixed << std::setprecision(3) << frame << ',' << ms << ',' << counters.draws << ','
        << counters.instanced << ',' << counters.instances << ',' << counters.useful << ','
        << counters.submitted << ',' << counters.uploads << ',' << counters.builds << ','
        << counters.builtVertices << ',' << counters.buildMs << ',' << counters.hits << ',' << counters.misses
        << ',' << counters.diskHits << ',' << counters.diskMisses << ',' << counters.diskWrites << ','
        << counters.diskMs << ',' << counters.evictions << ',' << counters.failures << ',' << counters.visible
        << ',' << counters.culled << ',' << counters.textBuilds << ',' << cache.meshes << ',' << cache.bytes;
    for (std::size_t i = 0; i < PassCount; ++i)
    {
        row << ',' << counters.calls[i] << ',' << counters.ms[i];
    }
    row << ',' << update << ',' << total << ',' << counters.cpuHits << ',' << cache.cpuBytes << ','
        << writer.Dropped();
    frames.Write(row.str());
    if (total > 16.67 && frame > 120)
    {
        Event("frame_spike", "update_ms=" + std::to_string(update) + " render_swap_ms=" + std::to_string(ms),
              0, total);
    }
    if ((consoleTimed && Clock::now() - lastConsole >= std::chrono::seconds(1)) ||
        (consoleEvery && frame % consoleEvery == 0))
    {
        lastConsole = Clock::now();
        std::cout << "[Render] frame=" << frame << " draw_calls=" << counters.draws
                  << " instanced=" << counters.instanced << " instances=" << counters.instances
                  << " mesh_builds=" << counters.builds << " disk_hits=" << counters.diskHits
                  << " upload_bytes=" << counters.uploads << '\n';
    }
}

void RenderProfiler::DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    glDrawArrays(mode, first, count);
    CountDraw(false);
}

void RenderProfiler::DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
    glDrawArraysInstanced(mode, first, count, instances);
    CountDraw(true);
}

void RenderProfiler::Event(const char* category, const std::string& detail, std::uint64_t bytes, double ms)
{
    if (diagnosticCapture)
    {
        if (diagnosticCapture->size() < 50000)
        {
            diagnosticCapture->push_back({category, detail, bytes, ms});
        }
        return;
    }
    if (!initialized)
    {
        return;
    }
    std::ostringstream row;
    row << std::fixed << std::setprecision(3) << Milliseconds(sessionStart) << ',' << frame << ','
        << Names[static_cast<std::size_t>(pass)] << ',' << Quote(category) << ',' << Quote(detail) << ','
        << bytes << ',' << ms;
    events.Write(row.str());
}

void RenderProfiler::CacheEvent(const char* action, const std::string& key, std::uint64_t bytes, double ms)
{
    if (diagnosticCapture)
    {
        Event(action, key, bytes, ms);
        return;
    }
    const std::string type = action;
    if (recording)
    {
        if (type == "memory_hit")
        {
            ++counters.hits;
        }
        else if (type == "cpu_hit")
        {
            ++counters.cpuHits;
        }
        else if (type == "memory_miss")
        {
            ++counters.misses;
        }
        else if (type == "disk_hit")
        {
            ++counters.diskHits;
        }
        else if (type == "disk_miss")
        {
            ++counters.diskMisses;
        }
        else if (type == "disk_write")
        {
            ++counters.diskWrites;
        }
        else if (type == "evict")
        {
            ++counters.evictions;
        }
        else
        {
            ++counters.failures;
        }
        if (type.find("disk_") == 0)
        {
            counters.diskMs += ms;
        }
    }
    // Aggregate frequent memory lookups instead of writing an event for every object.
    if (type != "memory_hit" && type != "memory_miss" && type != "cpu_hit")
    {
        Event(action, key, bytes, ms);
    }
}

void RenderProfiler::MeshBuilt(std::size_t count, double ms)
{
    if (recording)
    {
        ++counters.builds;
        counters.builtVertices += count;
        counters.buildMs += ms;
    }
}

void RenderProfiler::Uploaded(std::size_t bytes)
{
    if (recording)
    {
        counters.uploads += bytes;
    }
}

void RenderProfiler::Visibility(bool visible)
{
    if (visible)
    {
        ++counters.visible;
    }
    else
    {
        ++counters.culled;
    }
}

void RenderProfiler::TextBuilt()
{
    ++counters.textBuilds;
}

void RenderProfiler::Batch(const char* reason, std::size_t instances, std::uint64_t useful,
                           std::uint64_t submitted)
{
    if (recording)
    {
        counters.instances += instances;
        counters.useful += useful;
        counters.submitted += submitted;
    }
    std::ostringstream row;
    row << frame << ',' << Names[static_cast<std::size_t>(pass)] << ',' << reason << ',' << instances << ','
        << useful << ',' << submitted << ',' << (useful ? static_cast<double>(submitted) / useful : 0);
    batches.Write(row.str());
}

void RenderProfiler::Shutdown()
{
    if (!initialized)
    {
        return;
    }
    Event("session", "shutdown");
    writer.Stop();
    initialized = false;
}
