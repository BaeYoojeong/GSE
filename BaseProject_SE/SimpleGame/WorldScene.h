#pragma once
#include "WindowInput.h"
#include <array>

// Ashen Inheritance: window/input adapter for the actor scene, game rules, and renderer.
namespace WorldScene
{
    bool Initialize(bool benchmark = false);
    void Shutdown();
    void Render();
    void Update(float seconds);
    void Resize(int width, int height);
    void Arrow(SpecialKey key, bool down);
    void Key(unsigned char key, bool down);
    void ClearInput();
    void BenchmarkStep(unsigned frame);

    struct InputState
    {
        std::array<bool, 4> arrows;
        bool attack, sprint, jump;
    };

    InputState InspectInput();
    double SimulationTime();
} // namespace WorldScene
