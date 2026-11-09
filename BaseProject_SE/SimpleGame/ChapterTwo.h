#pragma once

#include "WorldTypes.h"
#include <iosfwd>
#include <string>

namespace Ashen
{
    class Game;
    class WorldActor;

    // Persistent chapter facts are independent of streamed actors and of the current life.
    class ChapterTwo
    {
      public:
        enum class Stage
        {
            Tutorial,
            PilgrimRoad,
            Offering,
            BellKeeper,
            RingBell,
            Complete
        };
        Stage CurrentStage() const;
        bool Complete() const;
        float BossMaximumHealth() const;
        static Location Road();
        static Location Altar();
        static Location Arena();
        void Update(Game& game);
        void Spawn(Game& game, ChunkKey chunk, ActorId parent);
        bool Interact(Game& game);
        void DamageBoss(Game& game, WorldActor& boss, float amount);
        void ResetBattle(Game& game, const char* reason);
        const WorldActor* Boss(const Game& game) const;
        Location Destination() const;
        std::wstring Objective() const;
        std::wstring Direction(const Game& game) const;
        std::wstring Journal() const;
        bool Read(std::istream& input);
        void Write(std::ostream& output) const;

      private:
        unsigned flags = 0;
        float bossMaximumHealth = 600;
    };
} // namespace Ashen
