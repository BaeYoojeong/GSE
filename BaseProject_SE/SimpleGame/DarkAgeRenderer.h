#pragma once

#include "DarkAgeGame.h"

namespace AshenRenderer
{
    struct Color
    {
        float r, g, b, a = 1;
    };

    bool Initialize();
    void Shutdown();
    void Resize(int width, int height);
    void World(const Ashen::Game& game);
    void EndFrame();
    void Panel(float x, float y, float width, float height, Color color);
    void Text(float x, float y, const std::wstring& text, int size = 18, Color color = {.86f, .84f, .75f, 1});
} // namespace AshenRenderer
