#include "WorldActor.h"
#include "DarkAgeGame.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    WorldActor::WorldActor(Game& owner, Kind type) : kind(type), game(owner)
    {
        renderable = true;
    }

    void WorldActor::Update(float seconds)
    {
        struct AnimationStep
        {
            WorldActor& actor;
            float dt;

            ~AnimationStep()
            {
                actor.Animate(dt);
            }
        } animation{*this, seconds};

        const Vec2 previous = WorldPosition();
        moving = false;
        flash = (std::max)(0.0f, flash - seconds);
        cooldown = (std::max)(0.0f, cooldown - seconds);

        if (game.Dead() && kind != Kind::Citizen && kind != Kind::Quarry)
        {
            return;
        }

        OnUpdate(seconds);

        if (!IsActive() || kind == Kind::Citizen)
        {
            // Citizen proxies can be reparented before this frame; their simulation supplies motion.
            return;
        }

        if (kind != Kind::Enemy && kind != Kind::Settler && kind != Kind::Heir && kind != Kind::Citizen)
        {
            return;
        }

        const Vec2 current = WorldPosition();
        const Vec2 delta = {current.x - previous.x, current.y - previous.y};
        const float distance = std::sqrt(DistanceSquared(current, previous));
        moving = distance > .001f;

        if (moving)
        {
            if ((kind != Kind::Heir || flash <= 0) && attackVisualSeconds <= 0)
            {
                FaceDirection(delta);
            }

            walkCycle = std::fmod(walkCycle + distance / 10, 4.0f);
        }
        else if ((kind == Kind::Settler || kind == Kind::Heir) && flash <= 0 &&
                 DistanceSquared(current, game.Hero().WorldPosition()) < 160 * 160 &&
                 game.ClearPath(current, game.Hero().WorldPosition()))
        {
            const Vec2 hero = game.Hero().WorldPosition();
            FaceDirection({hero.x - current.x, hero.y - current.y});
        }
    }

    void WorldActor::FaceDirection(Vec2 worldDirection)
    {
        if (worldDirection.x * worldDirection.x + worldDirection.y * worldDirection.y < .000001f)
        {
            return;
        }

        const Vec2 screen = {CameraCos * worldDirection.x - CameraSin * worldDirection.y,
                             CameraDepth * (CameraSin * worldDirection.x + CameraCos * worldDirection.y)};

        if (std::fabs(screen.x) > std::fabs(screen.y))
        {
            facing = screen.x < 0 ? 0 : 2;
        }
        else
        {
            facing = screen.y < 0 ? 1 : 3;
        }
    }

    void WorldActor::OnUpdate(float seconds)
    {
        // Static scenery only needs the shared timers and scene transform.
        (void)seconds;
    }

    bool WorldActor::CanInteract() const
    {
        return false;
    }

    bool WorldActor::Interact()
    {
        return false;
    }

    std::wstring WorldActor::InteractionPrompt() const
    {
        return {};
    }
} // namespace Ashen
