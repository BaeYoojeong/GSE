#include "WorldSociety.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace Ashen
{
    unsigned WorldSociety::MarketVisitors(ChunkKey village, unsigned site, std::uint64_t exclude) const
    {
        unsigned count = 0;
        const Vec2 market = MarketPosition(site);
        for (const auto& pair : citizens)
        {
            const auto& other = pair.second;
            if (other.id == exclude || other.health <= 0 || other.marketDuty)
            {
                continue;
            }
            const Vec2 position = InFrame(other.village, other.position, village);
            const Vec2 goal = InFrame(other.village, other.activityGoal, village);
            const bool present = DistanceSquared(position, market) <= 80 * 80;
            const bool reserved = other.intention == Activity::Trade &&
                                  other.goalActivity == Activity::Trade &&
                                  DistanceSquared(goal, market) <= 80 * 80;
            count += present || reserved ? 1 : 0;
        }
        return count;
    }

    bool WorldSociety::HasMarketVacancy(const CitizenRecord& npc) const
    {
        return MarketVisitors(npc.village, 0, npc.id) < MarketVisitorLimit ||
               MarketVisitors(npc.village, 1, npc.id) < MarketVisitorLimit;
    }

    Vec2 WorldSociety::MarketDestination(const CitizenRecord& npc) const
    {
        if (IsMarketKeeper(npc))
        {
            unsigned rank = 0;
            for (const auto& pair : citizens)
            {
                rank += pair.first < npc.id && pair.second.village == npc.village &&
                                pair.second.profession == Profession::Merchant
                            ? 1
                            : 0;
            }
            return MarketPosition(rank % 2);
        }
        unsigned site = static_cast<unsigned>((npc.id + npc.journey) % 2);
        float best = 1e20f;
        for (unsigned candidate = 0; candidate < 2; ++candidate)
        {
            const unsigned visitors = MarketVisitors(npc.village, candidate, npc.id);
            const float cost = visitors * 600.0f +
                               std::sqrt(DistanceSquared(npc.position, MarketPosition(candidate))) * .08f;
            if (visitors < MarketVisitorLimit && cost < best)
            {
                best = cost;
                site = candidate;
            }
        }
        const Vec2 market = MarketPosition(site);
        Vec2 destination = market;
        best = 1e20f;
        // Eight spaced positions have more clearance than the old sixteen-point 38-unit ring.
        for (unsigned index = 0; index < 8; ++index)
        {
            const float angle = static_cast<float>((npc.id + index) % 8) * .785398163f;
            const Vec2 point = {market.x + std::cos(angle) * 58, market.y + std::sin(angle) * 58};
            float cost = static_cast<float>(index);
            for (const auto& pair : citizens)
            {
                const auto& other = pair.second;
                if (other.id == npc.id || other.health <= 0)
                {
                    continue;
                }
                const Vec2 position = InFrame(other.village, other.position, npc.village);
                const Vec2 goal = InFrame(other.village, other.activityGoal, npc.village);
                cost += DistanceSquared(point, position) < 38 * 38 ? 1000 : 0;
                cost += other.goalActivity == Activity::Trade && DistanceSquared(point, goal) < 38 * 38 ? 1000
                                                                                                        : 0;
            }
            if (cost < best)
            {
                best = cost;
                destination = point;
            }
        }
        return destination;
    }

    void WorldSociety::FinishMarketVisit(CitizenRecord& npc, const char* reason)
    {
        if (IsMarketKeeper(npc))
        {
            return;
        }
        npc.marketCooldown = 35 + static_cast<float>(npc.id % 25);
        npc.marketVisitSeconds = npc.marketTripSeconds = 0;
        npc.decisionTimer = npc.travelCommitSeconds = 0;
        npc.goalActivity = Activity::Dead;
        RenderProfiler::Event("market_visit_end", "npc=" + std::to_string(npc.id) + " reason=" + reason +
                                                      " cooldown_s=" + std::to_string(npc.marketCooldown));
    }

    bool WorldSociety::PurchaseItem(Game& game, std::uint64_t id, ItemId itemId)
    {
        const auto* item = FindItem(itemId);
        auto found = citizens.find(id);
        if (!item || found == citizens.end())
        {
            return false;
        }
        if (item->price <= 0)
        {
            game.Notify(L"기본 지급 무기는 판매하지 않습니다.");
            return false;
        }
        auto& merchant = found->second;
        if (merchant.profession != Profession::Merchant || merchant.health <= 0 || IsHostile(id))
        {
            game.Notify(L"지금 이 상점에서는 거래할 수 없습니다.");
            return false;
        }
        const bool resource = item->category == ItemCategory::Resource;
        if ((resource && (game.supplies >= 99 || merchant.goods <= 0)) ||
            (!resource && !game.inventory.CanAdd(itemId)))
        {
            game.Notify(resource ? L"물자 재고 또는 소지 한도를 확인하세요."
                                 : L"이미 가진 무기이거나 인벤토리 수량이 가득 찼습니다.");
            return false;
        }
        if (playerCoins < item->price || merchant.coins > 1000000 - item->price)
        {
            game.Notify(L"거래할 동전이 부족하거나 상인의 동전 보관 한도에 도달했습니다.");
            return false;
        }
        if (resource)
        {
            ++game.supplies;
            --merchant.goods;
        }
        else if (!game.inventory.Add(itemId))
        {
            return false;
        }
        playerCoins -= item->price;
        merchant.coins += item->price;
        if (item->category == ItemCategory::Weapon)
        {
            game.inventory.Equip(itemId);
            game.growth.itemsSeen |= 1u << static_cast<unsigned>(ItemKind::Weapon);
        }
        else if (itemId == ItemId::HealingPotion)
        {
            game.growth.itemsSeen |= 1u << static_cast<unsigned>(ItemKind::Healing);
        }
        ++trades;
        game.dirty = true;
        Log("player_purchase", 0, id, item->price, L"상점 구매 · " + std::wstring(item->name),
            &merchant.village);
        game.Notify(std::wstring(item->name) +
                    (item->category == ItemCategory::Weapon ? L" 구매·장착 완료" : L" +1") + L" / 동전 -" +
                    std::to_wstring(item->price));
        return true;
    }

    bool WorldSociety::SellSupplies(Game& game, std::uint64_t id)
    {
        auto found = citizens.find(id);
        if (found == citizens.end())
        {
            return false;
        }
        auto& merchant = found->second;
        if (merchant.profession != Profession::Merchant || merchant.health <= 0 || IsHostile(id) ||
            game.supplies <= 0 || merchant.goods >= 99 || merchant.coins < 2 || playerCoins > 1000000 - 2)
        {
            game.Notify(L"판매 불가 · 물자, 상인 동전 또는 보관 한도를 확인하세요.");
            return false;
        }
        --game.supplies;
        ++merchant.goods;
        playerCoins += 2;
        merchant.coins -= 2;
        ++trades;
        game.dirty = true;
        Log("player_sell", 0, id, 2, L"상점에 물자 판매", &merchant.village);
        game.Notify(L"물자 -1 / 동전 +2");
        return true;
    }
} // namespace Ashen
