#pragma once
#include "WindowInput.h"
#include <array>

// Ashen Inheritance: window/input adapter for the actor scene, game rules, and renderer.
namespace WorldScene
{
    bool Initialize(bool benchmark = false, bool stress = false);
    void Shutdown();
    void Render();
    void Update(float seconds);
    void Resize(int width, int height);
    void Arrow(SpecialKey key, bool down);
    void Key(unsigned char key, bool down);
    void Pointer(float x, float y);
    void PointerButton(float x, float y, bool down, bool valid = true);
    void PointerMove(float x, float y);
    void Scroll(float x, float y, double steps);
    void ClearInput();
    void BenchmarkStep(unsigned frame);

    struct InputState
    {
        std::array<bool, 4> arrows;
        bool attack, sprint, itemPanel;
    };

    InputState InspectInput();
    double SimulationTime();
} // namespace WorldScene
