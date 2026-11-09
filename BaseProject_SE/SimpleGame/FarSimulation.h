#pragma once
#include <memory>
#include <cstdint>

namespace Ashen
{
    class Game;

    // Worker owns a detached logical-world snapshot. Only the game thread may commit results.
    class FarSimulation
    {
      public:
        struct Statistics
        {
            std::uint64_t accepted = 0, rejected = 0, residents = 0;
            double workerMilliseconds = 0, commitMilliseconds = 0;
        };

        FarSimulation();
        ~FarSimulation();
        FarSimulation(const FarSimulation&) = delete;
        FarSimulation& operator=(const FarSimulation&) = delete;
        void Submit(Game& game);
        bool Poll(Game& game);
        bool WaitReady(unsigned milliseconds);
        Statistics Stats() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        void Run();
    };
} // namespace Ashen
