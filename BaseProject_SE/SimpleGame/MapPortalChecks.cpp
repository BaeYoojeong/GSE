#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "AcceptanceChecks.h"
#include "DarkAgeGame.h"
#include "WorldMap.h"
#include <filesystem>
#include <sstream>

namespace Ashen
{
    void AcceptanceChecks::MapAndWaypoints(Game& game, const std::function<void(bool, const char*)>& require)
    {
        Waypoints state;
        require(!state.Active(0) && state.Activate(0) && !state.Activate(0) &&
                    !state.Activate(TownWorld::Count),
                "waypoint_activation_is_bounded_and_idempotent");
        std::ostringstream encoded;
        state.Write(encoded);
        Waypoints decoded;
        std::istringstream input(encoded.str());
        require(decoded.Read(input) && decoded.Active(0), "waypoint_state_roundtrip");
        std::istringstream corrupt("WAYPOINTS_V1 128");
        require(!decoded.Read(corrupt) && decoded.Active(0), "invalid_waypoint_mask_preserves_old_state");
        WorldMap::Overview();
        for (unsigned town = 0; town < TownWorld::Count; ++town)
        {
            const auto point = WorldMap::Marker(town);
            require(WorldMap::Hit(point.x, point.y) == static_cast<int>(town),
                    "all_map_portal_markers_share_their_click_coordinates");
        }
        require(WorldMap::Hit(WorldMap::Left - 1, WorldMap::Top) < 0 && WorldMap::TravelButton(650, 500) &&
                    !WorldMap::TravelButton(600, 500),
                "map_background_and_travel_button_are_distinct");
        const auto pixels = WorldMap::BuildImage(game);
        require(pixels.size() == WorldMap::Width * WorldMap::Height * 4 &&
                    pixels == WorldMap::BuildImage(game),
                "map_image_is_deterministic_and_sized");
        bool opaque = true;
        for (std::size_t index = 3; index < pixels.size(); index += 4)
        {
            opaque &= pixels[index] == 255;
        }
        require(opaque, "map_rgba_image_is_opaque");

        const Location previous = game.Absolute(game.hero->position);
        const Vec2 oldCamera = game.camera;
        const float previousHealth = game.health;
        const auto relocate = [&](const Location& location)
        {
            game.origin = location.chunk;
            game.hero->position = location.local;
            game.camera = location.local;
            game.Stream();
        };
        require(!game.TravelToWaypoint(1) && game.Absolute(game.hero->position).chunk == previous.chunk &&
                    !game.ActivateWaypoint(1),
                "locked_or_remote_portals_cannot_be_used");
        relocate(Waypoints::Position(0));
        require(game.ActivateWaypoint(0) && game.waypoints.Active(0), "nearby_portal_activates_and_saves");
        relocate(Waypoints::Position(1));
        require(game.ActivateWaypoint(1), "another_portal_requires_a_physical_visit");
        require(game.TravelToWaypoint(0) && game.PlayerLocation().chunk == Waypoints::Position(0).chunk &&
                    DistanceSquared(game.PlayerLocation().local, Waypoints::Position(0).local) <= 105 * 105 &&
                    game.Walkable(game.hero->position, 16),
                "activated_portal_travel_uses_a_walkable_arrival");
        relocate({{1000000000, -1000000000}, {384, 400}});
        require(game.TravelToWaypoint(0) && game.PlayerLocation().chunk == Waypoints::Position(0).chunk,
                "portal_travel_preserves_large_world_chunk_precision");
        game.health = 0;
        require(!game.TravelToWaypoint(0), "dead_player_cannot_use_portal");
        game.health = previousHealth;
        relocate(previous);
        game.camera = oldCamera;
        game.requestedMap = -1;
        require(game.Save(), "waypoints_saved_before_legacy_inventory_migration_check");

        // V7 already has an inventory, but no waypoint section. Preserve its equipment and quantities.
        const auto directory = std::filesystem::path(game.StoragePath()).parent_path() / L"migration-v7";
        std::filesystem::create_directories(directory);
        const auto database = directory / L"World_v1.txt.sqlite3";
        require(game.regionDatabase.Backup(database.wstring()), "v7_migration_fixture_database_copied");
        RegionDatabase legacy;
        std::string core;
        bool found = false;
        require(legacy.Open(database.wstring()) && legacy.ReadCore(core, found) && found,
                "v7_migration_fixture_opens");
        core.replace(0, std::string("ASHEN_WORLD_V8").size(), "ASHEN_WORLD_V7");
        const auto start = core.find("WAYPOINTS_V1 ");
        require(start != std::string::npos, "v8_has_waypoint_section");
        core.erase(start, core.find('\n', start) + 1 - start);
        require(legacy.Commit(core, {}, game.society.simulationSeconds), "v7_core_without_waypoints_created");
        legacy.Close();
        wchar_t oldDirectory[32768] = {};
        GetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", oldDirectory, 32768);

        struct RestoreDirectory
        {
            const wchar_t* value;

            ~RestoreDirectory()
            {
                SetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", *value ? value : nullptr);
            }
        } restore{oldDirectory};

        SetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", directory.c_str());
        Game migrated;
        migrated.headless = true;

        struct StopGame
        {
            Game& game;

            ~StopGame()
            {
                game.Shutdown();
            }
        } stop{migrated};

        require(migrated.Initialize() && migrated.StorageHealthy() && !migrated.waypoints.Active(0) &&
                    migrated.inventory.Equipped() == game.inventory.Equipped() &&
                    migrated.inventory.Quantity(ItemId::HealingPotion) ==
                        game.inventory.Quantity(ItemId::HealingPotion) &&
                    migrated.society.citizens.size() == game.society.citizens.size(),
                "v7_migration_preserves_inventory_and_keeps_portals_locked");
        require(std::filesystem::is_regular_file(directory / L"World_v1.txt.pre-waypoints-v8.sqlite3"),
                "v7_database_is_backed_up_before_v8_save");
    }
} // namespace Ashen
