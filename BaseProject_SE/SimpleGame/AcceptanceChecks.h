#pragma once
#include <filesystem>
#include <functional>

namespace Ashen
{
    class Game;

    class AcceptanceChecks
    {
      public:
        // Explicit opt-in; refuses an existing directory and never opens the normal game profile.
        static int Run(const std::filesystem::path& directory);

      private:
        static void OpenWorld(Game& game, const std::function<void(bool, const char*)>& require);
        static void MapAndWaypoints(Game& game, const std::function<void(bool, const char*)>& require);
        static void InventoryAndMarkets(Game& game, const std::function<void(bool, const char*)>& require);
    };
} // namespace Ashen
