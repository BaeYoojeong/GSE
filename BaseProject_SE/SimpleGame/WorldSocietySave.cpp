#include "WorldSociety.h"
#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <set>

namespace Ashen
{
    bool WorldSociety::Read(std::istream& in, std::uint64_t currentLife)
    {
        std::string marker;
        size_t count = 0;
        if (!(in >> marker >> playerCoins >> notoriety >> simulationSeconds >> trades >> raids >> hunts >>
              count) ||
            (marker != "SOCIETY_V1" && marker != "SOCIETY_V2" && marker != "SOCIETY_V3" &&
             marker != "SOCIETY_V4" && marker != "SOCIETY_V5" && marker != "SOCIETY_V6") ||
            playerCoins < 0 || playerCoins > 1000000 || notoriety < 0 || notoriety > 100 ||
            !std::isfinite(simulationSeconds) || simulationSeconds < 0 || count < 18 || count > 1000210)
        {
            return false;
        }
        expandedWorld = marker == "SOCIETY_V5" || marker == "SOCIETY_V6";
        readingLegacyTowns = marker == "SOCIETY_V5";
        std::set<std::pair<ChunkKey, unsigned>> plots;
        const bool roaming = expandedWorld || marker == "SOCIETY_V3" || marker == "SOCIETY_V4";
        std::set<std::uint64_t> origins;
        auto finiteRange = [](float value, float low, float high)
        { return std::isfinite(value) && value >= low && value <= high; };
        auto validChunk = [](ChunkKey key)
        {
            constexpr auto limit = INT64_MAX / 4;
            return key.x >= -limit && key.x <= limit && key.y >= -limit && key.y <= limit;
        };

        for (size_t i = 0; i < count; ++i)
        {
            CitizenRecord npc;
            int profession = 0, ambition = 0, activity = 0;
            if (!(in >> npc.id >> npc.originLife >> npc.village.x >> npc.village.y >> npc.position.x >>
                  npc.position.y >> profession >> ambition >> activity >> npc.raidWeight >> npc.health >>
                  npc.maximumHealth >> npc.coins >> npc.goods >> npc.insight >> npc.actionTimer >>
                  npc.attackTimer >> npc.hostileSeconds >> npc.target >> npc.lastGiftLife >>
                  npc.lastTalkLife) ||
                npc.id == 0 || npc.originLife > currentLife ||
                (npc.originLife ? npc.id != 18 + npc.originLife
                                : (!expandedWorld ? npc.id > 18 : !TownWorld::SeedId(npc.id))) ||
                (npc.originLife && !origins.insert(npc.originLife).second) || !validChunk(npc.village) ||
                !finiteRange(npc.position.x,
                             expandedWorld ? -50000.0f
                             : roaming     ? -744.0f
                                           : 24.0f,
                             expandedWorld ? 50000.0f
                             : roaming     ? 1512.0f
                                           : 744.0f) ||
                !finiteRange(npc.position.y,
                             expandedWorld ? -50000.0f
                             : roaming     ? -744.0f
                                           : 24.0f,
                             expandedWorld ? 50000.0f
                             : roaming     ? 1512.0f
                                           : 744.0f) ||
                profession < 0 || profession > 3 || ambition < 0 || ambition > 2 || activity < 0 ||
                activity > 6 || !finiteRange(npc.raidWeight, 0, 1) ||
                !finiteRange(npc.maximumHealth, 90, 150) || !finiteRange(npc.health, 0, npc.maximumHealth) ||
                npc.coins < 0 || npc.coins > 1000000 || npc.goods < 0 || npc.goods > 99 || npc.insight < 0 ||
                npc.insight > 10000 || !finiteRange(npc.actionTimer, 0, 60) ||
                !finiteRange(npc.attackTimer, 0, 12) || !finiteRange(npc.hostileSeconds, 0, 30) ||
                npc.lastGiftLife > currentLife || npc.lastTalkLife > currentLife ||
                ((npc.health == 0) != (activity == static_cast<int>(Activity::Dead))))
            {
                return false;
            }
            int intention = activity < 5 ? activity : static_cast<int>(Activity::Rest);
            if (marker != "SOCIETY_V1")
            {
                if (!(in >> intention >> npc.raidCooldown >> npc.reviveProtection >> npc.deaths) ||
                    intention < 0 || intention > 4 || !finiteRange(npc.raidCooldown, 0, 24) ||
                    !finiteRange(npc.reviveProtection, 0, ReviveProtectionSeconds))
                {
                    return false;
                }
            }
            else if (activity == static_cast<int>(Activity::Raid) || npc.actionTimer > 6)
            {
                npc.raidCooldown = (std::min)(24.0f, npc.actionTimer);
                npc.actionTimer = 0;
            }
            if (expandedWorld)
            {
                int journey = 0;
                if (!(in >> npc.houseLot >> journey >> npc.destinationTown >> npc.pendingLot >> npc.mailId >>
                      npc.migrationCooldown >> npc.transitAge) ||
                    npc.houseLot > 1000210 || npc.houseLot == 35 || npc.pendingLot > 1000210 || journey < 0 ||
                    journey > 3 || IndexDuringRead(npc.village) < 0 ||
                    !finiteRange(npc.migrationCooldown, 0, 600) || !finiteRange(npc.transitAge, 0, 1000000) ||
                    (journey == 0 ? npc.destinationTown != -1
                                  : npc.destinationTown < 0 || npc.destinationTown >= 7) ||
                    (journey == 1 ? npc.mailId == 0 : npc.mailId != 0) ||
                    !plots.insert({npc.village, npc.houseLot}).second)
                {
                    return false;
                }
                npc.journeyKind = static_cast<JourneyKind>(journey);
                if (journey == 2 && npc.destinationTown != IndexDuringRead(npc.village))
                {
                    return false;
                }
                if (journey == 3 &&
                    (npc.destinationTown == IndexDuringRead(npc.village) || npc.pendingLot == 35))
                {
                    return false;
                }
            }
            npc.intention = static_cast<Activity>(intention);
            npc.profession = static_cast<Profession>(profession);
            npc.ambition = static_cast<Ambition>(ambition);
            npc.activity = static_cast<Activity>(activity);
            if (!citizens.emplace(npc.id, npc).second)
            {
                return false;
            }
        }
        for (std::uint64_t id = 1; id <= 18; ++id)
        {
            const auto* npc = Find(id);
            if (!npc || npc->originLife != 0 ||
                (expandedWorld ? IndexDuringRead(npc->village) < 0
                               : !(npc->village == ChunkKey{0, 0} || npc->village == ChunkKey{1, 0} ||
                                   npc->village == ChunkKey{0, 1})))
            {
                return false;
            }
        }
        if (expandedWorld)
        {
            for (unsigned index = 18; index < TownWorld::InitialPopulation; ++index)
            {
                const auto* npc = Find(TownWorld::SeedIdBase + index);
                if (!npc || npc->originLife != 0)
                {
                    return false;
                }
            }
            for (const auto& pair : citizens)
            {
                const auto& npc = pair.second;
                if (npc.journeyKind == JourneyKind::Migration &&
                    !plots.insert({CenterDuringRead(npc.destinationTown), npc.pendingLot}).second)
                {
                    return false;
                }
            }
        }
        if (!(in >> count) || (expandedWorld && count != TownWorld::Count * HuntingSlots) || count < 6 ||
            count > 6000018)
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            HuntingGround ground;
            // V1: two grounds; V2: two or four; V3: three; V4: six outdoor grounds.
            if (!(in >> ground.village.x >> ground.village.y >> ground.slot >> ground.health >>
                  ground.respawnSeconds >> ground.attackTimer) ||
                !validChunk(ground.village) || (expandedWorld && IndexDuringRead(ground.village) < 0) ||
                ground.slot < 0 ||
                ground.slot >= (marker == "SOCIETY_V1"   ? 2
                                : marker == "SOCIETY_V2" ? 4
                                : marker == "SOCIETY_V3" ? 3
                                                         : HuntingSlots) ||
                !finiteRange(ground.health, 0,
                             (expandedWorld || marker == "SOCIETY_V4") ? HuntingGround::MaximumHealth : 60) ||
                !finiteRange(ground.respawnSeconds, 0,
                             marker == "SOCIETY_V1"   ? 35.0f
                             : marker == "SOCIETY_V2" ? 6.0f
                                                      : QuarryRespawnSeconds) ||
                !finiteRange(ground.attackTimer, 0, 2) || (ground.health > 0 && ground.respawnSeconds > 0) ||
                !hunting.emplace(std::make_pair(ground.village, ground.slot), ground).second)
            {
                return false;
            }
        }
        for (const auto& pair : citizens)
        {
            const int slots = (expandedWorld || marker == "SOCIETY_V4") ? HuntingSlots
                              : marker == "SOCIETY_V3"                  ? 3
                                                                        : 2;
            for (int slot = 0; slot < slots; ++slot)
            {
                if (!Quarry(pair.second.village, slot))
                {
                    return false;
                }
            }
            if (pair.second.target && !Find(pair.second.target))
            {
                return false;
            }
        }
        for (auto it = hunting.begin(); it != hunting.end();)
        {
            if (it->second.slot >= HuntingSlots || (marker == "SOCIETY_V2" && it->second.slot == 3))
            {
                it = hunting.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (expandedWorld && !ReadNetwork(in))
        {
            return false;
        }
        if (readingLegacyTowns)
        {
            for (auto& pair : citizens)
            {
                pair.second.village = TownWorld::Towns()[IndexDuringRead(pair.second.village)].center;
            }
            decltype(hunting) migrated;
            for (auto& pair : hunting)
            {
                auto ground = pair.second;
                ground.village = TownWorld::Towns()[IndexDuringRead(ground.village)].center;
                migrated.emplace(std::make_pair(ground.village, ground.slot), ground);
            }
            hunting = std::move(migrated);
            readingLegacyTowns = false;
        }
        return true;
    }

    void WorldSociety::Write(std::ostream& out) const
    {
        out << "SOCIETY_V6 " << playerCoins << ' ' << notoriety << ' ' << simulationSeconds << ' ' << trades
            << ' ' << raids << ' ' << hunts << ' ' << citizens.size() << '\n';
        for (const auto& pair : citizens)
        {
            const auto& npc = pair.second;
            out << npc.id << ' ' << npc.originLife << ' ' << npc.village.x << ' ' << npc.village.y << ' '
                << npc.position.x << ' ' << npc.position.y << ' ' << static_cast<int>(npc.profession) << ' '
                << static_cast<int>(npc.ambition) << ' ' << static_cast<int>(npc.activity) << ' '
                << npc.raidWeight << ' ' << npc.health << ' ' << npc.maximumHealth << ' ' << npc.coins << ' '
                << npc.goods << ' ' << npc.insight << ' ' << npc.actionTimer << ' ' << npc.attackTimer << ' '
                << npc.hostileSeconds << ' ' << npc.target << ' ' << npc.lastGiftLife << ' '
                << npc.lastTalkLife << ' ' << static_cast<int>(npc.intention) << ' ' << npc.raidCooldown
                << ' ' << npc.reviveProtection << ' ' << npc.deaths << ' ' << npc.houseLot << ' '
                << static_cast<int>(npc.journeyKind) << ' ' << npc.destinationTown << ' ' << npc.pendingLot
                << ' ' << npc.mailId << ' ' << npc.migrationCooldown << ' ' << npc.transitAge << '\n';
        }
        out << hunting.size() << '\n';
        for (const auto& pair : hunting)
        {
            const auto& ground = pair.second;
            out << ground.village.x << ' ' << ground.village.y << ' ' << ground.slot << ' ' << ground.health
                << ' ' << ground.respawnSeconds << ' ' << ground.attackTimer << '\n';
        }
        WriteNetwork(out);
    }
} // namespace Ashen
