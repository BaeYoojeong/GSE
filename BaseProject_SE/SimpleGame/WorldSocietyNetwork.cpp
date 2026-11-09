#include "WorldSociety.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <istream>
#include <ostream>
#include <set>

namespace Ashen
{
    namespace
    {
        float Appeal(const CitizenRecord& npc, const TownNews& report)
        {
            // Information available at home, never a remote live population query.
            return (1 - npc.raidWeight) * report.safety * 22 + npc.raidWeight * (1 - report.safety) * 34 +
                   report.wealth * 12 + report.trade * (npc.profession == Profession::Merchant ? 40 : 8) +
                   report.learning * (npc.ambition == Ambition::Insight ? 44 : 6) +
                   (npc.ambition == Ambition::Vitality ? report.safety * 12 : 0);
        }

        void WriteReport(std::ostream& out, const TownNews& report)
        {
            out << report.authored << ' ' << report.safety << ' ' << report.wealth << ' ' << report.trade
                << ' ' << report.learning << ' ' << report.population << ' ' << report.murders << ' '
                << report.raids << '\n';
        }

        bool ReadReport(std::istream& in, TownNews& report, double now)
        {
            if (!(in >> report.authored >> report.safety >> report.wealth >> report.trade >>
                  report.learning >> report.population >> report.murders >> report.raids))
            {
                return false;
            }
            if (!std::isfinite(report.authored) || (report.authored < 0 && report.authored != -1) ||
                report.authored > now + .01 || report.population > 1000210)
            {
                return false;
            }
            for (float value : {report.safety, report.wealth, report.trade, report.learning})
            {
                if (!std::isfinite(value) || value < 0 || value > 1)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    const wchar_t* WorldSociety::JourneyName(JourneyKind kind)
    {
        const wchar_t* names[] = {L"마을 생활", L"편지 운반", L"전령 귀향", L"이사 중"};
        return names[static_cast<unsigned>(kind) % 4];
    }

    unsigned WorldSociety::VacantLot(ChunkKey town) const
    {
        std::set<unsigned> occupied;
        // The central chapel owns this plot; preserve a walkable margin around it.
        occupied.insert(35);
        for (const auto& pair : citizens)
        {
            const auto& npc = pair.second;
            if (npc.village == town)
            {
                occupied.insert(npc.houseLot);
            }
            if (npc.journeyKind == JourneyKind::Migration && npc.destinationTown >= 0 &&
                TownWorld::Towns()[npc.destinationTown].center == town)
            {
                occupied.insert(npc.pendingLot);
            }
        }
        // Disperse households across all nine districts before adding another row.
        for (unsigned order = 0; order < 72; ++order)
        {
            const unsigned lot = (order % 9) * 8 + order / 9;
            if (!occupied.count(lot))
            {
                return lot;
            }
        }
        unsigned lot = 72;
        while (occupied.count(lot))
        {
            ++lot;
        }
        return lot;
    }

    void WorldSociety::ExpandTowns()
    {
        if (expandedWorld)
        {
            return;
        }
        auto previous = std::move(citizens);
        citizens.clear();
        hunting.clear();
        for (auto& pair : previous)
        {
            auto npc = pair.second;
            const int town = npc.originLife ? TownWorld::Nearest(npc.village) : 0;
            npc.village = TownWorld::Towns()[town].center;
            npc.houseLot = VacantLot(npc.village);
            npc.position = TownWorld::Door(npc.houseLot);
            npc.target = 0;
            npc.intention = npc.activity = Activity::Rest;
            npc.hostileSeconds = 0;
            npc.migrationCooldown = 180 + static_cast<float>(npc.id % 120);
            citizens.emplace(npc.id, npc);
        }
        for (unsigned town = 0; town < TownWorld::Count; ++town)
        {
            for (unsigned slot = 0; slot < TownWorld::StartingResidents; ++slot)
            {
                const unsigned index = town * TownWorld::StartingResidents + slot;
                const std::uint64_t id = index < 18 ? index + 1 : TownWorld::SeedIdBase + index;
                if (Find(id))
                {
                    continue;
                }
                CitizenRecord npc;
                npc.id = id;
                npc.village = TownWorld::Towns()[town].center;
                npc.houseLot = VacantLot(npc.village);
                npc.position = TownWorld::Door(npc.houseLot);
                npc.profession = slot % 10 < 4   ? Profession::Hunter
                                 : slot % 10 < 6 ? Profession::Merchant
                                 : slot % 10 < 8 ? Profession::Seeker
                                                 : Profession::Raider;
                npc.ambition = npc.profession == Profession::Seeker ? Ambition::Insight
                               : slot % 3 == 0                      ? Ambition::Vitality
                                                                    : Ambition::Wealth;
                npc.raidWeight = npc.profession == Profession::Raider ? .82f : .03f + (slot % 5) * .025f;
                npc.coins = npc.profession == Profession::Merchant ? 80 : 24;
                npc.goods = npc.profession == Profession::Merchant ? 12 : 2;
                npc.actionTimer = static_cast<float>(slot % 6);
                npc.migrationCooldown = 180 + static_cast<float>(slot * 4);
                citizens.emplace(id, npc);
            }
            EnsureGrounds(TownWorld::Towns()[town].center);
        }
        expandedWorld = true;
        nextNetworkUpdate = simulationSeconds;
        RenderProfiler::Event("seven_towns_migrated",
                              "founders=210 towns=7 houses=" + std::to_string(citizens.size()));
    }

    void WorldSociety::CancelMission(CitizenRecord& npc)
    {
        if (npc.mailId)
        {
            Log("letter_lost", npc.id, npc.mailId, 0, L"전령 사망 · 운반 중이던 편지는 전달되지 않았습니다.",
                &npc.village);
            letters.erase(npc.mailId);
        }
        if (npc.journeyKind == JourneyKind::Migration)
        {
            Log("migration_cancelled", npc.id, 0, 0, L"이사 중 사망 · 기존 집에서 다시 시작합니다.",
                &npc.village);
        }
        npc.journeyKind = JourneyKind::None;
        npc.destinationTown = -1;
        npc.mailId = 0;
        npc.pendingLot = 0;
        npc.transitAge = 0;
        npc.migrationCooldown = (std::max)(npc.migrationCooldown, 180.0f);
    }

    void WorldSociety::NetworkTick()
    {
        if (simulationSeconds < nextNetworkUpdate)
        {
            return;
        }
        nextNetworkUpdate = simulationSeconds + 45;
        ++networkRound;
        // Every town authors only its own report. Relays keep the original timestamp.
        for (unsigned town = 0; town < TownWorld::Count; ++town)
        {
            const auto& definition = TownWorld::Towns()[town];
            TownNews report;
            report.authored = simulationSeconds;
            report.trade = definition.trade;
            report.learning = definition.learning;
            double money = 0, raiding = 0;
            for (const auto& pair : citizens)
            {
                const auto& npc = pair.second;
                if (!(npc.village == definition.center))
                {
                    continue;
                }
                ++report.population;
                money += npc.coins;
                raiding += npc.raidWeight;
            }
            const auto incidents = RecentIncidents(definition.center);
            report.murders = incidents.murders;
            report.raids = incidents.raids;
            const float count = static_cast<float>((std::max)(1u, report.population));
            report.wealth = static_cast<float>((std::min)(1.0, money / (count * 140)));
            report.safety = (std::clamp)(definition.law - static_cast<float>(raiding / count) * .2f -
                                             static_cast<float>(incidents.murders) * .04f -
                                             static_cast<float>(incidents.raids) * .005f,
                                         0.0f, 1.0f);
            knowledge[town][town] = report;
        }
        for (unsigned town = 0; town < TownWorld::Count; ++town)
        {
            const auto center = TownWorld::Towns()[town].center;
            bool outgoing = false;
            for (const auto& pair : letters)
            {
                outgoing |= pair.second.source == static_cast<int>(town);
            }
            if (!outgoing && letters.size() < TownWorld::Count * 2)
            {
                std::vector<int> neighbors;
                for (unsigned target = 0; target < TownWorld::Count; ++target)
                {
                    const auto next = TownWorld::Towns()[target].center;
                    if (std::abs(next.x - center.x) + std::abs(next.y - center.y) == TownWorld::CenterSpacing)
                    {
                        neighbors.push_back(static_cast<int>(target));
                    }
                }
                CitizenRecord* carrier = nullptr;
                for (auto& pair : citizens)
                {
                    auto& npc = pair.second;
                    if (npc.village == center && npc.journeyKind == JourneyKind::None &&
                        npc.profession == Profession::Merchant && npc.health >= npc.maximumHealth * .65f &&
                        npc.hostileSeconds <= 0 && npc.reviveProtection <= 0 &&
                        TownWorld::Contains(center, Locate(center, npc.position).chunk) &&
                        !IsMarketKeeper(npc))
                    {
                        carrier = &npc;
                        break;
                    }
                }
                if (carrier && !neighbors.empty())
                {
                    TownLetter letter;
                    letter.id = nextLetter++;
                    letter.carrier = carrier->id;
                    letter.source = static_cast<int>(town);
                    letter.destination = neighbors[(networkRound + town) % neighbors.size()];
                    letter.sent = simulationSeconds;
                    letter.reports = knowledge[town];
                    letters.emplace(letter.id, letter);
                    carrier->journeyKind = JourneyKind::Courier;
                    carrier->destinationTown = letter.destination;
                    carrier->mailId = letter.id;
                    carrier->transitAge = 0;
                    carrier->target = 0;
                    carrier->quarrySlot = -1;
                    Log("letter_dispatched", carrier->id, letter.id, letter.destination,
                        std::wstring(TownWorld::Towns()[town].name) + L" → " +
                            TownWorld::Towns()[letter.destination].name + L" 편지 출발",
                        &center);
                }
            }
            // At most one household from each town chooses a move per bulletin cycle.
            for (auto& pair : citizens)
            {
                auto& npc = pair.second;
                if (!(npc.village == center) || npc.journeyKind != JourneyKind::None ||
                    npc.migrationCooldown > 0 || npc.health < npc.maximumHealth * .5f ||
                    npc.hostileSeconds > 0)
                {
                    continue;
                }
                npc.migrationCooldown = 180 + static_cast<float>(npc.id % 120);
                int bestTown = -1;
                float best = Appeal(npc, knowledge[town][town]) + 9;
                for (unsigned target = 0; target < TownWorld::Count; ++target)
                {
                    const auto& report = knowledge[town][target];
                    if (target == town || report.authored < 0 || simulationSeconds - report.authored > 600 ||
                        report.population >= 45)
                    {
                        continue;
                    }
                    const float score = Appeal(npc, report);
                    if (score > best)
                    {
                        best = score;
                        bestTown = static_cast<int>(target);
                    }
                }
                if (bestTown < 0)
                {
                    continue;
                }
                const auto destination = TownWorld::Towns()[bestTown].center;
                npc.pendingLot = VacantLot(destination);
                const auto& received = knowledge[town][bestTown];
                RenderProfiler::Event("migration_decision",
                                      "npc=" + std::to_string(npc.id) + " source=" + std::to_string(town) +
                                          " destination=" + std::to_string(bestTown) + " current_score=" +
                                          std::to_string(Appeal(npc, knowledge[town][town])) +
                                          " destination_score=" + std::to_string(best) + " report_age_s=" +
                                          std::to_string(simulationSeconds - received.authored) +
                                          " reserved_lot=" + std::to_string(npc.pendingLot));
                npc.destinationTown = bestTown;
                npc.journeyKind = JourneyKind::Migration;
                npc.transitAge = 0;
                npc.target = 0;
                npc.quarrySlot = -1;
                Log("migration_started", npc.id, 0, bestTown,
                    std::wstring(TownWorld::Towns()[bestTown].name) +
                        L"의 전해 들은 생활 환경과 성향을 비교해 이사를 결정했습니다.",
                    &center);
                break;
            }
        }
    }

    bool WorldSociety::TravelMission(Game& game, CitizenRecord& npc, float dt)
    {
        if (npc.journeyKind == JourneyKind::None)
        {
            return false;
        }
        npc.transitAge += dt;
        const int target = npc.destinationTown;
        if (target < 0 || target >= static_cast<int>(TownWorld::Count))
        {
            CancelMission(npc);
            return false;
        }
        const ChunkKey destination = TownWorld::Towns()[target].center;
        const Vec2 point = npc.journeyKind == JourneyKind::Migration   ? TownWorld::Door(npc.pendingLot)
                           : npc.journeyKind == JourneyKind::Returning ? TownWorld::Door(npc.houseLot)
                                                                       : Vec2{384, 460};
        npc.activity = Activity::Return;
        npc.intention = Activity::Trade;
        npc.goalActivity = Activity::Trade;
        npc.activityGoal = InFrame(destination, point, npc.village);
        if (!Travel(game, npc, npc.activityGoal, dt))
        {
            return true;
        }
        if (npc.journeyKind == JourneyKind::Courier)
        {
            const auto it = letters.find(npc.mailId);
            if (it != letters.end())
            {
                const auto& letter = it->second;
                for (unsigned subject = 0; subject < TownWorld::Count; ++subject)
                {
                    if (letter.reports[subject].authored > knowledge[target][subject].authored)
                    {
                        knowledge[target][subject] = letter.reports[subject];
                    }
                }
                ++lettersDelivered;
                Log("letter_delivered", npc.id, letter.id, target,
                    std::wstring(TownWorld::Towns()[target].name) + L"에 편지 도착 · 새 소식이 알려졌습니다.",
                    &destination);
                letters.erase(it);
            }
            npc.mailId = 0;
            npc.journeyKind = JourneyKind::Returning;
            npc.destinationTown = TownWorld::Index(npc.village);
            npc.transitAge = 0;
        }
        else
        {
            if (npc.journeyKind == JourneyKind::Migration)
            {
                npc.position = InFrame(npc.village, npc.position, destination);
                npc.village = destination;
                npc.houseLot = npc.pendingLot;
                ++migrationsCompleted;
                Log("migration_arrived", npc.id, 0, static_cast<int>(npc.houseLot + 1),
                    std::wstring(TownWorld::Towns()[target].name) + L" 이사 완료 · 새 집 " +
                        std::to_wstring(npc.houseLot + 1) + L"호",
                    &destination);
            }
            npc.journeyKind = JourneyKind::None;
            npc.destinationTown = -1;
            npc.pendingLot = 0;
            npc.transitAge = 0;
            npc.migrationCooldown = 300;
            npc.activity = npc.intention = Activity::Rest;
            npc.goalActivity = Activity::Dead;
            npc.travelCommitSeconds = 0;
            npc.decisionTimer = 0;
            npc.activityGoal = npc.position;
        }
        game.dirty = true;
        return true;
    }

    bool WorldSociety::ReadNetwork(std::istream& in)
    {
        std::string marker;
        size_t count = 0;
        if (!(in >> marker >> nextLetter >> lettersDelivered >> migrationsCompleted >> networkRound >>
              nextNetworkUpdate) ||
            marker != "TOWN_NETWORK_V1" || nextLetter == 0 || nextLetter > UINT64_MAX - 1000000 ||
            !std::isfinite(nextNetworkUpdate) || nextNetworkUpdate < 0 ||
            nextNetworkUpdate > simulationSeconds + 46)
        {
            return false;
        }
        for (auto& board : knowledge)
        {
            for (auto& report : board)
            {
                if (!ReadReport(in, report, simulationSeconds))
                {
                    return false;
                }
            }
        }
        if (!(in >> count) || count > TownWorld::Count * 2)
        {
            return false;
        }
        std::set<std::uint64_t> carriers;
        std::set<int> sources;
        for (size_t i = 0; i < count; ++i)
        {
            TownLetter letter;
            if (!(in >> letter.id >> letter.carrier >> letter.source >> letter.destination >> letter.sent) ||
                letter.id == 0 || letter.id >= nextLetter || letter.source < 0 || letter.source >= 7 ||
                letter.destination < 0 || letter.destination >= 7 || letter.source == letter.destination ||
                !std::isfinite(letter.sent) || letter.sent < 0 || letter.sent > simulationSeconds + .01)
            {
                return false;
            }
            const auto* carrier = Find(letter.carrier);
            const auto from = TownWorld::Towns()[letter.source].center;
            const auto to = TownWorld::Towns()[letter.destination].center;
            if (!carrier || carrier->journeyKind != JourneyKind::Courier || carrier->mailId != letter.id ||
                carrier->destinationTown != letter.destination ||
                IndexDuringRead(carrier->village) != letter.source ||
                !carriers.insert(letter.carrier).second || !sources.insert(letter.source).second ||
                std::abs(from.x - to.x) + std::abs(from.y - to.y) != TownWorld::CenterSpacing)
            {
                return false;
            }
            for (auto& report : letter.reports)
            {
                if (!ReadReport(in, report, letter.sent))
                {
                    return false;
                }
            }
            if (!letters.emplace(letter.id, letter).second)
            {
                return false;
            }
        }
        for (const auto& pair : citizens)
        {
            const auto& npc = pair.second;
            if (npc.journeyKind == JourneyKind::Courier && !letters.count(npc.mailId))
            {
                return false;
            }
        }
        return true;
    }

    void WorldSociety::WriteNetwork(std::ostream& out) const
    {
        out << "TOWN_NETWORK_V1 " << nextLetter << ' ' << lettersDelivered << ' ' << migrationsCompleted
            << ' ' << networkRound << ' ' << nextNetworkUpdate << '\n';
        for (const auto& board : knowledge)
        {
            for (const auto& report : board)
            {
                WriteReport(out, report);
            }
        }
        out << letters.size() << '\n';
        for (const auto& pair : letters)
        {
            const auto& letter = pair.second;
            out << letter.id << ' ' << letter.carrier << ' ' << letter.source << ' ' << letter.destination
                << ' ' << letter.sent << '\n';
            for (const auto& report : letter.reports)
            {
                WriteReport(out, report);
            }
        }
    }
} // namespace Ashen
