#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "WorldMonitor.h"
#include "DarkAgeGame.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

namespace Ashen
{
    namespace
    {
        std::string Utf8(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }
            const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                                   nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<size_t>(length), '\0');
            if (length > 0)
            {
                WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                                    length, nullptr, nullptr);
            }
            return result;
        }

        // Only the game thread builds JSON. HTTP clients receive an immutable complete snapshot.
        class Json
        {
          public:
            Json()
            {
                out.imbue(std::locale::classic());
                out << std::setprecision(15);
            }

            void Object(const char* name = nullptr)
            {
                Prefix(name);
                out << '{';
                first.push_back(true);
            }

            void Array(const char* name)
            {
                Prefix(name);
                out << '[';
                first.push_back(true);
            }

            void EndObject()
            {
                first.pop_back();
                out << '}';
            }

            void EndArray()
            {
                first.pop_back();
                out << ']';
            }

            void String(const char* name, const std::string& value)
            {
                Prefix(name);
                Quote(value);
            }

            void String(const char* name, const std::wstring& value)
            {
                String(name, Utf8(value));
            }

            template <typename T> void Number(const char* name, T value)
            {
                Prefix(name);
                if constexpr (std::is_floating_point_v<T>)
                {
                    if (!std::isfinite(value))
                    {
                        out << "null";
                        return;
                    }
                }
                out << value;
            }

            void Boolean(const char* name, bool value)
            {
                Prefix(name);
                out << (value ? "true" : "false");
            }

            void Null(const char* name)
            {
                Prefix(name);
                out << "null";
            }

            std::string Finish() const
            {
                return out.str();
            }

          private:
            void Prefix(const char* name)
            {
                if (!first.empty())
                {
                    if (!first.back())
                    {
                        out << ',';
                    }
                    first.back() = false;
                }
                if (name)
                {
                    Quote(name);
                    out << ':';
                }
            }

            void Quote(const std::string& value)
            {
                constexpr char hex[] = "0123456789abcdef";
                out << '"';
                for (unsigned char c : value)
                {
                    if (c == '"' || c == '\\')
                    {
                        out << '\\' << static_cast<char>(c);
                    }
                    else if (c < 32)
                    {
                        out << "\\u00" << hex[c >> 4] << hex[c & 15];
                    }
                    else
                    {
                        out << static_cast<char>(c);
                    }
                }
                out << '"';
            }

            std::ostringstream out;
            std::vector<bool> first;
        };

        std::string VillageId(ChunkKey key)
        {
            return std::to_string(key.x) + "," + std::to_string(key.y);
        }

        void WriteLocation(Json& json, const char* name, const Location& location)
        {
            json.Object(name);
            json.Object("chunk");
            json.String("x", std::to_string(location.chunk.x));
            json.String("y", std::to_string(location.chunk.y));
            json.EndObject();
            json.Object("local");
            json.Number("x", location.local.x);
            json.Number("y", location.local.y);
            json.EndObject();
            json.EndObject();
        }

        struct VillageSummary
        {
            unsigned total = 0, alive = 0, hostile = 0, injured = 0, protectedCount = 0;
            unsigned away = 0, visitors = 0;
            std::uint64_t coins = 0, goods = 0, insight = 0;
            double raidWeight = 0;
            std::array<unsigned, 4> jobs = {};
            std::array<unsigned, 7> activities = {};
            unsigned quarryAlive = 0, quarrySlots = 0, ordinaryAlive = 0, ordinaryLoaded = 0;
        };

        constexpr const char* JobKeys[] = {"hunter", "raider", "merchant", "seeker"};
        constexpr const char* GoalKeys[] = {"vitality", "wealth", "insight"};
        constexpr const char* ActivityKeys[] = {"rest", "hunt", "raid", "trade", "study", "return", "dead"};

        std::int64_t NowMilliseconds()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }
    } // namespace

    void WorldMonitor::Start(const Game& game)
    {
        Stop();
        unsigned short port = 8765;
        char value[32] = {};
        const DWORD length = GetEnvironmentVariableA("ASHEN_MONITOR_PORT", value, sizeof(value));
        if (length)
        {
            char* end = nullptr;
            const unsigned long requested = length < sizeof(value) ? std::strtoul(value, &end, 10) : 65536;
            if (end && *end == '\0' && value[0] != '-' && requested <= 65535)
            {
                port = static_cast<unsigned short>(requested);
            }
            else
            {
                std::printf("[Monitor] Invalid ASHEN_MONITOR_PORT; using 8765.\n");
            }
        }
        static unsigned session = 0;
        sessionId = std::to_string(NowMilliseconds()) + "-" + std::to_string(GetCurrentProcessId()) + "-" +
                    std::to_string(++session);
        sequence = 0;
        active = server.Start(port);
        if (active)
        {
            Update(game, true);
        }
    }

    void WorldMonitor::Update(const Game& game, bool force)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!active || (!force && now < nextPublish))
        {
            return;
        }
        nextPublish = now + std::chrono::seconds(1);
        try
        {
            ++sequence;
            std::string json = Snapshot(game);
            const auto bytes = json.size();
            server.Publish(std::move(json));
            const double milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count();
            RenderProfiler::Event("monitor_snapshot",
                                  "sequence=" + std::to_string(sequence) +
                                      " npcs=" + std::to_string(game.society.citizens.size()),
                                  bytes, milliseconds);
        }
        catch (const std::exception& error)
        {
            std::printf("[Monitor] Snapshot unavailable: %s\n", error.what());
        }
    }

    void WorldMonitor::Stop()
    {
        active = false;
        server.Stop();
    }

    std::string WorldMonitor::Snapshot(const Game& game)
    {
        const auto& society = game.society;
        std::map<ChunkKey, VillageSummary> villages;
        for (const auto& town : TownWorld::Towns())
        {
            villages.emplace(town.center, VillageSummary{});
        }
        std::set<ChunkKey> knownChunks;
        std::set<std::uint64_t> visibleCitizens;
        std::map<ChunkKey, unsigned> loadedEnemies;
        std::map<ChunkKey, unsigned> visitors;
        std::uint64_t aliveCitizens = 0, aliveQuarry = 0, ordinaryAlive = 0, ordinaryLoaded = 0;

        for (const auto& pair : game.regions)
        {
            knownChunks.insert(pair.first);
        }
        for (const auto& pair : game.chunks)
        {
            knownChunks.insert(pair.first);
        }
        for (Actor* base : game.scene.Snapshot())
        {
            const auto* actor = dynamic_cast<const WorldActor*>(base);
            if (!actor || !actor->IsActive() || actor->health <= 0)
            {
                continue;
            }
            if (actor->kind == Kind::Citizen && actor->IsVisible())
            {
                visibleCitizens.insert(actor->memory);
            }
            else if (actor->kind == Kind::Enemy)
            {
                ++loadedEnemies[actor->chunk];
                ++ordinaryLoaded;
            }
        }
        for (const auto& pair : society.citizens)
        {
            const auto& npc = pair.second;
            auto& village = villages[npc.village];
            knownChunks.insert(npc.village);
            ++village.total;
            village.alive += npc.health > 0 ? 1 : 0;
            aliveCitizens += npc.health > 0 ? 1 : 0;
            const Location location = WorldSociety::Locate(npc.village, npc.position);
            knownChunks.insert(location.chunk);

            if (npc.health > 0 && !TownWorld::Contains(npc.village, location.chunk))
            {
                ++village.away;
                const int town = TownWorld::At(location.chunk);
                if (town >= 0)
                {
                    ++visitors[TownWorld::Towns()[town].center];
                }
            }

            village.hostile += society.IsHostile(npc.id) ? 1 : 0;
            village.injured += npc.health < npc.maximumHealth ? 1 : 0;
            village.protectedCount += npc.reviveProtection > 0 ? 1 : 0;
            village.coins += npc.coins;
            village.goods += npc.goods;
            village.insight += npc.insight;
            village.raidWeight += npc.raidWeight;
            ++village.jobs[static_cast<unsigned>(npc.profession)];
            ++village.activities[static_cast<unsigned>(npc.activity)];
        }
        for (const auto& pair : society.hunting)
        {
            const auto& quarry = pair.second;
            auto& village = villages[quarry.village];
            knownChunks.insert(quarry.village);
            knownChunks.insert(WorldSociety::Locate(quarry.village, quarry.position).chunk);
            ++village.quarrySlots;
            if (quarry.health > 0 && !quarry.placementPending)
            {
                ++village.quarryAlive;
                ++aliveQuarry;
            }
        }
        for (const auto& key : knownChunks)
        {
            const auto* region = game.RegionAt(key);
            unsigned alive = 0;
            for (int slot = 0; slot < 2; ++slot)
            {
                alive += game.EnemySlotEnabled(key, slot) && (!region || !(region->defeated & (1u << slot)))
                             ? 1
                             : 0;
            }
            ordinaryAlive += alive;
            const auto village = villages.find(key);
            if (village != villages.end())
            {
                village->second.ordinaryAlive = alive;
                village->second.ordinaryLoaded = loadedEnemies[key];
                village->second.visitors = visitors[key];
            }
        }

        Json json;
        json.Object();
        json.Number("schema_version", 1);
        json.String("session_id", sessionId);
        json.Number("sequence", sequence);
        json.Number("generated_at_ms", NowMilliseconds());
        json.Number("simulation_seconds", society.simulationSeconds);
        json.Object("game");
        json.Boolean("running", game.initialized);
        json.Boolean("player_dead", game.Dead());
        json.Boolean("pk_enabled", game.pkEnabled);
        json.EndObject();
        json.Object("world_streaming");
        const auto planet = PlanetWorld::Locate(game.Absolute(game.hero->position));
        json.Number("planet_x", planet.planetX);
        json.Number("planet_y", planet.planetY);
        json.Number("longitude", planet.longitude);
        json.Number("latitude", planet.latitude);
        json.Number("cached_regions", game.CachedRegionCount());
        json.Number("stored_regions", game.StoredRegionCount());
        json.Number("center_travel_seconds", TownWorld::CenterSpacing * ChunkSize / TownWorld::PlayerSpeed);
        if (game.farSimulation)
        {
            const auto worker = game.farSimulation->Stats();
            json.Number("worker_batches", worker.accepted);
            json.Number("worker_conflicts", worker.rejected);
            json.Number("worker_residents", worker.residents);
            json.Number("worker_cpu_ms", worker.workerMilliseconds);
            json.Number("worker_commit_ms", worker.commitMilliseconds);
        }
        json.EndObject();
        json.Object("totals");
        json.String("monster_count_scope", "resident_activity_and_memory_cached_regions");
        json.Number("npc_total", society.citizens.size());
        json.Number("npc_alive", aliveCitizens);
        json.Number("npc_visible", visibleCitizens.size());
        json.Number("village_total", villages.size());
        json.Number("monster_alive_known", aliveQuarry + ordinaryAlive);
        json.Number("quarry_alive", aliveQuarry);
        json.Number("quarry_slots", society.hunting.size());
        json.Number("ordinary_monster_alive_known", ordinaryAlive);
        json.Number("ordinary_monster_loaded", ordinaryLoaded);
        json.Number("loaded_chunks", game.chunks.size());
        json.Number("known_chunks", knownChunks.size());
        json.Number("trades", society.trades);
        json.Number("raids", society.raids);
        json.Number("hunts", society.hunts);
        json.EndObject();
        json.Array("villages");
        for (const auto& pair : villages)
        {
            const auto& village = pair.second;
            const auto recent = society.RecentIncidents(pair.first);
            const double count = (std::max)(1u, village.total);
            const double raiderShare = village.jobs[1] / count;
            const double averageWeight = village.raidWeight / count;
            const double violence =
                (std::clamp)(recent.murders * 30.0 + recent.raids * 6.0 + village.hostile / count * 30, 0.0,
                             100.0);
            const double safety =
                100 - (std::clamp)(averageWeight * 45 + raiderShare * 30 + violence * .6, 0.0, 100.0);
            std::string type = "mixed";
            std::wstring label = L"혼합 마을", description = L"여러 직업과 성향이 공존합니다.";
            if (violence >= 60)
            {
                type = "conflict";
                label = L"분쟁 중";
                description = L"최근 살인·약탈과 적대 행동이 많습니다.";
            }
            else if (raiderShare >= .34 || averageWeight >= .55)
            {
                type = "raider";
                label = L"약탈자 마을";
                description = L"약탈 직업 또는 약탈 성향이 우세합니다.";
            }
            else if (recent.murders > 0)
            {
                type = "unrest";
                label = L"흉흉한 마을";
                description = L"최근 살인 사건이 발생했습니다.";
            }
            else if (village.jobs[2] / count >= .4)
            {
                type = "trading";
                label = L"상업 마을";
                description = L"상인 비중이 높은 거점입니다.";
            }
            else if (village.jobs[3] / count >= .4)
            {
                type = "learning";
                label = L"수련 마을";
                description = L"깨달음을 추구하는 구도자가 많습니다.";
            }
            else if (averageWeight < .2 && recent.raids == 0)
            {
                type = "peaceful";
                label = L"선량한 마을";
                description = L"약탈 성향이 낮고 최근 폭력 사건이 없습니다.";
            }
            else if (village.jobs[0] / count >= .5)
            {
                type = "hunting";
                label = L"사냥꾼 마을";
                description = L"사냥을 생업으로 삼는 주민이 많습니다.";
            }
            json.Object();
            json.String("id", VillageId(pair.first));
            json.String("name", std::wstring(TownWorld::Towns()[TownWorld::Index(pair.first)].name));
            json.Number("houses", village.total);
            json.Number("districts", 9);
            json.Array("known_news");
            const int observer = TownWorld::Index(pair.first);
            for (unsigned subject = 0; subject < TownWorld::Count; ++subject)
            {
                const auto& report = society.knowledge[observer][subject];
                json.Object();
                json.String("village_id", VillageId(TownWorld::Towns()[subject].center));
                json.Boolean("known", report.authored >= 0);
                json.Boolean("local", subject == static_cast<unsigned>(observer));
                json.Number("authored_seconds", report.authored);
                json.Number("age_seconds",
                            report.authored < 0 ? -1.0 : society.simulationSeconds - report.authored);
                json.Number("population", report.population);
                json.Number("safety", report.safety);
                json.Number("wealth", report.wealth);
                json.Number("murders", report.murders);
                json.Number("raids", report.raids);
                json.EndObject();
            }
            json.EndArray();
            json.Object("chunk");
            json.String("x", std::to_string(pair.first.x));
            json.String("y", std::to_string(pair.first.y));
            json.EndObject();
            bool loaded = false;
            for (const auto& chunk : game.chunks)
            {
                loaded |= TownWorld::Contains(pair.first, chunk.first);
            }
            json.Boolean("loaded", loaded);
            json.Object("mood");
            json.String("type", type);
            json.String("label", label);
            json.String("description", description);
            json.Number("raider_share", raiderShare);
            json.Number("avg_raid_weight", averageWeight);
            json.Number("violence_score", violence);
            json.Number("safety_score", safety);
            json.EndObject();
            json.Object("population");
            json.Number("total", village.total);
            json.Number("alive", village.alive);
            json.Number("hostile", village.hostile);
            json.Number("injured", village.injured);
            json.Number("protected", village.protectedCount);
            json.Number("away", village.away);
            json.Number("present", village.alive - village.away);
            json.Number("visitors", village.visitors);
            json.EndObject();
            json.Object("economy");
            json.Number("coins", village.coins);
            json.Number("goods", village.goods);
            json.Number("insight", village.insight);
            json.EndObject();
            json.Object("monsters");
            json.Number("quarry_alive", village.quarryAlive);
            json.Number("quarry_slots", village.quarrySlots);
            json.Number("ordinary_alive", village.ordinaryAlive);
            json.Number("ordinary_loaded", village.ordinaryLoaded);
            json.EndObject();
            json.Object("activities");
            for (unsigned i = 0; i < village.activities.size(); ++i)
            {
                json.Number(ActivityKeys[i], village.activities[i]);
            }
            json.EndObject();
            json.Object("recent");
            json.Number("murders", recent.murders);
            json.Number("raids", recent.raids);
            json.Number("trades", recent.trades);
            json.EndObject();
            if (recent.lastIncidentSeconds < 0)
            {
                json.Null("last_incident_seconds");
            }
            else
            {
                json.Number("last_incident_seconds", recent.lastIncidentSeconds);
            }
            json.EndObject();
        }
        json.EndArray();
        json.Object("communication");
        json.String("delivered", std::to_string(society.lettersDelivered));
        json.String("migrations_completed", std::to_string(society.migrationsCompleted));
        json.Array("letters");
        for (const auto& pair : society.letters)
        {
            const auto& letter = pair.second;
            json.Object();
            json.String("id", std::to_string(letter.id));
            json.String("carrier", std::to_string(letter.carrier));
            json.String("source", VillageId(TownWorld::Towns()[letter.source].center));
            json.String("destination", VillageId(TownWorld::Towns()[letter.destination].center));
            json.Number("travel_seconds", society.simulationSeconds - letter.sent);
            json.EndObject();
        }
        json.EndArray();
        json.EndObject();
        json.Object("chapter");
        json.Number("stage", static_cast<unsigned>(game.chapter.CurrentStage()));
        json.String("objective", game.chapter.Objective());
        json.Boolean("complete", game.chapter.Complete());
        json.EndObject();
        json.Array("npcs");
        for (const auto& pair : society.citizens)
        {
            const auto& npc = pair.second;
            json.Object();
            json.String("id", std::to_string(npc.id));
            json.String("origin_life", std::to_string(npc.originLife));
            json.String("village_id", VillageId(npc.village));
            json.Number("house_number", npc.houseLot + 1);
            WriteLocation(json, "house_location",
                          WorldSociety::Locate(npc.village, TownWorld::House(npc.houseLot)));
            json.Number("travel_kind", static_cast<unsigned>(npc.journeyKind));
            json.String("travel_label", std::wstring(WorldSociety::JourneyName(npc.journeyKind)));
            json.String("mail_id", std::to_string(npc.mailId));
            if (npc.destinationTown >= 0)
            {
                json.String("destination_village", VillageId(TownWorld::Towns()[npc.destinationTown].center));
            }
            else
            {
                json.Null("destination_village");
            }
            json.Number("transit_seconds", npc.transitAge);
            json.String("profession", std::string(JobKeys[static_cast<unsigned>(npc.profession)]));
            json.String("profession_label", std::wstring(WorldSociety::JobName(npc.profession)));
            json.String("ambition", std::string(GoalKeys[static_cast<unsigned>(npc.ambition)]));
            json.String("ambition_label", std::wstring(WorldSociety::GoalName(npc.ambition)));
            json.String("activity", std::string(ActivityKeys[static_cast<unsigned>(npc.activity)]));
            json.String("activity_label", std::wstring(WorldSociety::ActivityName(npc.activity)));
            json.String("intention", std::string(ActivityKeys[static_cast<unsigned>(npc.intention)]));
            json.String("intention_label", std::wstring(WorldSociety::ActivityName(npc.intention)));
            json.Object("position");
            json.Number("x", npc.position.x);
            json.Number("y", npc.position.y);
            json.EndObject();
            const Location location = WorldSociety::Locate(npc.village, npc.position);
            WriteLocation(json, "location", location);
            json.Boolean("away_from_home", !TownWorld::Contains(npc.village, location.chunk));

            Location destination = WorldSociety::Locate(npc.village, npc.activityGoal);
            bool hasDestination = npc.health > 0 && npc.goalActivity == npc.intention;
            std::string destinationKind = "activity";

            if (npc.intention == Activity::Hunt)
            {
                const auto quarry = society.hunting.find({npc.village, npc.quarrySlot});
                hasDestination = npc.health > 0 && quarry != society.hunting.end() &&
                                 quarry->second.health > 0 && !quarry->second.placementPending;

                if (hasDestination)
                {
                    destination = WorldSociety::Locate(quarry->second.village, quarry->second.position);
                }

                destinationKind = "quarry";
            }
            else if (npc.intention == Activity::Raid && npc.target == 0)
            {
                destinationKind = "patrol";
            }
            else if (npc.intention == Activity::Raid && npc.target != 0)
            {
                const auto target = society.citizens.find(npc.target);
                hasDestination =
                    npc.health > 0 && target != society.citizens.end() && target->second.health > 0;

                if (hasDestination)
                {
                    destination = WorldSociety::Locate(target->second.village, target->second.position);
                }

                destinationKind = "target";
            }

            if (npc.journeyKind != JourneyKind::None && npc.destinationTown >= 0)
            {
                const auto center = TownWorld::Towns()[npc.destinationTown].center;
                const Vec2 point = npc.journeyKind == JourneyKind::Migration ? TownWorld::Door(npc.pendingLot)
                                   : npc.journeyKind == JourneyKind::Returning ? TownWorld::Door(npc.houseLot)
                                                                               : Vec2{384, 460};
                destination = WorldSociety::Locate(center, point);
                hasDestination = true;
                destinationKind = "journey";
            }
            if (hasDestination)
            {
                WriteLocation(json, "destination", destination);
            }
            else
            {
                json.Null("destination");
            }

            json.String("destination_kind", hasDestination ? destinationKind : std::string("unassigned"));
            json.Number("journey", npc.journey);
            json.Number("goal_dwell_seconds", npc.goalDwellSeconds);
            json.Number("health", npc.health);
            json.Number("max_health", npc.maximumHealth);
            json.Number("coins", npc.coins);
            json.Number("goods", npc.goods);
            json.Number("insight", npc.insight);
            json.Number("raid_weight", npc.raidWeight);
            json.String("raid_phase_label", std::wstring(WorldSociety::RaidPhase(npc)));
            json.Number("raid_cooldown_seconds", npc.raidCooldown);
            json.String("successful_raids_session", std::to_string(npc.successfulRaids));
            json.String("stolen_coins_session", std::to_string(npc.stolenCoins));
            json.String("last_raid_target", std::to_string(npc.lastRaidTarget));
            json.Number("last_raid_amount", npc.lastRaidAmount);
            json.Number("travel_commit_seconds", npc.travelCommitSeconds);
            json.Number("destinations_reached_session", npc.destinationsReached);
            json.Boolean("in_wilderness", !society.IsVillage(location.chunk));
            json.Boolean("hostile", society.IsHostile(npc.id));
            json.Number("revive_protection", npc.reviveProtection);
            json.String("deaths", std::to_string(npc.deaths));
            json.String("target", std::to_string(npc.target));
            json.Number("quarry_slot", npc.quarrySlot);
            json.Object("scores");
            for (unsigned i = 0; i < npc.decisionScores.size(); ++i)
            {
                json.Number(ActivityKeys[i], npc.decisionScores[i]);
            }
            json.EndObject();
            json.Boolean("visible", visibleCitizens.count(npc.id) != 0);
            json.EndObject();
        }
        json.EndArray();
        json.Array("events");
        for (const auto& event : society.monitorEvents)
        {
            json.Object();
            json.String("id", std::to_string(event.id));
            json.Number("time_seconds", event.timeSeconds);
            json.String("type", event.type);
            json.String("severity", event.severity);
            json.String("category", event.category);
            if (event.hasVillage)
            {
                json.String("village_id", VillageId(event.village));
            }
            else
            {
                json.Null("village_id");
            }
            json.String("actor_id", std::to_string(event.actor));
            json.String("target_id", std::to_string(event.target));
            json.Number("amount", event.amount);
            json.String("message", event.message);
            json.Boolean("confirmed", event.confirmed);
            json.Number("witness_count", event.witnessCount);
            if (event.sourceEvent)
            {
                json.String("source_event_id", std::to_string(event.sourceEvent));
            }
            else
            {
                json.Null("source_event_id");
            }
            json.EndObject();
        }
        json.EndArray();
        json.Object("event_window");
        json.String("oldest_id",
                    society.monitorEvents.empty() ? "0" : std::to_string(society.monitorEvents.front().id));
        json.String("newest_id",
                    society.monitorEvents.empty() ? "0" : std::to_string(society.monitorEvents.back().id));
        json.Number("retained", society.monitorEvents.size());
        json.Number("capacity", WorldSociety::EventCapacity);
        json.Number("recent_seconds", WorldSociety::RecentEventSeconds);
        json.EndObject();
        json.Array("warnings");
        json.String(nullptr, std::wstring(L"몹 수는 현재 로딩된 청크·저장된 지역·알려진 마을 범위의 "
                                          L"집계입니다. 무한 월드 전체 수가 아닙니다."));
        json.String(nullptr, std::wstring(L"사건은 이번 실행에서 최근 512건을 보관하며, 마을 분위기는 별도 "
                                          L"최근 120초 기록으로 계산합니다."));
        if (!game.StorageHealthy())
        {
            json.String(nullptr, std::wstring(L"게임 저장소 상태를 확인하세요. 저장 오류가 보고되었습니다."));
        }
        json.EndArray();
        json.EndObject();
        return json.Finish();
    }
} // namespace Ashen
