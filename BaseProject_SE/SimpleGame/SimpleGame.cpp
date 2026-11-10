/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)
This program is free software under the What The Hell License.
*/
#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include "Dependencies/glew.h"
#define GLFW_INCLUDE_NONE
#include "Dependencies/GLFW/upstream/include/GLFW/glfw3.h"
#include "WorldScene.h"
#include "RenderProfiler.h"
#include "AcceptanceChecks.h"
#include "FrameworkBenchmark.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <stdexcept>
#include <fstream>
#include "ThirdParty/json.hpp"
#include "PostProcessing.h"

namespace
{
    struct WindowState
    {
        bool focused = false, iconified = false, resized = true;
        int width = 1120, height = 784;
        unsigned resizeEvents = 0;
        unsigned long long renderedFrames = 0;
    };

    WindowState& State(GLFWwindow* window)
    {
        return *static_cast<WindowState*>(glfwGetWindowUserPointer(window));
    }

    void GlfwError(int code, const char* message)
    {
        std::cerr << "[GLFW] " << code << ": " << (message ? message : "unknown error") << '\n';
    }

    void FramebufferSize(GLFWwindow* window, int width, int height)
    {
        auto& state = State(window);
        state.width = width;
        state.height = height;
        state.resized = true;
        ++state.resizeEvents;
    }

    void Focus(GLFWwindow* window, int focused)
    {
        State(window).focused = focused == GLFW_TRUE;
        if (!focused)
        {
            WorldScene::ClearInput();
        }
    }

    void Iconify(GLFWwindow* window, int iconified)
    {
        State(window).iconified = iconified == GLFW_TRUE;
        if (iconified)
        {
            WorldScene::ClearInput();
        }
    }

    void Key(GLFWwindow* window, int key, int scancode, int action, int modifiers)
    {
        (void)scancode;
        if (action == GLFW_REPEAT || (action != GLFW_PRESS && action != GLFW_RELEASE))
        {
            return;
        }
        const bool down = action == GLFW_PRESS;
        if (down &&
            (!State(window).focused || (modifiers & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER))))
        {
            return;
        }
        using Special = WorldScene::SpecialKey;
        switch (key)
        {
        case GLFW_KEY_LEFT:
            WorldScene::Arrow(Special::Left, down);
            return;
        case GLFW_KEY_UP:
            WorldScene::Arrow(Special::Up, down);
            return;
        case GLFW_KEY_RIGHT:
            WorldScene::Arrow(Special::Right, down);
            return;
        case GLFW_KEY_DOWN:
            WorldScene::Arrow(Special::Down, down);
            return;
        case GLFW_KEY_ESCAPE:
            WorldScene::Key(27, down);
            return;
        case GLFW_KEY_TAB:
            WorldScene::Key('\t', down);
            return;
        case GLFW_KEY_LEFT_BRACKET:
            WorldScene::Key('[', down);
            return;
        case GLFW_KEY_RIGHT_BRACKET:
            WorldScene::Key(']', down);
            return;
        }
        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F10)
        {
            WorldScene::Arrow(static_cast<Special>(static_cast<int>(Special::F1) + key - GLFW_KEY_F1), down);
            return;
        }
        // Gameplay bindings use physical keys; character callbacks are reserved for future text input.
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        {
            WorldScene::Key(static_cast<unsigned char>('a' + key - GLFW_KEY_A), down);
        }
        else if ((key >= GLFW_KEY_0 && key <= GLFW_KEY_9) || key == GLFW_KEY_SPACE)
        {
            WorldScene::Key(static_cast<unsigned char>(key), down);
        }
    }

    void Render(GLFWwindow* window)
    {
        RenderProfiler::BeginFrame();
        WorldScene::Render();
        glfwSwapBuffers(window);
        RenderProfiler::EndFrame();
        ++State(window).renderedFrames;
    }

    bool TickWindow(GLFWwindow* window, double& lastUpdate)
    {
        auto& state = State(window);
        const bool background = !state.focused || state.iconified || state.width <= 0 || state.height <= 0;
        if (!background)
        {
            RenderProfiler::BeginLoop();
        }
        if (background)
        {
            glfwWaitEventsTimeout(.02);
        }
        else
        {
            glfwPollEvents();
        }
        if (glfwWindowShouldClose(window))
        {
            return false;
        }
        const double now = glfwGetTime();
        const bool drawable = state.focused && !state.iconified && state.width > 0 && state.height > 0;
        if (drawable && background)
        {
            RenderProfiler::BeginLoop();
        }
        if (!drawable)
        {
            RenderProfiler::CancelLoop();
        }
        if (drawable || now - lastUpdate >= .02)
        {
            WorldScene::Update(static_cast<float>((std::max)(0., now - lastUpdate)));
            lastUpdate = now;
        }
        if (!drawable)
        {
            return true;
        }
        if (state.resized)
        {
            WorldScene::Resize(state.width, state.height);
            state.resized = false;
        }
        Render(window);
        return true;
    }

    int Smoke(GLFWwindow* window, const std::filesystem::path& output)
    {
        using Json = nlohmann::json;
        Json report = {{"schema", "ashen_glfw_smoke_1"}, {"checks", Json::array()}, {"passed", true}};
        auto check = [&](bool passed, const char* name)
        {
            report["checks"].push_back({{"name", name}, {"passed", passed}});
            if (!passed)
            {
                report["passed"] = false;
            }
        };
        auto send = [&](int key, int action = GLFW_PRESS, int mods = 0)
        { Key(window, key, 0, action, mods); };
        Focus(window, GLFW_TRUE);
        const int arrows[] = {GLFW_KEY_LEFT, GLFW_KEY_UP, GLFW_KEY_RIGHT, GLFW_KEY_DOWN};
        for (unsigned i = 0; i < 4; ++i)
        {
            send(arrows[i]);
            const auto state = WorldScene::InspectInput();
            check(state.arrows[i] && std::count(state.arrows.begin(), state.arrows.end(), true) == 1,
                  "arrow_mapping");
            send(arrows[i], GLFW_RELEASE, GLFW_MOD_CONTROL);
            check(!WorldScene::InspectInput().arrows[i], "modified_release_clears_held_key");
        }
        send(GLFW_KEY_C, GLFW_PRESS, GLFW_MOD_SHIFT);
        check(WorldScene::InspectInput().sprint, "shift_run_pressed");
        send(GLFW_KEY_C, GLFW_RELEASE);
        check(!WorldScene::InspectInput().sprint, "run_released");
        send(GLFW_KEY_SPACE);
        check(WorldScene::InspectInput().attack, "attack_pressed");
        send(GLFW_KEY_SPACE, GLFW_RELEASE);
        check(!WorldScene::InspectInput().attack, "attack_released");
        send(GLFW_KEY_J);
        check(WorldScene::InspectInput().jump, "jump_requested");
        const bool bloom = PostProcessing::Options().bloom;
        send(GLFW_KEY_F2);
        check(PostProcessing::Options().bloom != bloom, "function_key_press");
        send(GLFW_KEY_F2, GLFW_REPEAT);
        check(PostProcessing::Options().bloom != bloom, "repeat_does_not_retoggle");
        send(GLFW_KEY_F2, GLFW_RELEASE);
        check(PostProcessing::Options().bloom != bloom, "function_key_release_does_not_retoggle");
        send(GLFW_KEY_F2);
        check(PostProcessing::Options().bloom == bloom, "function_key_restored");
        send(GLFW_KEY_LEFT);
        send(GLFW_KEY_C);
        Focus(window, GLFW_FALSE);
        const auto lost = WorldScene::InspectInput();
        check(std::none_of(lost.arrows.begin(), lost.arrows.end(), [](bool value) { return value; }) &&
                  !lost.attack && !lost.sprint && !lost.jump,
              "focus_callback_clears_all_inputs");
        send(GLFW_KEY_C);
        check(!WorldScene::InspectInput().sprint, "unfocused_press_ignored");
        Focus(window, GLFW_TRUE);
        send(GLFW_KEY_C, GLFW_PRESS, GLFW_MOD_CONTROL);
        check(!WorldScene::InspectInput().sprint, "os_shortcut_not_game_action");
        send(GLFW_KEY_TAB);
        send(GLFW_KEY_C);
        check(!WorldScene::InspectInput().sprint, "journal_blocks_game_controls");
        send(GLFW_KEY_TAB);
        send(GLFW_KEY_C);
        Iconify(window, GLFW_TRUE);
        check(!WorldScene::InspectInput().sprint && State(window).iconified, "iconify_callback_clears_input");
        Iconify(window, GLFW_FALSE);
        const auto prior = State(window).resizeEvents;
        glfwSetWindowSize(window, 1000, 700);
        glfwPollEvents();
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        check(width > 0 && height > 0 && State(window).width == width && State(window).height == height &&
                  State(window).resizeEvents > prior,
              "framebuffer_resize_callback");
        WorldScene::Resize(width, height);
        Render(window);
        check(glGetError() == GL_NO_ERROR, "resized_render_has_no_gl_error");
        Iconify(window, GLFW_TRUE);
        Focus(window, GLFW_FALSE);
        glfwPollEvents();
        double backgroundLast = glfwGetTime();
        const double simulationBefore = WorldScene::SimulationTime();
        const auto rendersBefore = State(window).renderedFrames;
        for (unsigned tick = 0; tick < 12; ++tick)
        {
            TickWindow(window, backgroundLast);
        }
        check(WorldScene::SimulationTime() > simulationBefore + .1 &&
                  State(window).renderedFrames == rendersBefore,
              "background_loop_advances_world_without_rendering");
        Iconify(window, GLFW_FALSE);
        Focus(window, GLFW_TRUE);
        TickWindow(window, backgroundLast);
        check(State(window).renderedFrames > rendersBefore, "foreground_loop_resumes_rendering");
        FramebufferSize(window, 0, 0);
        check(State(window).width == 0 && State(window).height == 0,
              "zero_framebuffer_recorded_for_render_skip");
        FramebufferSize(window, width, height);
        WorldScene::ClearInput();
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        check(glfwWindowShouldClose(window) == GLFW_TRUE, "close_request_observed");
        std::ofstream out(output / L"window_smoke.json");
        out << report.dump(2);
        std::cout << report.dump() << '\n';
        return report["passed"].get<bool>() ? 0 : 1;
    }

    struct Lifetime
    {
        GLFWwindow* window = nullptr;
        bool initialized = false, scene = false;

        ~Lifetime()
        {
            if (scene)
            {
                glfwMakeContextCurrent(window);
                WorldScene::Shutdown();
            }
            if (window)
            {
                glfwDestroyWindow(window);
            }
            if (initialized)
            {
                glfwTerminate();
            }
        }
    };

    int Run(int argc, char** argv)
    {
        if (argc == 3 && std::string(argv[1]) == "--self-test")
        {
            int count = 0;
            auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
            if (!wide || count != 3)
            {
                if (wide)
                {
                    LocalFree(wide);
                }
                return 2;
            }
            const std::filesystem::path directory(wide[2]);
            LocalFree(wide);
            return Ashen::AcceptanceChecks::Run(directory);
        }
        const auto benchmark = FrameworkBenchmark::Parse(argc, argv);
        WindowState state;
        Lifetime lifetime;
        glfwSetErrorCallback(GlfwError);
        if (!glfwInit())
        {
            return 1;
        }
        lifetime.initialized = true;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_RED_BITS, 8);
        glfwWindowHint(GLFW_GREEN_BITS, 8);
        glfwWindowHint(GLFW_BLUE_BITS, 8);
        glfwWindowHint(GLFW_ALPHA_BITS, 0);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);
        glfwWindowHint(GLFW_STENCIL_BITS, 8);
        glfwWindowHint(GLFW_SAMPLES, 0);
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, benchmark.enabled ? GLFW_FALSE : GLFW_TRUE);
        lifetime.window = glfwCreateWindow(1120, 784, "Ashen Inheritance - Living World", nullptr, nullptr);
        if (!lifetime.window)
        {
            return 1;
        }
        auto* window = lifetime.window;
        glfwSetWindowPos(window, 80, 40);
        glfwMakeContextCurrent(window);
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK || !GLEW_VERSION_3_3)
        {
            std::cerr << "OpenGL 3.3 is required.\n";
            return 1;
        }
        while (glGetError() != GL_NO_ERROR)
        {
        }
        // Preserve the former uncapped foreground loop. Opt in to display pacing with ASHEN_VSYNC=1.
        const int vsync = !benchmark.enabled && RenderProfiler::Environment("ASHEN_VSYNC") == "1" ? 1 : 0;
        glfwSwapInterval(vsync);
        glfwSetWindowUserPointer(window, &state);
        glfwSetFramebufferSizeCallback(window, FramebufferSize);
        glfwSetKeyCallback(window, Key);
        glfwSetWindowFocusCallback(window, Focus);
        glfwSetWindowIconifyCallback(window, Iconify);
        state.focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
        state.iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
        glfwGetFramebufferSize(window, &state.width, &state.height);
        lifetime.scene = true;
        if (!WorldScene::Initialize(benchmark.enabled, benchmark.stress))
        {
            std::cerr << "Could not initialize the world renderer.\n";
            return 1;
        }
        WorldScene::Resize(state.width, state.height);
        state.resized = false;
        RenderProfiler::Event("window_backend", std::string("GLFW ") + glfwGetVersionString() +
                                                    "; vsync=" + std::to_string(vsync));
        if (benchmark.enabled)
        {
            if (benchmark.smoke)
            {
                return Smoke(window, benchmark.output);
            }
            return FrameworkBenchmark::Run(
                benchmark, "GLFW", [] { glfwPollEvents(); }, [&] { glfwSwapBuffers(window); });
        }
        std::cout << "Ashen Inheritance | GLFW | Arrows: move | C: run | J: jump | Space: attack\n"
                     "E: interact | G: share | B/N: buy/sell | P: PK | V: residents | M: map | Q: quest\n"
                     "Tab: life journal | R: next life | F8: save | F9: reload | F10: Blender export\n";
        double lastUpdate = glfwGetTime();
        while (!glfwWindowShouldClose(window))
        {
            if (!TickWindow(window, lastUpdate))
            {
                break;
            }
        }
        return 0;
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        return Run(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Application error: " << error.what() << '\n';
        return 1;
    }
}
