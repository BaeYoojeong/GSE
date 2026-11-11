#include "WorldActor.h"
#include "DarkAgeGame.h"
#include "AssetCatalog.h"
#include <cmath>
#include <algorithm>

namespace Ashen
{
    const char* WorldActor::MotionName(Motion value)
    {
        const char* names[] = {"idle", "walk", "run", "jump", "turn_left", "turn_right"};
        return names[static_cast<unsigned>(value)];
    }

    bool WorldActor::Loops(Motion value)
    {
        return value == Motion::Idle || value == Motion::Walk || value == Motion::Run;
    }

    void WorldActor::Animate(float seconds)
    {
        if (kind != Kind::Player && kind != Kind::Citizen && kind != Kind::Heir && kind != Kind::Settler &&
            kind != Kind::Enemy && kind != Kind::Quarry)
        {
            return;
        }
        const float yaw[] = {-2.35619449f, -.785398163f, .785398163f, 2.35619449f};
        const float desired = yaw[static_cast<unsigned>(facing) % 4];
        auto angle = [](float value) { return std::remainder(value, 6.283185307f); };
        if (std::fabs(angle(desired - targetYaw)) > .01f)
        {
            const float delta = angle(desired - visualYaw);
            targetYaw = desired;
            if (!moving && jumpRemaining <= 0)
            {
                turnRemaining = AssetCatalog::ClipDuration(delta < 0 ? "turn_left" : "turn_right");
                const Motion turn = delta < 0 ? Motion::TurnLeft : Motion::TurnRight;
                previousMotion = motion;
                previousMotionSeconds = motionSeconds;
                motion = turn;
                motionSeconds = 0;
                blendSeconds = 0;
            }
        }
        const float difference = angle(targetYaw - visualYaw);
        const float rate = moving ? 16.f : 6.f;
        visualYaw = angle(visualYaw + difference * (1 - std::exp(-seconds * rate)));
        jumpCooldown = (std::max)(0.f, jumpCooldown - seconds);
        jumpRemaining = (std::max)(0.f, jumpRemaining - seconds);
        turnRemaining = (std::max)(0.f, turnRemaining - seconds);
        const Motion next = jumpRemaining > 0 ? Motion::Jump
                            : moving          ? (sprinting ? Motion::Run : Motion::Walk)
                            : turnRemaining > 0 && (motion == Motion::TurnLeft || motion == Motion::TurnRight)
                                ? motion
                                : Motion::Idle;
        if (next != motion)
        {
            previousMotion = motion;
            previousMotionSeconds = motionSeconds;
            motion = next;
            motionSeconds = 0;
            blendSeconds = 0;
        }
        else
        {
            motionSeconds += seconds;
        }
        previousMotionSeconds += seconds;
        blendSeconds += seconds;
    }
} // namespace Ashen
