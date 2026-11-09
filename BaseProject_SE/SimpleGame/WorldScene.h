#pragma once

// Ashen Inheritance: window/input adapter for the actor scene, game rules, and renderer.
namespace WorldScene
{
    bool Initialize();
    void Shutdown();
    void Render();
    void Update(float seconds);
    void Resize(int width, int height);
    void Arrow(int key, bool down);
    void Key(unsigned char key, bool down);
    void ClearInput();
} // namespace WorldScene
