#pragma once

#include "MonitorServer.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <memory>

namespace Ashen
{
    class Game;

    class WorldMonitor
    {
      public:
        WorldMonitor();
        ~WorldMonitor();
        void Start(const Game& game);
        void Update(const Game& game, bool force = false);
        void Stop();

      private:
        struct Sample;
        struct Async;
        std::unique_ptr<Async> async;
        std::string Snapshot(const Sample& game);
        MonitorServer server;
        bool active = false;
        std::string sessionId;
        std::uint64_t sequence = 0;
        std::chrono::steady_clock::time_point nextPublish;
    };
} // namespace Ashen
