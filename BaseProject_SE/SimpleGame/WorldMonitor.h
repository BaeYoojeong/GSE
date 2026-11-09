#pragma once

#include "MonitorServer.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace Ashen
{
    class Game;

    class WorldMonitor
    {
      public:
        void Start(const Game& game);
        void Update(const Game& game, bool force = false);
        void Stop();

      private:
        std::string Snapshot(const Game& game);
        MonitorServer server;
        bool active = false;
        std::string sessionId;
        std::uint64_t sequence = 0;
        std::chrono::steady_clock::time_point nextPublish;
    };
} // namespace Ashen
