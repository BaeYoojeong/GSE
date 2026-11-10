#pragma once
#include <filesystem>
#include <functional>

namespace FrameworkBenchmark
{
    struct Options
    {
        bool enabled = false, live = false, smoke = false, stress = false, world = false;
        std::filesystem::path output;
    };

    // Opt-in only. Requires a new absolute output directory and never opens the player's profile.
    Options Parse(int argc, char** argv);
    int Run(const Options& options, const char* backend, const std::function<void()>& poll,
            const std::function<void()>& swap);
} // namespace FrameworkBenchmark
