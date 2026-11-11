#pragma once

#include "DarkAgeGame.h"
#include <vector>

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
    unsigned CreateImage(int width, int height, const std::vector<unsigned char>& rgba);
    void DeleteImage(unsigned texture);
    void Image(float x, float y, float width, float height, unsigned texture);
    bool UiPoint(float framebufferX, float framebufferY, Ashen::Vec2& point);
    void Clip(float x, float y, float width, float height);
    void EndClip();
    void Panel(float x, float y, float width, float height, Color color);
    void Text(float x, float y, const std::wstring& text, int size = 18, Color color = {.86f, .84f, .75f, 1});
} // namespace AshenRenderer
