#pragma once

#include "WorldTypes.h"
#include <vector>

namespace Ashen
{
    class Game;
}

namespace WorldMap
{
    constexpr int Width = 520, Height = 390;
    constexpr float Left = 72, Top = 211;

    struct View
    {
        Ashen::Location center = {{5, 5}, {384, 384}};
        float pixelsPerChunk = 26;
    };

    View CurrentView();
    void Focus(const Ashen::Location& location);
    void FocusPlayer(const Ashen::Game& game);
    void Overview();
    void Pan(float pixelsX, float pixelsY);
    void Zoom(float x, float y, double steps);
    bool Contains(float x, float y);
    bool BeginDrag(float x, float y);
    void Drag(float x, float y);
    bool EndDrag(float x, float y);
    void CancelDrag();
    Ashen::Vec2 Marker(unsigned town);
    int Hit(float x, float y);
    bool TravelButton(float x, float y);
    bool Project(const Ashen::Location& position, Ashen::Vec2& point);
    std::vector<unsigned char> BuildImage(const Ashen::Game& game);
    void Render(const Ashen::Game& game, int selectedTown);
    void Shutdown();
} // namespace WorldMap
