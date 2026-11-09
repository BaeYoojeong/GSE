#include "WorldSociety.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Ashen
{
    namespace
    {
        constexpr float Unavailable = -1000;
        constexpr int CoinLimit = 1000000;
        constexpr float TravelMinimum = -50000;
        constexpr float TravelMaximum = 50000;

        bool Reachable(Vec2 point)
        {
            return point.x >= TravelMinimum && point.x <= TravelMaximum && point.y >= TravelMinimum &&
                   point.y <= TravelMaximum;
        }

        unsigned NearestMarketSite(Vec2 point)
        {
            return DistanceSquared(point, WorldSociety::MarketPosition(0)) <=
                           DistanceSquared(point, WorldSociety::MarketPosition(1))
                       ? 0u
                       : 1u;
        }

        constexpr float BaseWeights[4][5] = {
            // Rest, Hunt, Raid, Trade, Study. Profession biases do not remove edges.
            {4, 30, 0, 16, 8},
            {6, 16, 18, 14, 8},
            {5, 12, -6, 34, 10},
            {6, 14, -6, 14, 34}};
    } // namespace

    bool WorldSociety::IsMarketKeeper(const CitizenRecord& npc) const
    {
        if (npc.journeyKind != JourneyKind::None || npc.profession != Profession::Merchant ||
            npc.health <= npc.maximumHealth * .30f || npc.hostileSeconds > 0 || npc.reviveProtection > 0)
        {
            return false;
        }
        unsigned count = 0, rank = 0;
        for (const auto& pair : citizens)
        {
            const auto& other = pair.second;
            if (other.village == npc.village && other.journeyKind == JourneyKind::None &&
                other.profession == Profession::Merchant && other.health > other.maximumHealth * .30f &&
                other.hostileSeconds <= 0 && other.reviveProtection <= 0)
            {
                rank += other.id < npc.id ? 1 : 0;
                ++count;
            }
        }
        if (count == 0)
        {
            return false;
        }
        const auto shift = static_cast<unsigned>(std::fmod(simulationSeconds / 60, count));
        // One duty per home leaves other merchants free to visit neighboring markets.
        return rank == shift;
    }

    HuntingGround* WorldSociety::FindQuarry(CitizenRecord& npc, bool reserve)
    {
        HuntingGround* best = nullptr;
        float bestCost = 1e20f;
        for (int slot = 0; slot < HuntingSlots; ++slot)
        {
            auto it = hunting.find({npc.village, slot});
            if (it == hunting.end() || it->second.health <= 0 || it->second.placementPending)
            {
                continue;
            }
            unsigned assigned = 0;
            float cost = std::sqrt(DistanceSquared(npc.position, it->second.position));
            for (const auto& pair : citizens)
            {
                const auto& other = pair.second;
                if (other.id != npc.id && other.village == npc.village && other.health > 0 &&
                    other.intention == Activity::Hunt && other.quarrySlot == slot)
                {
                    ++assigned;
                }
            }
            if (assigned >= 2)
            {
                continue;
            }
            // Keep a live reservation throughout travel instead of turning toward a nearer spawn.
            if (npc.quarrySlot == slot)
            {
                best = &it->second;
                break;
            }
            cost += assigned * 700;
            const int preferred = static_cast<int>((npc.id + npc.huntingTrip) % HuntingSlots);
            cost -= preferred == slot ? 1600.0f : 0;
            cost += static_cast<float>((npc.id + slot * 3) % 7);
            if (cost < bestCost)
            {
                bestCost = cost;
                best = &it->second;
            }
        }
        if (reserve)
        {
            const int nextSlot = best ? best->slot : -1;
            if (best && (npc.quarrySlot != nextSlot || npc.goalActivity != Activity::Hunt))
            {
                SetDestination(npc, HuntingPosition(npc, *best), Activity::Hunt);
            }
            npc.quarrySlot = nextSlot;
        }
        return best;
    }

    CitizenRecord* WorldSociety::FindRaidTarget(const CitizenRecord& npc)
    {
        CitizenRecord* best = nullptr;
        float bestValue = -1e20f;
        const int purseSpace = CoinLimit - npc.coins;
        if (purseSpace <= 0)
        {
            return nullptr;
        }
        for (auto& pair : citizens)
        {
            auto& other = pair.second;
            if (other.id == npc.id || other.health <= 0 || other.reviveProtection > 0 || other.coins <= 0)
            {
                continue;
            }
            const Vec2 position = InFrame(other.village, other.position, npc.village);
            const float distanceSquared = DistanceSquared(npc.position, position);
            if (!Reachable(position) || distanceSquared > 2600 * 2600)
            {
                continue;
            }
            // Follow a viable traveler across the activity area; do not lose them at a 400-unit gate.
            if (npc.target == other.id)
            {
                return &other;
            }
            const bool traveler = !(Locate(other.village, other.position).chunk == other.village);
            const float value = static_cast<float>((std::min)(other.coins, purseSpace)) -
                                std::sqrt(distanceSquared) * .08f - other.raidWeight * 10 +
                                (traveler ? 35.0f : 0);
            if (value > bestValue)
            {
                best = &other;
                bestValue = value;
            }
        }
        return best;
    }

    CitizenRecord* WorldSociety::FindTradePartner(const CitizenRecord& npc, bool& buying, bool requireMarket)
    {
        if (npc.health <= 0 || npc.hostileSeconds > 0 || (requireMarket && !IsAtMarket(npc)))
        {
            return nullptr;
        }
        CitizenRecord* best = nullptr;
        float bestCost = 1e20f;
        bool bestBuying = false;
        const Location location = Locate(npc.village, npc.position);
        for (auto& pair : citizens)
        {
            auto& other = pair.second;
            if (other.id == npc.id || other.health <= 0 || other.hostileSeconds > 0 ||
                other.activity == Activity::Raid)
            {
                continue;
            }
            const Vec2 position = InFrame(other.village, other.position, npc.village);
            const float distanceSquared = DistanceSquared(npc.position, position);
            if (!Reachable(position) || distanceSquared > 2600 * 2600)
            {
                continue;
            }
            if (requireMarket)
            {
                const Location otherLocation = Locate(other.village, other.position);
                if (!IsAtMarket(other) || !(location.chunk == otherLocation.chunk) ||
                    distanceSquared > 130 * 130 ||
                    NearestMarketSite(location.local) != NearestMarketSite(otherLocation.local))
                {
                    continue;
                }
            }
            const bool merchant = npc.profession == Profession::Merchant;
            if (!merchant && other.profession != Profession::Merchant)
            {
                continue;
            }
            const int sellPrice = merchant ? 3 : 2;
            const int buyPrice = merchant ? 2 : 3;
            const bool canSell = npc.goods > (merchant ? 0 : 1) && other.goods < (merchant ? 2 : 16) &&
                                 other.coins >= sellPrice && npc.coins <= CoinLimit - sellPrice;
            const bool canBuy = npc.goods < (merchant ? 16 : 2) && other.goods > (merchant ? 2 : 0) &&
                                npc.coins >= buyPrice && other.coins <= CoinLimit - buyPrice;
            if (!canSell && !canBuy)
            {
                continue;
            }
            // Physical proximity determines partners, including visitors from another village.
            float cost = std::sqrt(distanceSquared);
            cost -= other.profession == Profession::Merchant ? 70.0f : 0;
            cost -= IsAtMarket(other) ? 40.0f : 0;
            if (cost < bestCost)
            {
                best = &other;
                bestCost = cost;
                bestBuying = !canSell;
            }
        }
        if (best)
        {
            buying = bestBuying;
        }
        return best;
    }

    void WorldSociety::SelectActivity(CitizenRecord& npc, bool playerCanBeRobbed)
    {
        const float injury = 1 - npc.health / npc.maximumHealth;
        const float poverty = 1 - (std::min)(1.0f, npc.coins / 40.0f);
        const float shortage = 1 - (std::min)(1.0f, npc.goods / 6.0f);
        auto& score = npc.decisionScores;
        for (unsigned i = 0; i < score.size(); ++i)
        {
            score[i] = BaseWeights[static_cast<unsigned>(npc.profession)][i];
        }
        score[0] += injury * 120 - (injury < .05f ? 40 : 0);
        score[1] += poverty * 25 + shortage * 30 - injury * 25;
        score[2] += npc.raidWeight * 95 - 35 + poverty * 12 - injury * 30;
        score[3] += (std::min)(24.0f, static_cast<float>(npc.goods) * 3) + poverty * 8;
        score[4] += npc.goods > 0 ? 10 : 0;

        if (npc.ambition == Ambition::Vitality)
        {
            score[0] += 14;
            score[1] += 10;
            score[4] += 12;
        }
        else if (npc.ambition == Ambition::Wealth)
        {
            score[1] += 10;
            score[2] += 8;
            score[3] += 16;
        }
        else
        {
            score[1] += 4;
            score[4] += 20;
        }

        bool buying = false;
        const bool canTrade = FindTradePartner(npc, buying, false) != nullptr;
        HuntingGround* quarry = FindQuarry(npc, false);
        if (!quarry || npc.goods >= 98)
        {
            score[1] = Unavailable;
        }
        if (npc.raidCooldown > 0 || npc.coins >= 1000000 ||
            (!playerCanBeRobbed && !FindRaidTarget(npc) && npc.raidWeight < .6f))
        {
            score[2] = Unavailable;
        }
        const bool marketKeeper = IsMarketKeeper(npc);
        if (!canTrade && !marketKeeper)
        {
            score[3] = Unavailable;
        }
        if (marketKeeper)
        {
            // Waiting for customers is a valid job, even when no NPC can exchange right now.
            score[3] = (std::max)(score[3], 110.0f);
        }
        if (npc.actionTimer > 0)
        {
            score[3] -= 18;
            score[4] -= 18;
        }

        // A small deterministic preference variation plus boredom opens cross-job edges.
        // Hysteresis prevents a new winner on every 10 Hz simulation tick.
        const unsigned previous = static_cast<unsigned>(npc.intention);
        for (unsigned i = 0; i < score.size(); ++i)
        {
            if (score[i] <= Unavailable)
            {
                continue;
            }
            const auto phase =
                (npc.id % 17 + static_cast<unsigned>(std::fmod(simulationSeconds / 8, 17.0)) + i * 7) % 17;
            score[i] += static_cast<float>(phase) * .5f - 4;
            if (i == previous)
            {
                score[i] += 6 - (std::min)(30.0f, (std::max)(0.0f, npc.intentionAge - 8) * 2);
            }
        }
        if (injury > .70f || npc.reviveProtection > 0)
        {
            score[0] = 300;
        }
        else if (npc.intention == Activity::Rest && injury > .15f)
        {
            score[0] += 20;
        }
        unsigned winner = 0;
        for (unsigned i = 1; i < score.size(); ++i)
        {
            if (score[i] > score[winner])
            {
                winner = i;
            }
        }
        // A trip is work in progress. Re-evaluate emergencies, but finish a valid destination
        // before ordinary score changes send the resident back toward town.
        const bool destinationValid =
            previous == static_cast<unsigned>(Activity::Trade) || score[previous] > Unavailable;
        if (npc.travelCommitSeconds > 0 && npc.goalActivity == npc.intention && destinationValid &&
            DistanceSquared(npc.position, npc.activityGoal) > 18 * 18 && injury < .45f &&
            npc.reviveProtection <= 0)
        {
            winner = previous;
        }
        const Activity next = static_cast<Activity>(winner);
        if (next != npc.intention)
        {
            std::ostringstream detail;
            detail << "npc=" << npc.id << " from=" << previous << " to=" << winner << " hp=" << npc.health
                   << " coins=" << npc.coins << " goods=" << npc.goods << " raid_weight=" << npc.raidWeight
                   << " scores=";
            for (float value : score)
            {
                detail << value << '/';
            }
            RenderProfiler::Event("society_transition", detail.str());
            npc.intention = next;
            npc.travelCommitSeconds = 0;
            npc.intentionAge = 0;
            npc.target = 0;
            if (next != Activity::Hunt)
            {
                npc.quarrySlot = -1;
            }
        }
        if (next == Activity::Hunt)
        {
            FindQuarry(npc);
        }
        else
        {
            npc.quarrySlot = -1;
        }
        npc.decisionTimer = 1.25f + static_cast<float>(npc.id % 4) * .1f;
    }
} // namespace Ashen
