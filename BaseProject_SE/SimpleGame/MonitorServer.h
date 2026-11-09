#pragma once

#include <memory>
#include <string>

namespace Ashen
{
    // The worker owns HTTP transport only. Game state is serialized on the game thread.
    class MonitorServer
    {
      public:
        MonitorServer();
        ~MonitorServer();

        MonitorServer(const MonitorServer&) = delete;
        MonitorServer& operator=(const MonitorServer&) = delete;

        bool Start(unsigned short port = 8765);
        void Publish(std::string json);
        void Stop();

      private:
        struct Impl;
        std::unique_ptr<Impl> implementation;
    };
} // namespace Ashen
