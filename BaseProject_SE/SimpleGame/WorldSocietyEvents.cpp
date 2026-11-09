#include "WorldSociety.h"
#include "RenderProfiler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>

namespace Ashen
{
    const wchar_t* WorldSociety::RaidPhase(const CitizenRecord& npc)
    {
        if (npc.health <= 0)
        {
            return L"사망";
        }
        if (npc.raidResultSeconds > 0)
        {
            return L"약탈 성공";
        }
        if (npc.hostileSeconds > 0 && npc.activity == Activity::Raid)
        {
            return L"플레이어와 교전";
        }
        if (npc.raidCooldown > 0)
        {
            return L"재약탈 대기";
        }
        if (npc.intention == Activity::Raid)
        {
            return npc.target ? L"약탈 대상 추적" : L"야외 대상 탐색";
        }
        return L"다른 활동 중";
    }

    void WorldSociety::RecordRaid(CitizenRecord& npc, std::uint64_t target, int amount)
    {
        if (amount <= 0)
        {
            return;
        }
        ++raids;
        ++raidsSinceReport;
        ++npc.successfulRaids;
        npc.stolenCoins += static_cast<std::uint64_t>(amount);
        npc.lastRaidTarget = target;
        npc.lastRaidAmount = amount;
        npc.raidResultSeconds = 4;
        const CitizenRecord* victim = target ? Find(target) : nullptr;
        std::ostringstream transfer;
        transfer << "npc=" << npc.id << " target=" << target << " transferred=" << amount
                 << " attacker_coins_after=" << npc.coins
                 << " victim_coins_after=" << (victim ? victim->coins : playerCoins);
        RenderProfiler::Event("raid_transfer", transfer.str());
    }

    std::uint64_t WorldSociety::RecordEvent(const char* type, std::uint64_t actor, std::uint64_t target,
                                            int amount, const std::wstring& message, const ChunkKey* village,
                                            unsigned witnesses, std::uint64_t sourceEvent)
    {
        SocietyEvent event;
        if (lastMonitorEvent < UINT64_MAX)
        {
            ++lastMonitorEvent;
        }
        event.id = lastMonitorEvent;
        event.timeSeconds = simulationSeconds;
        event.type = type;
        event.originalTarget = target;
        event.originalMessage = message;
        event.actor = actor;
        event.target = target;
        event.amount = amount;
        event.message = message;
        event.severity = "info";
        event.category = "progress";
        event.witnessCount = witnesses;
        event.sourceEvent = sourceEvent;

        // Hunt targets are quarry slot numbers, never citizen identities.
        if (event.type == "hunt" || event.type == "player_hunt")
        {
            event.target = 0;
        }

        const CitizenRecord* resident = Find(event.actor);
        if (!resident && event.target)
        {
            resident = Find(event.target);
        }
        if (village || resident)
        {
            event.hasVillage = true;
            event.village = village ? *village : resident->village;
            const int town = TownWorld::At(event.village);
            if (town >= 0)
            {
                event.village = TownWorld::Towns()[town].center;
            }
        }

        if (event.type.rfind("letter_", 0) == 0)
        {
            event.category = "communication";
            event.target = 0; // Mail IDs are not NPC IDs.
            event.message += L" · 편지 #" + std::to_wstring(target);
        }
        else if (event.type.rfind("migration_", 0) == 0)
        {
            event.category = "life";
        }
        if (event.type == "murder")
        {
            event.severity = "critical";
            event.category = "combat";
        }
        else if (event.type == "murder_rumor")
        {
            event.severity = "warning";
            event.category = "rumor";
            event.confirmed = false;
        }
        else if (event.type == "raid" || event.type == "rob_player" || event.type == "player_hit")
        {
            event.severity = "warning";
            event.category = "combat";
        }
        else if (event.type == "hunt" || event.type == "player_hunt")
        {
            event.category = "combat";
        }
        else if (event.type == "death" || event.type == "revive" || event.type == "load_revive" ||
                 event.type == "birth")
        {
            event.severity = event.type == "death" ? "warning" : "info";
            event.category = "life";
        }
        else if (event.type == "trade" || event.type == "player_buy" || event.type == "player_sell" ||
                 event.type == "death_loot" || event.type == "gift")
        {
            event.category = "economy";
        }

        if (event.hasVillage &&
            (event.type == "murder" || event.type == "raid" || event.type == "rob_player" ||
             event.type == "trade" || event.type == "player_buy" || event.type == "player_sell"))
        {
            const double second = std::floor(simulationSeconds);
            const auto slot = static_cast<std::size_t>(std::fmod(second, RecentEventSeconds));
            auto& bucket = incidentBuckets[event.village][slot];
            if (bucket.second != second)
            {
                bucket = {};
                bucket.second = second;
            }

            auto& counts = bucket.summary;
            if (event.type == "murder")
            {
                ++counts.murders;
            }
            else if (event.type == "raid" || event.type == "rob_player")
            {
                ++counts.raids;
            }
            else
            {
                ++counts.trades;
            }
            counts.lastIncidentSeconds = simulationSeconds;
        }

        std::ostringstream detail;
        detail << "event=" << event.type << " event_id=" << event.id << " sim_s=" << simulationSeconds
               << " actor=" << event.actor << " target=" << event.target << " amount=" << event.amount
               << " confirmed=" << event.confirmed << " witnesses=" << event.witnessCount
               << " source_event=" << event.sourceEvent;
        if (event.hasVillage)
        {
            detail << " village=(" << event.village.x << ',' << event.village.y << ')';
        }
        if (event.type == "hunt" || event.type == "player_hunt")
        {
            detail << " quarry_slot=" << target;
        }
        if (event.type.rfind("letter_", 0) == 0)
        {
            detail << " letter_id=" << target;
        }
        RenderProfiler::Event("society", detail.str());
        if (!backgroundSimulation)
        {
            std::printf("[society] %s\n", detail.str().c_str());
        }
        if (backgroundSimulation)
        {
            backgroundEvents.push_back(event);
        }

        monitorEvents.push_back(std::move(event));
        while (monitorEvents.size() > EventCapacity)
        {
            monitorEvents.pop_front();
        }
        return lastMonitorEvent;
    }

    IncidentSummary WorldSociety::RecentIncidents(ChunkKey village) const
    {
        IncidentSummary result;
        const auto found = incidentBuckets.find(village);
        if (found == incidentBuckets.end())
        {
            return result;
        }

        const double second = std::floor(simulationSeconds);
        for (const auto& bucket : found->second)
        {
            if (bucket.second < 0 || bucket.second > second || bucket.second <= second - RecentEventSeconds)
            {
                continue;
            }
            result.murders += bucket.summary.murders;
            result.raids += bucket.summary.raids;
            result.trades += bucket.summary.trades;
            result.lastIncidentSeconds =
                (std::max)(result.lastIncidentSeconds, bucket.summary.lastIncidentSeconds);
        }

        return result;
    }
} // namespace Ashen
