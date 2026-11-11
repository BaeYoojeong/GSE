#include "WorldSociety.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace Ashen
{
    namespace
    {
        constexpr int CoinLimit = 1000000;
        constexpr float TickSeconds = .1f;

        bool Near(Vec2 a, Vec2 b, float radius)
        {
            return DistanceSquared(a, b) <= radius * radius;
        }

    } // namespace

    bool WorldSociety::IsFoundingVillage(ChunkKey chunk)
    {
        return TownWorld::Index(chunk) >= 0;
    }

    const wchar_t* WorldSociety::JobName(Profession job)
    {
        const wchar_t* names[] = {L"사냥꾼", L"약탈자", L"상인", L"구도자"};
        return names[static_cast<unsigned>(job) % 4];
    }

    const wchar_t* WorldSociety::GoalName(Ambition goal)
    {
        const wchar_t* names[] = {L"강한 몸", L"재산 축적", L"깨달음"};
        return names[static_cast<unsigned>(goal) % 3];
    }

    const wchar_t* WorldSociety::ActivityName(Activity activity)
    {
        const wchar_t* names[] = {L"회복", L"사냥", L"약탈", L"거래", L"수련", L"이동", L"사망"};
        return names[static_cast<unsigned>(activity) % 7];
    }

    void WorldSociety::EnsureGrounds(ChunkKey village)
    {
        for (int slot = 0; slot < HuntingSlots; ++slot)
        {
            HuntingGround ground;
            ground.village = village;
            ground.slot = slot;
            ground.position = QuarryPosition(slot);
            hunting.emplace(std::make_pair(village, slot), ground);
        }
    }

    void WorldSociety::Initialize(const std::map<std::uint64_t, Memory>& memories)
    {
        ExpandTowns();


        for (const auto& pair : memories)
        {
            AddHeir(pair.second);
        }

        for (auto& pair : citizens)
        {
            EnsureGrounds(pair.second.village);
            if (pair.second.health <= 0)
            {
                Revive(pair.second, "load_revive");
            }
        }
        for (auto& pair : hunting)
        {
            auto& ground = pair.second;
            ground.respawnSeconds = (std::min)(ground.respawnSeconds, QuarryRespawnSeconds);
            ground.spawnVariant =
                static_cast<unsigned>((static_cast<std::uint64_t>(ground.village.x) * 3 +
                                       static_cast<std::uint64_t>(ground.village.y) * 5 + ground.slot) %
                                      8);
            ground.position = QuarryPosition(ground.slot, ground.spawnVariant);
        }
    }

    void WorldSociety::AddHeir(const Memory& memory)
    {
        if (Find(18 + memory.life.id))
        {
            return;
        }
        CitizenRecord npc;
        npc.id = 18 + memory.life.id;
        npc.originLife = memory.life.id;
        npc.village = TownWorld::Towns()[TownWorld::Nearest(memory.home.chunk)].center;
        npc.houseLot = VacantLot(npc.village);
        // Birth records keep the death site; the new person's settlement home is the safe road.
        npc.position = ActivityPosition(npc, Activity::Rest);
        npc.profession = memory.nature == Nature::Guardian ? Profession::Hunter
                         : memory.nature == Nature::Giver  ? Profession::Merchant
                                                           : Profession::Seeker;
        npc.ambition = memory.nature == Nature::Guardian ? Ambition::Vitality : Ambition::Insight;
        npc.raidWeight = memory.emotion == Emotion::Anger ? .72f : .08f;
        if (npc.raidWeight >= .6f)
        {
            npc.ambition = Ambition::Wealth;
        }
        npc.lastGiftLife = memory.lastGiftLife;
        npc.lastTalkLife = memory.lastTalkLife;
        npc.coins = 0;
        npc.goods = 0;
        citizens.emplace(npc.id, npc);
        EnsureGrounds(npc.village);
    }

    const CitizenRecord* WorldSociety::Find(std::uint64_t id) const
    {
        const auto it = citizens.find(id);
        return it == citizens.end() ? nullptr : &it->second;
    }

    const HuntingGround* WorldSociety::Quarry(ChunkKey village, int slot) const
    {
        const auto it = hunting.find({village, slot});
        return it == hunting.end() ? nullptr : &it->second;
    }

    bool WorldSociety::IsHostile(std::uint64_t id) const
    {
        const auto* npc = Find(id);
        return npc && npc->health > 0 && npc->reviveProtection <= 0 &&
               (npc->hostileSeconds > 0 || npc->activity == Activity::Raid);
    }

    void WorldSociety::Log(const char* event, std::uint64_t actor, std::uint64_t target, int amount,
                           const std::wstring& text, const ChunkKey* village)
    {
        RecordEvent(event, actor, target, amount, text, village);
        recentEvents.push_front(text);

        while (recentEvents.size() > 4)
        {
            recentEvents.pop_back();
        }
    }

    bool WorldSociety::Exchange(CitizenRecord& seller, CitizenRecord& buyer, int price)
    {
        if (!IsAtMarket(seller) || !IsAtMarket(buyer) ||
            !Near(seller.position, InFrame(buyer.village, buyer.position, seller.village), 130) ||
            seller.health <= 0 || buyer.health <= 0 || seller.goods < 1 || buyer.goods >= 99 ||
            buyer.coins < price || seller.coins > CoinLimit - price)
        {
            return false;
        }

        --seller.goods;
        ++buyer.goods;
        seller.coins += price;
        buyer.coins -= price;
        ++trades;
        const ChunkKey market = Locate(seller.village, seller.position).chunk;
        Log("trade", seller.id, buyer.id, price,
            L"주민 #" + std::to_wstring(seller.id) + L" → #" + std::to_wstring(buyer.id) + L" 물자 거래",
            &market);
        return true;
    }

    void WorldSociety::Revive(CitizenRecord& npc, const char* reason)
    {
        CancelMission(npc);
        npc.health = npc.maximumHealth;
        npc.position = ActivityPosition(npc, Activity::Rest);
        npc.activityGoal = npc.position;
        npc.goalActivity = Activity::Rest;
        npc.goalDwellSeconds = 8;
        npc.travelCommitSeconds = 0;
        npc.marketDuty = false;
        npc.marketCooldown = 12 + static_cast<float>(npc.id % 15);
        npc.marketVisitSeconds = 0;
        npc.activity = npc.intention = Activity::Rest;
        npc.hostileSeconds = npc.attackTimer = npc.actionTimer = 0;
        npc.attackVisualSeconds = npc.hitVisualSeconds = 0;
        npc.decisionTimer = npc.intentionAge = 0;
        npc.raidCooldown = 3;
        npc.reviveProtection = ReviveProtectionSeconds;
        npc.quarrySlot = -1;
        npc.target = 0;
        if (npc.deaths < UINT64_MAX)
        {
            ++npc.deaths;
        }
        Log(reason, npc.id, 0, 0, L"주민 #" + std::to_wstring(npc.id) + L" 마을에서 즉시 부활");
    }

    void WorldSociety::Kill(Game& game, CitizenRecord& victim, CitizenRecord* killer, bool player,
                            std::uint64_t attacker)
    {
        // Transfer only the remaining holdings. Revival never grants a new purse or inventory.
        victim.health = 0;
        victim.activity = Activity::Dead;
        const std::uint64_t attackerId = attacker ? attacker : killer ? killer->id : 0;
        const bool murder = player || killer || attackerId;
        const std::wstring attackerName = player ? L"여행자" : L"주민 #" + std::to_wstring(attackerId);
        const std::wstring deathMessage =
            murder ? attackerName + L" → 주민 #" + std::to_wstring(victim.id) + L" 살인 발생"
                   : L"주민 #" + std::to_wstring(victim.id) + L" 사망";
        const ChunkKey incidentVillage = Locate(victim.village, victim.position).chunk;
        unsigned witnesses = 0;
        if (murder)
        {
            for (const auto& pair : citizens)
            {
                const auto& observer = pair.second;
                if (observer.id != victim.id && observer.id != attackerId && observer.health > 0 &&
                    Near(InFrame(observer.village, observer.position, victim.village), victim.position, 180))
                {
                    ++witnesses;
                }
            }
        }
        const std::uint64_t deathEvent = RecordEvent(murder ? "murder" : "death", attackerId, victim.id, 0,
                                                     deathMessage, &incidentVillage, witnesses);
        if (murder && witnesses > 0)
        {
            RecordEvent("murder_rumor", attackerId, victim.id, 0,
                        L"살인 소문 · " + attackerName + L"가 주민 #" + std::to_wstring(victim.id) +
                            L"를 죽였다는 이야기가 목격자 " + std::to_wstring(witnesses) +
                            L"명에게서 퍼집니다.",
                        &incidentVillage, witnesses, deathEvent);
        }
        if (player || (killer && killer->health > 0))
        {
            int& purse = player ? playerCoins : killer->coins;
            int& goods = player ? game.supplies : killer->goods;
            const int money = (std::min)(victim.coins, CoinLimit - purse);
            const int cargo = (std::min)(victim.goods, 99 - goods);
            purse += money;
            goods += cargo;
            victim.coins -= money;
            victim.goods -= cargo;
            Log("death_loot", player ? 0 : killer->id, victim.id, money,
                L"주민 #" + std::to_wstring(victim.id) + L" 사망 · 재산 이전");
        }
        Revive(victim, "revive");
        SyncActors(game);
        for (Actor* base : game.scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);
            if (actor && actor->kind == Kind::Citizen && actor->memory == victim.id)
            {
                SyncActor(*actor);
                actor->flash = .6f;
                break;
            }
        }
        game.dirty = true;
    }

    float WorldSociety::CitizenDamage(const CitizenRecord& npc)
    {
        return (npc.profession == Profession::Hunter ? 30.0f : 22.0f) + (std::min)(8.0f, npc.insight / 25.0f);
    }

    float WorldSociety::CitizenCooldown(const CitizenRecord& npc)
    {
        return npc.profession == Profession::Hunter ? .8f : 1.1f;
    }

    void WorldSociety::ShowAttack(CitizenRecord& npc, Vec2 target)
    {
        npc.attackVisualSeconds = AttackVisualDuration;
        npc.attackVisualTarget = target;
        ++attacksSinceReport;
    }

    bool WorldSociety::DefendAgainstMonsters(Game& game, CitizenRecord& npc)
    {
        if (npc.health < npc.maximumHealth * .30f || npc.reviveProtection > 0 || npc.hostileSeconds > 0 ||
            game.chunks.count(Locate(npc.village, npc.position).chunk) == 0)
        {
            return false;
        }
        const Vec2 world = game.Relative({npc.village, npc.position});
        const float range = npc.intention == Activity::Hunt
                                ? (npc.profession == Profession::Hunter ? 165.0f : 100.0f)
                                : 65.0f;
        float best = range * range;
        WorldActor* target = nullptr;
        for (WorldActor* enemy : nearbyEnemies)
        {
            if (!enemy->IsActive() || enemy->health <= 0)
            {
                continue;
            }
            const float distance = DistanceSquared(world, enemy->WorldPosition());
            if (distance <= best && game.ClearPath(world, enemy->WorldPosition()))
            {
                target = enemy;
                best = distance;
            }
        }
        if (!target)
        {
            return false;
        }
        // Brief self-defense must not erase an expedition or a pursued resident.
        npc.activity = Activity::Hunt;
        if (npc.attackTimer <= 0)
        {
            const Vec2 point = target->WorldPosition();
            ShowAttack(npc, {npc.position.x + point.x - world.x, npc.position.y + point.y - world.y});
            npc.attackTimer = CitizenCooldown(npc);
            target->health = (std::max)(0.0f, target->health - CitizenDamage(npc));
            target->flash = .35f;
            if (target->health <= 0)
            {
                npc.goods = (std::min)(99, npc.goods + 2);
                npc.coins = (std::min)(CoinLimit, npc.coins + 3);
                npc.insight = (std::min)(10000, npc.insight + 1);
                ++hunts;
                ++monsterKillsSinceReport;
                game.Defeat(*target, false);
                Log("hunt", npc.id, 0, 2, L"주민 #" + std::to_wstring(npc.id) + L" 야생 괴물 처치 · 물자 +2");
            }
        }
        return true;
    }

    void WorldSociety::Think(Game& game, CitizenRecord& npc, float dt)
    {
        if (npc.health <= 0)
        {
            Kill(game, npc, nullptr, false);
            return;
        }

        npc.attackVisualSeconds = (std::max)(0.0f, npc.attackVisualSeconds - dt);
        npc.hitVisualSeconds = (std::max)(0.0f, npc.hitVisualSeconds - dt);
        npc.raidCooldown = (std::max)(0.0f, npc.raidCooldown - dt);
        npc.raidResultSeconds = (std::max)(0.0f, npc.raidResultSeconds - dt);
        npc.travelCommitSeconds = (std::max)(0.0f, npc.travelCommitSeconds - dt);
        npc.reviveProtection = (std::max)(0.0f, npc.reviveProtection - dt);
        npc.decisionTimer = (std::max)(0.0f, npc.decisionTimer - dt);
        npc.marketCooldown = (std::max)(0.0f, npc.marketCooldown - dt);
        if (npc.activity == npc.intention)
        {
            npc.intentionAge = (std::min)(60.0f, npc.intentionAge + dt);
        }
        npc.attackTimer = (std::max)(0.0f, npc.attackTimer - dt);
        npc.actionTimer = (std::max)(0.0f, npc.actionTimer - dt);
        npc.hostileSeconds = (std::max)(0.0f, npc.hostileSeconds - dt);
        const Location player = game.Absolute(game.hero->position);
        const Vec2 playerPoint = InFrame(player.chunk, player.local, npc.village);
        const bool playerNearby = !game.Dead() && Near(npc.position, playerPoint, 340);
        const Memory* origin = npc.originLife ? game.MemoryAt(npc.originLife) : nullptr;
        if (origin && playerNearby && npc.hostileSeconds <= 0 && npc.raidWeight < .6f)
        {
            if (origin->nature == Nature::Giver && origin->bond > 0 && npc.attackTimer <= 0 &&
                game.health < game.MaxHealth() && Near(npc.position, playerPoint, 100))
            {
                game.health = (std::min)(game.MaxHealth(), game.health + 8);
                npc.attackTimer = 12;
                game.Notify(L"전승자가 상처를 돌봅니다 · 체력 +8");
            }
        }

        npc.migrationCooldown = (std::max)(0.0f, npc.migrationCooldown - dt);
        if (DefendAgainstMonsters(game, npc))
        {
            return;
        }

        // Retaliation persists independently of the player's PK toggle.
        if (playerNearby && npc.hostileSeconds > 0 && npc.reviveProtection <= 0 &&
            npc.health > npc.maximumHealth * .30f && Near(npc.position, playerPoint, 220))
        {
            npc.activity = Activity::Raid;
            const Vec2 world = game.Relative({npc.village, npc.position});

            if (Near(npc.position, playerPoint, 48) && npc.attackTimer <= 0 &&
                game.ClearPath(world, game.hero->position))
            {
                npc.attackTimer = CitizenCooldown(npc);
                ShowAttack(npc, playerPoint);

                if (game.immunity <= 0)
                {
                    game.health = (std::max)(0.0f, game.health - 18);
                    game.immunity = .65f;
                    game.hero->flash = .3f;
                }
            }
            else if (game.ClearPath(world, game.hero->position))
            {
                const float length = std::sqrt(DistanceSquared(npc.position, playerPoint));
                if (length > 35)
                {
                    MoveCitizen(game, npc,
                                {(playerPoint.x - npc.position.x) / length * dt * 80,
                                 (playerPoint.y - npc.position.y) / length * dt * 80});
                }
            }

            return;
        }

        if (TravelMission(game, npc, dt))
        {
            return;
        }
        const auto* trading = dynamic_cast<const WorldActor*>(game.scene.Find(game.tradingMerchant));
        if (trading && trading->kind == Kind::Citizen && trading->memory == npc.id &&
            game.Shopkeeper(game.tradingMerchant) == npc.id)
        {
            npc.activity = Activity::Trade;
            return;
        }
        const bool duty = IsMarketKeeper(npc);
        if (duty != npc.marketDuty)
        {
            npc.marketDuty = duty;
            npc.travelCommitSeconds = 0;
            npc.goalActivity = Activity::Dead;
            npc.decisionTimer = 0;
        }
        if (!duty && npc.intention == Activity::Trade)
        {
            npc.marketTripSeconds += dt;
            npc.marketVisitSeconds = IsAtMarket(npc) ? npc.marketVisitSeconds + dt : 0;
            if (npc.marketVisitSeconds >= 8 + static_cast<float>(npc.id % 4) || npc.marketTripSeconds >= 45)
            {
                FinishMarketVisit(npc, "timeout");
            }
        }
        else
        {
            npc.marketVisitSeconds = npc.marketTripSeconds = 0;
        }
        if (Near(npc.position, npc.activityGoal, 18))
        {
            if (npc.travelCommitSeconds > 0)
            {
                ++npc.destinationsReached;
                npc.travelCommitSeconds = 0;
                RenderProfiler::Event("society_arrival", "npc=" + std::to_string(npc.id) +
                                                             " journey=" + std::to_string(npc.journey));
            }
            npc.goalDwellSeconds = (std::max)(0.0f, npc.goalDwellSeconds - dt);
        }
        if (npc.decisionTimer <= 0 || npc.health < npc.maximumHealth * .30f)
        {
            SelectActivity(
                npc, playerNearby && playerCoins > 0 && Near(npc.position, playerPoint, 70) &&
                         game.ClearPath(game.Relative({npc.village, npc.position}), game.hero->position));
        }

        const bool canLeave = npc.intention != Activity::Rest || npc.health >= npc.maximumHealth * .9f;
        if (npc.intention != Activity::Hunt &&
            (npc.goalActivity != npc.intention ||
             (canLeave && npc.goalDwellSeconds <= 0 && Near(npc.position, npc.activityGoal, 18))))
        {
            SetDestination(npc, ActivityPosition(npc, npc.intention), npc.intention);
        }

        if (npc.intention == Activity::Rest)
        {
            npc.activity = Activity::Rest;
            if (Travel(game, npc, npc.activityGoal, dt))
            {
                npc.health = (std::min)(npc.maximumHealth, npc.health + dt * 7);
            }
            else
            {
                npc.activity = Activity::Return;
            }
            return;
        }

        if (npc.intention == Activity::Raid)
        {
            CitizenRecord* victim = FindRaidTarget(npc);
            if (npc.raidCooldown > 0 || npc.reviveProtection > 0)
            {
                npc.decisionTimer = 0;
                return;
            }
            npc.activity = Activity::Raid;
            if (playerNearby && playerCoins > 0 && Near(npc.position, playerPoint, 70) &&
                game.ClearPath(game.Relative({npc.village, npc.position}), game.hero->position))
            {
                if (npc.attackTimer <= 0)
                {
                    const int stolen = (std::min)({3, playerCoins, CoinLimit - npc.coins});
                    playerCoins -= stolen;
                    npc.coins += stolen;
                    npc.hostileSeconds = 10;
                    npc.raidCooldown = 12;
                    npc.travelCommitSeconds = 0;
                    npc.attackTimer = CitizenCooldown(npc);
                    ShowAttack(npc, playerPoint);
                    npc.decisionTimer = 0;
                    RecordRaid(npc, 0, stolen);
                    const ChunkKey raidSite = Locate(npc.village, npc.position).chunk;
                    Log("rob_player", npc.id, 0, stolen, L"약탈자가 여행자의 동전을 빼앗았습니다.",
                        &raidSite);
                    game.Notify(L"약탈당했습니다. PK OFF 상태에서도 적대 주민에게 반격할 수 있습니다.");
                }
                return;
            }
            if (!victim)
            {
                if (npc.target)
                {
                    SetDestination(npc, ActivityPosition(npc, Activity::Raid), Activity::Raid);
                }
                npc.target = 0;
                Travel(game, npc, npc.activityGoal, dt);
                return;
            }
            const bool changedTarget = npc.target != victim->id;
            npc.target = victim->id;
            const Vec2 victimPoint = InFrame(victim->village, victim->position, npc.village);
            if (changedTarget)
            {
                SetDestination(npc, victimPoint, Activity::Raid);
                RenderProfiler::Event("raid_pursuit", "npc=" + std::to_string(npc.id) +
                                                          " target=" + std::to_string(victim->id));
            }
            npc.activityGoal = victimPoint;
            if (!Near(npc.position, victimPoint, 38))
            {
                Travel(game, npc, victimPoint, dt);
                return;
            }
            if (npc.attackTimer > 0 || (game.chunks.count(Locate(npc.village, npc.position).chunk) &&
                                        !game.ClearPath(game.Relative({npc.village, npc.position}),
                                                        game.Relative({victim->village, victim->position}))))
            {
                return;
            }
            npc.attackTimer = CitizenCooldown(npc);
            ShowAttack(npc, victimPoint);
            victim->hitVisualSeconds = .35f;
            const float retaliation = victim->attackTimer <= 0 ? CitizenDamage(*victim) * .6f : 0;
            if (retaliation > 0)
            {
                npc.hitVisualSeconds = .25f;
                ShowAttack(*victim, InFrame(npc.village, npc.position, victim->village));
                victim->attackTimer = CitizenCooldown(*victim);
            }
            const int stolen = (std::min)({6, victim->coins, CoinLimit - npc.coins});
            victim->coins -= stolen;
            npc.coins += stolen;
            victim->health = (std::max)(0.0f, victim->health - CitizenDamage(npc));
            npc.health = (std::max)(0.0f, npc.health - retaliation);
            npc.raidCooldown = 12;
            npc.travelCommitSeconds = 0;
            npc.decisionTimer = 0;
            npc.activity = Activity::Return;
            RecordRaid(npc, victim->id, stolen);
            const ChunkKey raidSite = Locate(npc.village, npc.position).chunk;
            Log("raid", npc.id, victim->id, stolen, L"주민 사이 약탈 · 동전 이전", &raidSite);
            const bool victimDied = victim->health <= 0;
            const bool attackerDied = npc.health <= 0;
            // Capture both deaths before either revival restores health.
            if (victimDied)
            {
                Kill(game, *victim, attackerDied ? nullptr : &npc, false, npc.id);
            }
            if (attackerDied)
            {
                Kill(game, npc, victimDied ? nullptr : victim, false, victim->id);
            }
            return;
        }

        if (npc.intention == Activity::Hunt)
        {
            npc.activity = Activity::Hunt;
            HuntingGround* ground = FindQuarry(npc);
            if (!ground)
            {
                // Respect occupied targets and respawn cooldowns; try another activity.
                npc.decisionTimer = 0;
                return;
            }
            const Vec2 goal = HuntingPosition(npc, *ground);
            npc.activityGoal = goal;
            if (!Travel(game, npc, goal, dt) || !Near(npc.position, ground->position, 95))
            {
                npc.activity = Activity::Return;
                return;
            }
            if (game.chunks.count(Locate(npc.village, npc.position).chunk) &&
                !game.ClearPath(game.Relative({npc.village, npc.position}),
                                game.Relative({npc.village, ground->position})))
            {
                return;
            }
            if (npc.attackTimer <= 0)
            {
                npc.attackTimer = CitizenCooldown(npc);
                ShowAttack(npc, ground->position);
                ground->hitVisualSeconds = .35f;
                ground->health = (std::max)(0.0f, ground->health - CitizenDamage(npc));
                if (ground->health <= 0)
                {
                    npc.goods = (std::min)(99, npc.goods + 2);
                    npc.coins = (std::min)(CoinLimit, npc.coins + 3);
                    npc.insight = (std::min)(10000, npc.insight + 1);
                    ground->respawnSeconds = QuarryRespawnSeconds;
                    ++npc.huntingTrip;
                    npc.travelCommitSeconds = 0;
                    npc.quarrySlot = -1;
                    npc.decisionTimer = 0;
                    ++hunts;
                    ++monsterKillsSinceReport;
                    Log("hunt", npc.id, static_cast<std::uint64_t>(ground->slot), 2,
                        L"주민 #" + std::to_wstring(npc.id) + L" 사냥 성공 · 물자 +2");
                }
            }
            if (ground->health > 0 && ground->attackTimer <= 0 && npc.reviveProtection <= 0)
            {
                ground->attackTimer = 1.6f;
                npc.health = (std::max)(0.0f, npc.health - 5);
                npc.hitVisualSeconds = .3f;
                if (npc.health <= 0)
                {
                    Kill(game, npc, nullptr, false);
                }
            }
            return;
        }

        if (!Travel(game, npc, npc.activityGoal, dt))
        {
            npc.activity = Activity::Return;
            return;
        }
        npc.activity = npc.intention;
        if (npc.actionTimer > 0)
        {
            return;
        }
        npc.actionTimer = 6;
        if (npc.intention == Activity::Trade)
        {
            bool buying = false;
            CitizenRecord* partner = FindTradePartner(npc, buying);
            if (partner)
            {
                const bool merchant = npc.profession == Profession::Merchant;
                if (Exchange(buying ? *partner : npc, buying ? npc : *partner,
                             buying ? (merchant ? 2 : 3) : (merchant ? 3 : 2)))
                {
                    FinishMarketVisit(npc, "traded");
                    FinishMarketVisit(*partner, "traded");
                }
            }
            npc.decisionTimer = 0;
            return;
        }
        // Study is available to every profession and consumes supplies to support vitality.
        if (npc.goods > 0)
        {
            --npc.goods;
            npc.health = (std::min)(npc.maximumHealth, npc.health + 10);
            if (npc.ambition == Ambition::Vitality && npc.maximumHealth < 150)
            {
                npc.maximumHealth = (std::min)(150.0f, npc.maximumHealth + 1);
                npc.health = (std::min)(npc.maximumHealth, npc.health + 1);
            }
        }
        npc.insight = (std::min)(10000, npc.insight + 1);
        if (npc.insight % 5 == 0)
        {
            Log("insight", npc.id, 0, npc.insight,
                L"주민 #" + std::to_wstring(npc.id) + L" 깨달음 " + std::to_wstring(npc.insight));
        }
    }

    void WorldSociety::Tick(Game& game, float dt)
    {
        simulationSeconds += dt;
        NetworkTick();
        for (auto& pair : citizens)
        {
            pair.second.motionOrigin = pair.second.position;
        }
        // Snapshot the loaded enemy set once per simulation tick, not once per resident.
        nearbyEnemies.clear();
        for (Actor* base : game.scene.Snapshot())
        {
            auto* actor = dynamic_cast<WorldActor*>(base);
            if (actor && actor->kind == Kind::Enemy && actor->health > 0 && actor->IsActive())
            {
                nearbyEnemies.push_back(actor);
            }
        }
        for (auto& pair : hunting)
        {
            auto& ground = pair.second;
            ground.attackTimer = (std::max)(0.0f, ground.attackTimer - dt);
            ground.hitVisualSeconds = (std::max)(0.0f, ground.hitVisualSeconds - dt);
            if (ground.health <= 0)
            {
                ground.respawnSeconds = (std::max)(0.0f, ground.respawnSeconds - dt);
                if (ground.respawnSeconds <= 0)
                {
                    PlaceQuarry(game, ground, true);
                }
            }
            else if (ground.placementPending)
            {
                PlaceQuarry(game, ground, false);
            }
            else if (!game.Dead())
            {
                const Location player = game.Absolute(game.hero->position);
                if (ground.attackTimer <= 0 &&
                    Near(InFrame(player.chunk, player.local, ground.village), ground.position, 55) &&
                    game.ClearPath(game.hero->position, game.Relative({ground.village, ground.position})))
                {
                    ground.attackTimer = 1.6f;
                    if (game.immunity <= 0)
                    {
                        game.health = (std::max)(0.0f, game.health - 8);
                        game.immunity = .65f;
                        game.hero->flash = .2f;
                    }
                }
            }
        }

        for (auto& pair : citizens)
        {
            auto& npc = pair.second;
            npc.simulationDebt += dt;
            const bool nearby = game.chunks.count(Locate(npc.village, npc.position).chunk) != 0;
            if (!nearby && game.farSimulation && npc.simulationDebt <= 2.5f)
            {
                continue;
            }
            if (nearby || npc.simulationDebt >= 1.0f)
            {
                const float step = nearby ? npc.simulationDebt : (std::min)(1.0f, npc.simulationDebt);
                npc.simulationDebt -= step;
                Think(game, npc, step);
                nearby ? ++nearSteps : ++farSteps;
            }
        }
        nearbyEnemies.clear();
        ResolveCrowding(game, dt);
        for (auto& pair : citizens)
        {
            auto& npc = pair.second;
            const Vec2 delta = {npc.position.x - npc.motionOrigin.x, npc.position.y - npc.motionOrigin.y};
            npc.motionVelocity =
                DistanceSquared(delta, {}) < 200 * 200 * dt * dt ? Vec2{delta.x / dt, delta.y / dt} : Vec2{};
        }
        game.dirty = true;
    }

    void WorldSociety::Update(Game& game, float seconds)
    {
        const auto started = std::chrono::steady_clock::now();
        const bool workerAdvanced = game.farSimulation && game.farSimulation->Poll(game);
        if (seconds > .5f)
        {
            RenderProfiler::Event("society_time_cap", "discarded_seconds=" + std::to_string(seconds - .5f));
        }
        seconds = (std::clamp)(seconds, 0.0f, .5f);
        accumulator += seconds;
        const bool advanced = accumulator >= TickSeconds;
        while (accumulator >= TickSeconds)
        {
            Tick(game, TickSeconds);
            accumulator -= TickSeconds;
        }
        if (advanced || workerAdvanced)
        {
            SyncActors(game);
        }
        if (game.farSimulation)
        {
            game.farSimulation->Submit(game);
        }
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        updateMilliseconds += milliseconds;
        peakUpdateMilliseconds = (std::max)(peakUpdateMilliseconds, milliseconds);
        reportTimer += seconds;
        if (reportTimer >= 10)
        {
            reportTimer = 0;
            ReportSpacing(game);
            std::ostringstream report;
            report << "near_steps=" << nearSteps << " far_steps=" << farSteps << " letters=" << letters.size()
                   << " delivered=" << lettersDelivered << " migrations=" << migrationsCompleted
                   << " sim_s=" << simulationSeconds << " residents=" << citizens.size() << " hunts=" << hunts
                   << " trades=" << trades << " raids=" << raids;
            std::int64_t money = playerCoins;
            std::int64_t goods = game.supplies;
            size_t alive = 0, merchants = 0, merchantsAtMarket = 0, away = 0;
            float farthest = 0;
            size_t wilderness = 0, pursuing = 0, searching = 0, committed = 0;
            for (const auto& pair : citizens)
            {
                const auto& resident = pair.second;
                wilderness += !IsVillage(Locate(resident.village, resident.position).chunk) ? 1 : 0;
                pursuing +=
                    resident.intention == Activity::Raid && resident.target && resident.raidCooldown <= 0 ? 1
                                                                                                          : 0;
                searching +=
                    resident.intention == Activity::Raid && !resident.target && resident.raidCooldown <= 0
                        ? 1
                        : 0;
                committed += resident.travelCommitSeconds > 0 ? 1 : 0;
                money += pair.second.coins;
                goods += pair.second.goods;
                alive += pair.second.health > 0 ? 1 : 0;
                away += !TownWorld::Contains(pair.second.village,
                                             Locate(pair.second.village, pair.second.position).chunk)
                            ? 1
                            : 0;
                farthest = (std::max)(farthest, std::sqrt(DistanceSquared(pair.second.position, {384, 400})));
                if (pair.second.profession == Profession::Merchant && pair.second.health > 0)
                {
                    ++merchants;
                    merchantsAtMarket += IsAtMarket(pair.second) && !IsHostile(pair.first) ? 1 : 0;
                }
            }
            report << " wilderness_residents=" << wilderness << " raid_pursuit=" << pursuing
                   << " raid_search=" << searching << " committed_trips=" << committed
                   << " successful_raids_10s=" << raidsSinceReport;
            report << " away_from_home=" << away << " farthest_from_home=" << farthest << " alive=" << alive
                   << " merchants=" << merchants << " merchants_at_market=" << merchantsAtMarket
                   << " npc_attacks_10s=" << attacksSinceReport
                   << " npc_monster_kills_10s=" << monsterKillsSinceReport << " total_coins=" << money
                   << " total_goods=" << goods << " cpu_total_ms=" << updateMilliseconds
                   << " cpu_peak_ms=" << peakUpdateMilliseconds;
            RenderProfiler::Event("society_summary", report.str(), 0, updateMilliseconds);
            std::printf("[society_combat] %s\n", report.str().c_str());
            attacksSinceReport = monsterKillsSinceReport = 0;
            raidsSinceReport = 0;
            nearSteps = farSteps = 0;
            updateMilliseconds = peakUpdateMilliseconds = 0;
        }
    }

    bool WorldSociety::DamageCitizen(Game& game, std::uint64_t id, float damage)
    {
        auto it = citizens.find(id);
        if (it == citizens.end() || it->second.health <= 0 || it->second.reviveProtection > 0 ||
            !std::isfinite(damage) || damage <= 0)
        {
            return false;
        }
        auto& npc = it->second;
        if (!IsHostile(id))
        {
            notoriety = (std::min)(100, notoriety + 5);
        }
        npc.hostileSeconds = 30;
        npc.health = (std::max)(0.0f, npc.health - damage);
        npc.hitVisualSeconds = .35f;
        Log("player_hit", 0, id, static_cast<int>(damage), L"여행자와 주민의 전투");
        if (npc.health <= 0)
        {
            Kill(game, npc, nullptr, true);
            ++game.life.anger;
            game.Notify(L"주민의 재산을 획득했습니다. 주민은 마을에서 즉시 부활합니다.");
        }
        game.dirty = true;
        return true;
    }

    bool WorldSociety::DamageQuarry(Game& game, ChunkKey village, int slot, float damage)
    {
        auto it = hunting.find({village, slot});
        if (it == hunting.end() || it->second.health <= 0 || it->second.placementPending || damage <= 0)
        {
            return false;
        }
        auto& ground = it->second;
        ground.health = (std::max)(0.0f, ground.health - damage);
        ground.hitVisualSeconds = .35f;
        if (ground.health <= 0)
        {
            ground.respawnSeconds = QuarryRespawnSeconds;
            playerCoins = (std::min)(CoinLimit, playerCoins + 3);
            game.supplies = (std::min)(99, game.supplies + 2);
            ++game.life.kills;
            game.growth.lessons |= 2u;
            game.AddExperience(12);
            ++hunts;
            Log("player_hunt", 0, static_cast<std::uint64_t>(slot), 3, L"여행자 사냥 성공 · 동전 +3, 물자 +2",
                &village);
        }
        game.dirty = true;
        return true;
    }

    bool WorldSociety::Trade(Game& game, std::uint64_t id, bool buy)
    {
        const auto* merchant = Find(id);
        if (!merchant || merchant->profession != Profession::Merchant || merchant->health <= 0 ||
            !IsAtMarket(*merchant) || IsHostile(id))
        {
            game.Notify(L"시장에서 살아 있는 비적대 상인과 거래할 수 있습니다.");
            return false;
        }
        return buy ? PurchaseItem(game, id, ItemId::Supplies) : SellSupplies(game, id);
    }

    bool WorldSociety::Give(Game& game, std::uint64_t id)
    {
        auto it = citizens.find(id);
        if (it == citizens.end() || it->second.health <= 0 || IsHostile(id))
        {
            return false;
        }
        auto& npc = it->second;
        if (npc.lastGiftLife == game.life.id || game.supplies <= 0 || npc.goods >= 99)
        {
            game.Notify(L"나눔 불가 · 이번 삶의 나눔 이력과 양쪽 물자를 확인하세요.");
            return false;
        }
        --game.supplies;
        ++npc.goods;
        npc.lastGiftLife = game.life.id;
        ++game.life.gifts;
        ++game.life.hope;
        if (npc.originLife)
        {
            ++game.memories.at(npc.originLife).bond;
            game.memories.at(npc.originLife).lastGiftLife = game.life.id;
        }
        game.dirty = true;
        game.Notify(L"물자를 나눴습니다. 주민의 생활과 이번 삶에 기록됩니다.");
        return true;
    }

    std::wstring WorldSociety::Describe(std::uint64_t id) const
    {
        const auto* npc = Find(id);
        if (!npc)
        {
            return L"주민 기록 없음";
        }
        const Location current = Locate(npc->village, npc->position);
        return L"주민 #" + std::to_wstring(id) + L" · " + JobName(npc->profession) +
               (npc->originLife ? L" · 제" + std::to_wstring(npc->originLife) + L"의 삶에서 전승" : L"") +
               L"\n목표: " + GoalName(npc->ambition) + L" / 행동: " + ActivityName(npc->activity) + L" → " +
               ActivityName(npc->intention) + L"\n체력 " + std::to_wstring(static_cast<int>(npc->health)) +
               L"/" + std::to_wstring(static_cast<int>(npc->maximumHealth)) + L"  동전 " +
               std::to_wstring(npc->coins) + L"  물자 " + std::to_wstring(npc->goods) + L"  깨달음 " +
               std::to_wstring(npc->insight) + L"\n약탈 성향 " +
               std::to_wstring(static_cast<int>(npc->raidWeight * 100)) + L"% · 마을 (" +
               std::to_wstring(npc->village.x) + L"," + std::to_wstring(npc->village.y) + L") / 현재 (" +
               std::to_wstring(current.chunk.x) + L"," + std::to_wstring(current.chunk.y) + L")" + L"\n집 " +
               std::to_wstring(npc->houseLot + 1) + L"호 · " + JourneyName(npc->journeyKind) +
               L" · 시장에서 E로 상인과 거래" + L"\n목표 점수 휴/사/약/거/수 " +
               std::to_wstring(static_cast<int>(npc->decisionScores[0])) + L"/" +
               std::to_wstring(static_cast<int>(npc->decisionScores[1])) + L"/" +
               std::to_wstring(static_cast<int>(npc->decisionScores[2])) + L"/" +
               std::to_wstring(static_cast<int>(npc->decisionScores[3])) + L"/" +
               std::to_wstring(static_cast<int>(npc->decisionScores[4])) + L" · 부활 " +
               std::to_wstring(npc->deaths);
    }

    unsigned WorldSociety::PageCount() const
    {
        return (std::max)(1u, static_cast<unsigned>((citizens.size() + 7) / 8));
    }

    std::wstring WorldSociety::Ledger(unsigned page) const
    {
        std::wstring text;
        unsigned index = 0;
        for (const auto& pair : citizens)
        {
            const auto& npc = pair.second;
            if (index >= (page % PageCount()) * 8 && index < (page % PageCount() + 1) * 8)
            {
                text += L"#" + std::to_wstring(npc.id) + L" " + JobName(npc.profession) + L" · " +
                        ActivityName(npc.activity) + L" / HP " +
                        std::to_wstring(static_cast<int>(npc.health)) + L" / 돈 " +
                        std::to_wstring(npc.coins) + L" / 물자 " + std::to_wstring(npc.goods) +
                        L" / 깨달음 " + std::to_wstring(npc.insight) + L"\n";
            }
            ++index;
        }
        return text;
    }

    std::wstring WorldSociety::Summary() const
    {
        unsigned alive = 0;
        for (const auto& pair : citizens)
        {
            alive += pair.second.health > 0 ? 1 : 0;
        }
        return L"주민 " + std::to_wstring(alive) + L"/" + std::to_wstring(citizens.size()) + L" · 사냥 " +
               std::to_wstring(hunts) + L" 거래 " + std::to_wstring(trades) + L" 약탈 " +
               std::to_wstring(raids);
    }
} // namespace Ashen
