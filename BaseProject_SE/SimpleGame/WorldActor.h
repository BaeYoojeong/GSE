#pragma once

#include "WorldTypes.h"
#include <string>

namespace Ashen
{
    class Game;

    class WorldActor : public Actor
    {
      public:
        WorldActor(Game& owner, Kind kind);
        void Update(float seconds) final;
        void FaceDirection(Vec2 worldDirection);
        virtual bool CanInteract() const;
        virtual bool Interact();
        virtual std::wstring InteractionPrompt() const;
        Kind kind;
        ChunkKey chunk;
        Vec2 home;
        int slot = 0;
        unsigned variant = 0;
        std::uint64_t memory = 0;
        float health = 45;
        float cooldown = 0;
        float windup = 0;
        float flash = 0;
        bool solid = false;
        Vec2 velocity;
        float remainingRange = 0;
        float damage = 0;
        int facing = 3;
        bool moving = false;
        float walkCycle = 0;
        float attackVisualSeconds = 0;
        Vec2 attackVisualTarget = {};
        enum class Motion
        {
            Idle,
            Walk,
            Run,
            Jump,
            TurnLeft,
            TurnRight
        };
        Motion motion = Motion::Idle, previousMotion = Motion::Idle;
        float motionSeconds = 0, previousMotionSeconds = 0, blendSeconds = 1;
        float visualYaw = 2.35619449f, targetYaw = 2.35619449f;
        float jumpRemaining = 0, jumpCooldown = 0, turnRemaining = 0;
        bool sprinting = false;
        void Animate(float seconds);
        static const char* MotionName(Motion value);
        static bool Loops(Motion value);

      protected:
        Game& Owner() const
        {
            return game;
        }

        virtual void OnUpdate(float seconds);

      private:
        Game& game;
    };

} // namespace Ashen
