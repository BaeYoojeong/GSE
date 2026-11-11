#include "DarkAgeGame.h"
#include "WorldActors.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>

namespace Ashen
{
    bool Game::ActivateWaypoint(unsigned town)
    {
        if (town >= TownWorld::Count || !hero || Dead())
        {
            return false;
        }
        const Vec2 portal = Relative(Waypoints::Position(town));
        if (DistanceSquared(hero->position, portal) > 86 * 86 || !ClearPath(hero->position, portal))
        {
            Notify(L"포탈 가까이에서 E를 눌러 활성화하세요.");
            return false;
        }
        if (!storageHealthy)
        {
            Notify(L"저장 오류를 해결한 뒤 포탈을 활성화하세요. F8 재시도");
            return false;
        }
        if (!waypoints.Active(town))
        {
            const auto previous = waypoints;
            waypoints.Activate(town);
            dirty = true;
            if (!Save())
            {
                waypoints = previous;
                dirty = true;
                Notify(L"포탈 활성화를 저장하지 못했습니다. F8 저장 후 E로 다시 시도하세요.");
                return false;
            }
            RenderProfiler::Event("waypoint_activated", "town=" + std::to_string(town));
            Notify(std::wstring(TownWorld::Towns()[town].name) + L" 포탈 활성화 · 지도에서 순간이동 가능");
        }
        requestedMap = static_cast<int>(town);
        ClearInput();
        return true;
    }

    bool Game::TravelToWaypoint(unsigned town)
    {
        if (town >= TownWorld::Count || !hero || Dead())
        {
            return false;
        }
        if (!waypoints.Active(town))
        {
            Notify(L"미활성 포탈입니다. 해당 마을을 방문해 E로 활성화하세요.");
            return false;
        }
        if (!storageHealthy)
        {
            Notify(L"저장 오류를 해결한 뒤 이동하세요. F8 재시도");
            return false;
        }
        const Location previous = Absolute(hero->position);
        const Vec2 oldCamera = camera;
        ClearInput();
        ClearProjectiles();
        CloseDialogue();
        requestedShop = tradingMerchant = 0;
        // Keep large-world travel exact: integer chunk origin plus a small local position.
        const auto relocate = [&](const Location& location)
        {
            origin = location.chunk;
            hero->position = location.local;
            camera = location.local;
            Stream();
        };
        relocate(Waypoints::Position(town));
        const Vec2 portal = hero->position;
        bool found = false;
        const auto nearby = scene.Snapshot();
        for (unsigned ring = 0; ring < 4 && !found; ++ring)
        {
            for (unsigned slot = 0; slot < 12; ++slot)
            {
                const float angle = 1.570796327f + slot * .523598776f;
                const float radius = 56 + ring * 16.0f;
                const Vec2 point = {portal.x + std::cos(angle) * radius, portal.y + std::sin(angle) * radius};
                if (!Walkable(point, 16))
                {
                    continue;
                }
                bool occupied = false;
                for (Actor* base : nearby)
                {
                    const auto* actor = dynamic_cast<const WorldActor*>(base);
                    if (actor && actor->IsActive() && actor->health > 0 &&
                        (actor->kind == Kind::Enemy || actor->kind == Kind::Quarry ||
                         actor->kind == Kind::Citizen) &&
                        DistanceSquared(point, actor->WorldPosition()) < 42 * 42)
                    {
                        occupied = true;
                        break;
                    }
                }
                if (!occupied)
                {
                    hero->position = point;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            relocate(previous);
            camera = oldCamera;
            Notify(L"포탈 주변에 안전한 도착 공간이 없어 이동하지 않았습니다.");
            return false;
        }
        camera = hero->position;
        hero->moving = false;
        hero->motion = hero->previousMotion = WorldActor::Motion::Idle;
        hero->motionSeconds = hero->previousMotionSeconds = 0;
        immunity = (std::max)(immunity, 1.5f);
        requestedMap = -1;
        dirty = true;
        RenderProfiler::Event("waypoint_travel", "town=" + std::to_string(town));
        if (Save())
        {
            Notify(std::wstring(TownWorld::Towns()[town].name) + L" 포탈로 이동했습니다.");
        }
        else
        {
            Notify(L"이동했지만 기록 저장에 실패했습니다. F8로 다시 저장하세요.");
        }
        return true;
    }

    WaypointActor::WaypointActor(Game& owner) : InteractiveActor(owner, Kind::Waypoint)
    {
    }

    bool WaypointActor::Interact()
    {
        return Owner().ActivateWaypoint(static_cast<unsigned>(slot));
    }

    std::wstring WaypointActor::InteractionPrompt() const
    {
        return Owner().waypoints.Active(static_cast<unsigned>(slot)) ? L"E 포탈 지도 열기 · 활성화됨"
                                                                     : L"E 순간이동 포탈 활성화";
    }
} // namespace Ashen
