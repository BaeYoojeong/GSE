#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "AcceptanceChecks.h"
#include "DarkAgeGame.h"
#include "AssetCatalog.h"
#include "RenderProfiler.h"
#include "ThirdParty/json.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <cmath>

namespace Ashen
{
    int AcceptanceChecks::Run(const std::filesystem::path& directory)
    {
        using Json = nlohmann::json;
        if (!directory.is_absolute() || std::filesystem::exists(directory))
        {
            std::cerr << "Self-test requires an absolute, new directory. Existing profiles are never used.\n";
            return 2;
        }
        std::filesystem::create_directories(directory);
        wchar_t previous[32768] = {};
        GetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", previous, 32768);
        SetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", directory.c_str());

        struct Restore
        {
            const wchar_t* value;

            ~Restore()
            {
                SetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", *value ? value : nullptr);
            }
        } restore{previous};

        RenderProfiler::Initialize();
        Json report = {{"schema", "ashen_acceptance_1"},
                       {"checks", Json::array()},
                       {"passed", false},
                       {"renders_game", false}};
        auto require = [&](bool valid, const char* name)
        {
            report["checks"].push_back({{"name", name}, {"passed", valid}});
            if (!valid)
            {
                throw std::runtime_error(name);
            }
        };
        Game game;
        game.headless = true;
        try
        {
            require(game.Initialize() && game.StorageHealthy(), "new_isolated_profile");
            require(game.society.citizens.size() == 210, "seven_towns_210_residents");
            std::array<unsigned, TownWorld::Count> populations = {};
            std::set<std::pair<ChunkKey, unsigned>> plots;
            for (const auto& pair : game.society.citizens)
            {
                const auto& npc = pair.second;
                const int index = TownWorld::Index(npc.village);
                require(index >= 0 && plots.emplace(npc.village, npc.houseLot).second && npc.houseLot != 35,
                        "unique_personal_house");
                ++populations[index];
            }
            for (auto count : populations)
            {
                require(count == 30, "thirty_residents_per_town");
            }
            const double travel = TownWorld::CenterSpacing * ChunkSize / TownWorld::PlayerSpeed;
            require(travel >= 19 && travel <= 21, "center_to_center_twenty_seconds");
            report["center_travel_seconds"] = travel;
            game.jumpRequested = true;
            game.hero->Animate(.01f);
            require(game.hero->motion == WorldActor::Motion::Jump && game.immunity > 0,
                    "jump_motion_and_brief_protection");
            game.hero->jumpRemaining = 0;
            game.hero->jumpCooldown = 0;
            game.hero->moving = false;
            game.hero->facing = 0;
            game.hero->Animate(.01f);
            require(game.hero->motion == WorldActor::Motion::TurnLeft ||
                        game.hero->motion == WorldActor::Motion::TurnRight,
                    "turn_clip_connected");
            for (const char* state : {"idle", "walk", "run", "jump", "turn_left", "turn_right"})
            {
                AssetCatalog::Request request;
                request.asset = AssetCatalog::RoleAsset("player", game.life.id);
                request.parts = AssetCatalog::NpcParts(game.life.id, 2);
                request.clip = AssetCatalog::ClipName(state);
                request.seconds = AssetCatalog::ClipDuration(state) * .5f;
                request.loop = false;
                const auto model = AssetCatalog::Prepare(request);
                require(model && model.pose.clip >= 0, "required_motion_asset_loads");
                std::vector<MeshCache::Vertex> vertices;
                require(GltfModel::Project(*model.model, model.pose, model.projection, vertices) &&
                            !vertices.empty(),
                        "skeletal_pose_projects_without_graphics_context");
            }
            // Run logical simulation only: no window, OpenGL context or monitoring socket.
            for (unsigned i = 0; i < 1200; ++i)
            {
                game.immunity = 1000;
                game.society.Update(game, .5f);
                game.farSimulation->WaitReady(250);
                game.society.Update(game, 0);
            }
            const auto worker = game.farSimulation->Stats();
            report["worker"] = {{"accepted", worker.accepted},
                                {"rejected", worker.rejected},
                                {"residents", worker.residents},
                                {"cpu_ms", worker.workerMilliseconds}};
            require(worker.accepted > 0, "background_npc_results_committed");
            require(game.society.lettersDelivered > 0, "physical_courier_delivery");
            require(game.society.hunts > 0 && game.society.trades > 0 && game.society.raids > 0,
                    "hunting_trading_raiding_continue");
            const auto npcId = game.society.citizens.begin()->first;
            game.farSimulation->WaitReady(1000);
            game.farSimulation->Poll(game);
            game.farSimulation.reset();
            const auto deaths = game.society.Find(npcId)->deaths;
            game.society.citizens.at(npcId).reviveProtection = 0;
            game.society.DamageCitizen(game, npcId, 10000);
            require(game.society.citizens.size() == 210 && game.society.Find(npcId)->health > 0 &&
                        game.society.Find(npcId)->deaths == deaths + 1,
                    "immediate_respawn_keeps_population");
            // Every previously visited region must survive cache eviction and restart.
            for (int i = 0; i < 200; ++i)
            {
                const ChunkKey key = {PlanetWorld::LongitudeChunks + i, 10000};
                require(game.RegionAt(key) != nullptr, "far_region_read");
                game.regions.at(key).harvested = 1;
            }
            require(game.Save() && game.CachedRegionCount() <= 128 && game.StoredRegionCount() >= 200,
                    "sparse_database_and_bounded_cache");
            require(game.RegionAt({PlanetWorld::LongitudeChunks, 10000})->harvested == 1,
                    "evicted_progress_reloaded");
            const auto positive = PlanetWorld::Locate({{PlanetWorld::LongitudeChunks, 0}, {0, 0}});
            const auto negative = PlanetWorld::Locate({{-PlanetWorld::LongitudeChunks, 0}, {0, 0}});
            require(positive.planetX == 1 && negative.planetX == -1 && positive.surfaceX == 0 &&
                        negative.surfaceX == 0,
                    "signed_planet_partition_coordinates");
            // Build a V5 society snapshot in memory, then verify real migration preserves identities and
            // wealth.
            WorldSociety legacy = game.society;
            for (auto& pair : legacy.citizens)
            {
                pair.second.village = TownWorld::LegacyCenter(TownWorld::Index(pair.second.village));
            }
            decltype(legacy.hunting) grounds;
            for (auto pair : legacy.hunting)
            {
                pair.second.village = TownWorld::LegacyCenter(TownWorld::Index(pair.second.village));
                grounds.emplace(std::make_pair(pair.second.village, pair.second.slot), pair.second);
            }
            legacy.hunting = std::move(grounds);
            std::ostringstream old;
            old << std::setprecision(9);
            legacy.Write(old);
            auto text = old.str();
            text.replace(0, 10, "SOCIETY_V5");
            WorldSociety migrated;
            std::istringstream input(text);
            require(migrated.Read(input, game.life.id), "v5_society_migrates");
            for (const auto& pair : game.society.citizens)
            {
                const auto* npc = migrated.Find(pair.first);
                require(npc && npc->coins == pair.second.coins && npc->houseLot == pair.second.houseLot &&
                            npc->village == pair.second.village,
                        "migration_preserves_identity_wealth_house");
            }
            require(game.Save(), "atomic_world_commit");
            const auto coins = game.society.Find(npcId)->coins;
            game.Shutdown();
            require(game.Initialize() && game.StorageHealthy(), "database_profile_restart");
            require(game.society.Find(npcId)->coins == coins &&
                        game.RegionAt({PlanetWorld::LongitudeChunks, 10000})->harvested == 1,
                    "restart_preserves_npc_and_far_progress");
            report["letters_delivered"] = game.society.lettersDelivered;
            report["migrations"] = game.society.migrationsCompleted;
            report["stored_regions"] = game.StoredRegionCount();
            report["passed"] = true;
        }
        catch (const std::exception& error)
        {
            report["error"] = error.what();
        }
        game.Shutdown();
        AssetCatalog::Shutdown();
        RenderProfiler::Shutdown();
        std::ofstream out(directory / L"acceptance.json");
        out << report.dump(2);
        std::cout << "Acceptance " << (report["passed"].get<bool>() ? "passed" : "failed")
                  << ". See acceptance.json in the selected directory.\n";
        return report["passed"].get<bool>() ? 0 : 1;
    }
} // namespace Ashen
