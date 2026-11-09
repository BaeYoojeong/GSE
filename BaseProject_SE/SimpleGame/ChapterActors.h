#pragma once
#include "WorldActors.h"

namespace Ashen
{
    class BellAltarActor final : public InteractiveActor
    {
      public:
        explicit BellAltarActor(Game& game);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;
    };

    class BellKeeperActor final : public WorldActor
    {
      public:
        explicit BellKeeperActor(Game& game);

      protected:
        void OnUpdate(float seconds) override;

      private:
        unsigned attacks = 0;
    };
} // namespace Ashen
