#pragma once

#include "WorldActor.h"

namespace Ashen
{
    class InteractiveActor : public WorldActor
    {
      public:
        bool CanInteract() const final;
        bool Interact() override = 0;
        std::wstring InteractionPrompt() const override = 0;

      protected:
        InteractiveActor(Game& owner, Kind kind);
    };

    class ResidenceActor final : public InteractiveActor
    {
      public:
        explicit ResidenceActor(Game& owner);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;
    };

    class CitizenActor final : public InteractiveActor
    {
      public:
        explicit CitizenActor(Game& owner);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;

      protected:
        void OnUpdate(float seconds) override;
    };

    class QuarryActor final : public WorldActor
    {
      public:
        explicit QuarryActor(Game& owner);

      protected:
        void OnUpdate(float seconds) override;
    };

    class PlayerActor final : public WorldActor
    {
      public:
        explicit PlayerActor(Game& owner);

      protected:
        void OnUpdate(float seconds) override;
    };

    class EnemyActor final : public WorldActor
    {
      public:
        explicit EnemyActor(Game& owner);

      protected:
        void OnUpdate(float seconds) override;
    };

    class HeirActor final : public InteractiveActor
    {
      public:
        explicit HeirActor(Game& owner);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;

      protected:
        void OnUpdate(float seconds) override;
    };

    class ProjectileActor final : public WorldActor
    {
      public:
        explicit ProjectileActor(Game& owner);

      protected:
        void OnUpdate(float seconds) override;
    };

    class DropActor final : public WorldActor
    {
      public:
        explicit DropActor(Game& owner);

      protected:
        void OnUpdate(float seconds) override;
    };

    class HerbActor final : public InteractiveActor
    {
      public:
        explicit HerbActor(Game& owner);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;
    };

    class FireActor final : public InteractiveActor
    {
      public:
        explicit FireActor(Game& owner);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;
    };

    class SettlerActor final : public InteractiveActor
    {
      public:
        explicit SettlerActor(Game& owner);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;
    };

    class GraveActor final : public InteractiveActor
    {
      public:
        explicit GraveActor(Game& owner);
        bool Interact() override;
        std::wstring InteractionPrompt() const override;
    };

} // namespace Ashen
