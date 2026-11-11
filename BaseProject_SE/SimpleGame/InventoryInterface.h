#pragma once

#include "WindowInput.h"

namespace Ashen
{
    class Game;
}

namespace InventoryInterface
{
    bool IsOpen();
    void Close(Ashen::Game& game);
    void OpenInventory(Ashen::Game& game);
    void OpenShop(Ashen::Game& game);
    void Update(Ashen::Game& game);
    bool Key(Ashen::Game& game, unsigned char key, bool down);
    bool Arrow(WorldScene::SpecialKey key, bool down);
    void Render(const Ashen::Game& game);
} // namespace InventoryInterface
