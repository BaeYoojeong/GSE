#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include "FrameworkBenchmark.h"
#include "RenderProfiler.h"
#include "WorldScene.h"
#include "Dependencies/wglew.h"
#include "ThirdParty/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace FrameworkBenchmark
{
    Options Parse(int argc, char** argv)
    {
        Options options;
        if (argc < 2 ||
            (std::string(argv[1]) != "--benchmark" && std::string(argv[1]) != "--benchmark-live" &&
             std::string(argv[1]) != "--window-smoke"))
        {
            return options;
        }
        int count = 0;
        auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide || count != 3)
        {
            if (wide)
            {
                LocalFree(wide);
            }
            throw std::runtime_error("Usage: SimpleGame --benchmark[-live] <new-absolute-directory>");
        }
        options.output = wide[2];
        LocalFree(wide);
        if (!options.output.is_absolute() || std::filesystem::exists(options.output))
        {
            throw std::runtime_error("Benchmark directory must be absolute and must not exist.");
        }
        std::filesystem::create_directories(options.output);
        SetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", options.output.c_str());
        SetEnvironmentVariableW(L"ASHEN_PROFILE_CONSOLE_EVERY", L"0");
        SetEnvironmentVariableW(L"ASHEN_BENCHMARK", L"1");
        options.smoke = std::string(argv[1]) == "--window-smoke";
        options.enabled = true;
        options.live = std::string(argv[1]) == "--benchmark-live";
        return options;
    }

    int Run(const Options& options, const char* backend, const std::function<void()>& poll,
            const std::function<void()>& swap)
    {
        using Clock = std::chrono::steady_clock;
        using Json = nlohmann::json;
        constexpr unsigned Warmup = 120, Samples = 600, QueryCount = 64;

        struct Frame
        {
            double cpu = 0, gpu = -1;
            RenderProfiler::FrameCounters counts;
        };

        struct Query
        {
            GLuint id = 0;
            int sample = -1;
            bool busy = false;
        };

        std::array<Query, QueryCount> queries;
        std::vector<Frame> frames(Samples);
        if (WGLEW_EXT_swap_control)
        {
            wglSwapIntervalEXT(0);
        }
        while (glGetError() != GL_NO_ERROR)
        {
        }
        for (auto& query : queries)
        {
            glGenQueries(1, &query.id);
        }
        auto resolve = [&](Query& query, bool wait)
        {
            if (!query.busy)
            {
                return true;
            }
            GLint available = GL_FALSE;
            if (!wait)
            {
                glGetQueryObjectiv(query.id, GL_QUERY_RESULT_AVAILABLE, &available);
            }
            if (!wait && !available)
            {
                return false;
            }
            GLuint64 time = 0;
            glGetQueryObjectui64v(query.id, GL_QUERY_RESULT, &time);
            if (query.sample >= 0)
            {
                frames[query.sample].gpu = static_cast<double>(time) / 1000000.;
            }
            query.busy = false;
            return true;
        };
        unsigned glErrors = 0;
        for (unsigned frame = 0; frame < Warmup + Samples; ++frame)
        {
            auto& query = queries[frame % QueryCount];
            const bool timed = resolve(query, false);
            const auto start = Clock::now();
            poll();
            if (options.live)
            {
                WorldScene::BenchmarkStep(frame);
            }
            RenderProfiler::BeginFrame();
            if (timed)
            {
                glBeginQuery(GL_TIME_ELAPSED, query.id);
            }
            WorldScene::Render();
            if (timed)
            {
                glEndQuery(GL_TIME_ELAPSED);
                query.busy = true;
                query.sample = frame >= Warmup ? static_cast<int>(frame - Warmup) : -1;
            }
            swap();
            RenderProfiler::EndFrame();
            const double cpu = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (frame >= Warmup)
            {
                frames[frame - Warmup].cpu = cpu;
                frames[frame - Warmup].counts = RenderProfiler::CurrentFrame();
            }
            if (frame % 60 == 0 && glGetError() != GL_NO_ERROR)
            {
                ++glErrors;
            }
        }
        for (auto& query : queries)
        {
            resolve(query, true);
            glDeleteQueries(1, &query.id);
        }
        auto statistics = [](std::vector<double> values)
        {
            Json result;
            if (values.empty())
            {
                return result;
            }
            std::sort(values.begin(), values.end());
            double total = 0;
            for (double value : values)
            {
                total += value;
            }
            result = {{"count", values.size()},
                      {"mean", total / values.size()},
                      {"median", values[values.size() / 2]},
                      {"p95", values[static_cast<size_t>((values.size() - 1) * .95)]},
                      {"p99", values[static_cast<size_t>((values.size() - 1) * .99)]}};
            return result;
        };
        Json result = {{"schema", "ashen_framework_benchmark_1"},
                       {"backend", backend},
                       {"mode", options.live ? "scripted_simulation" : "frozen_scene"},
                       {"warmup_frames", Warmup},
                       {"measured_frames", Samples},
                       {"world_seed", 20261110},
                       {"visible", false},
                       {"vsync", WGLEW_EXT_swap_control ? wglGetSwapIntervalEXT() : -1},
                       {"gl_errors", glErrors},
                       {"query_readback", "asynchronous; final drain outside measurement"}};
        auto glString = [](GLenum name)
        {
            const auto* text = glGetString(name);
            return text ? reinterpret_cast<const char*>(text) : "unavailable";
        };
        result["gpu_vendor"] = glString(GL_VENDOR);
        result["gpu_renderer"] = glString(GL_RENDERER);
        result["opengl_version"] = glString(GL_VERSION);
        for (auto item : {std::pair<const char*, GLenum>{"red_bits", GL_RED_BITS},
                          {"green_bits", GL_GREEN_BITS},
                          {"blue_bits", GL_BLUE_BITS},
                          {"alpha_bits", GL_ALPHA_BITS},
                          {"depth_bits", GL_DEPTH_BITS},
                          {"stencil_bits", GL_STENCIL_BITS},
                          {"samples", GL_SAMPLES}})
        {
            GLint value = 0;
            glGetIntegerv(item.second, &value);
            result["framebuffer"][item.first] = value;
        }
        std::vector<double> cpu, gpu, draws, builds, uploads;
        std::ofstream csv(options.output / L"framework_frames.csv");
        csv << "frame,cpu_frame_ms,gpu_ms,draw_calls,mesh_builds,upload_bytes\n";
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto& frame = frames[i];
            cpu.push_back(frame.cpu);
            if (frame.gpu >= 0)
            {
                gpu.push_back(frame.gpu);
            }
            draws.push_back(static_cast<double>(frame.counts.drawCalls));
            builds.push_back(static_cast<double>(frame.counts.meshBuilds));
            uploads.push_back(static_cast<double>(frame.counts.uploadBytes));
            csv << i << ',' << frame.cpu << ',' << frame.gpu << ',' << frame.counts.drawCalls << ','
                << frame.counts.meshBuilds << ',' << frame.counts.uploadBytes << '\n';
        }
        result["cpu_frame_ms"] = statistics(cpu);
        result["gpu_ms"] = statistics(gpu);
        result["draw_calls"] = statistics(draws);
        result["mesh_builds"] = statistics(builds);
        result["upload_bytes"] = statistics(uploads);
        // A separate unmeasured frame verifies that the window migration preserves rendered pixels.
        RenderProfiler::BeginFrame();
        WorldScene::Render();
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int width = viewport[2] + 2 * viewport[0], height = viewport[3] + 2 * viewport[1];
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        RenderProfiler::EndFrame();
        std::uint64_t hash = 14695981039346656037ull;
        for (auto byte : pixels)
        {
            hash = (hash ^ byte) * 1099511628211ull;
        }
        result["image_hash_fnv1a64"] = std::to_string(hash);
        result["image_width"] = width;
        result["image_height"] = height;
        std::ofstream image(options.output / L"frame.ppm", std::ios::binary);
        image << "P6\n" << width << ' ' << height << "\n255\n";
        for (int row = height - 1; row >= 0; --row)
        {
            image.write(reinterpret_cast<const char*>(pixels.data() + static_cast<size_t>(row) * width * 3),
                        width * 3);
        }
        std::ofstream out(options.output / L"framework_benchmark.json");
        out << result.dump(2);
        std::cout << "[Benchmark] " << result.dump() << '\n';
        return glErrors ? 1 : 0;
    }
} // namespace FrameworkBenchmark
