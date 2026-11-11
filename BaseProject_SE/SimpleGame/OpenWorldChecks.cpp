#include "AcceptanceChecks.h"
#include "DarkAgeGame.h"
#include "WorldLayout.h"
#include "WorldMap.h"
#include <array>
#include <cmath>

namespace Ashen
{
    void AcceptanceChecks::OpenWorld(Game& game, const std::function<void(bool, const char*)>& require)
    {
        WorldMap::Overview();
        constexpr std::int64_t far = 1ll << 59;
        const Location remote = {{far, -far}, {217, 501}};
        WorldMap::Focus(remote);
        Vec2 point;
        require(WorldMap::Project(remote, point) &&
                    DistanceSquared(point, {WorldMap::Left + WorldMap::Width / 2.f,
                                            WorldMap::Top + WorldMap::Height / 2.f}) < .001f,
                "world_map_centers_on_large_signed_chunk_coordinates");
        Location adjacent = remote;
        ++adjacent.chunk.x;
        Vec2 neighbor;
        require(WorldMap::Project(adjacent, neighbor) && std::fabs(neighbor.x - point.x - 26) < .001f,
                "world_map_keeps_adjacent_chunks_distinct_beyond_double_integer_precision");
        WorldMap::Zoom(neighbor.x, neighbor.y, 2);
        Vec2 anchored;
        require(WorldMap::Project(adjacent, anchored) && DistanceSquared(neighbor, anchored) < .001f,
                "map_zoom_keeps_the_world_point_under_the_cursor");
        WorldMap::Zoom(point.x, point.y, -100);
        WorldMap::Zoom(point.x, point.y, -100);
        const auto beforeRegions = game.regions.size();
        const auto beforeNpcs = game.society.citizens.size();
        const auto pixels = WorldMap::BuildImage(game);
        require(pixels.size() == WorldMap::Width * WorldMap::Height * 4 &&
                    game.regions.size() == beforeRegions && game.society.citizens.size() == beforeNpcs,
                "map_preview_is_bounded_and_does_not_create_regions_or_npcs");
        const auto oldCenter = WorldMap::CurrentView().center;
        require(WorldMap::BeginDrag(point.x, point.y), "map_drag_begins_inside_the_image");
        WorldMap::Drag(point.x + 64, point.y);
        require(!WorldMap::EndDrag(point.x + 64, point.y) &&
                    WorldMap::CurrentView().center.chunk.x == oldCenter.chunk.x - 8,
                "dragging_pans_beyond_the_original_map_without_selecting_a_portal");
        WorldMap::BeginDrag(point.x, point.y);
        WorldMap::CancelDrag();
        require(!WorldMap::EndDrag(point.x, point.y), "cancelled_drag_cannot_become_a_click");
        WorldMap::Overview();

        std::array<unsigned, 4> sites = {};
        unsigned changes = 0;
        for (int y = -48; y < -12; ++y)
        {
            for (int x = 18; x < 54; ++x)
            {
                const ChunkKey key = {x, y};
                const auto site = WorldLayout::Feature(game, key);
                ++sites[static_cast<unsigned>(site)];
                changes += game.Biome(key) != game.Biome({x + 1, y}) ? 1 : 0;
            }
        }
        require(sites[1] == 36 && sites[2] == 36 && sites[3] == 36 && changes < 1296 / 3,
                "outer_world_has_distributed_sites_and_biomes_span_neighboring_chunks");
        require(WorldLayout::Road({-4, -3}, 11, 0) && !WorldLayout::Road({-3, -3}, 11, 0),
                "negative_chunk_road_grid_uses_signed_floor_modulo");

        const Location previous = game.PlayerLocation();
        const Vec2 camera = game.camera;
        const auto relocate = [&](Location location)
        {
            game.origin = location.chunk;
            game.hero->position = location.local;
            game.camera = location.local;
            game.Stream();
        };
        relocate(remote);
        game.hero->position.x += ChunkSize;
        game.Rebase();
        require(game.origin.x == remote.chunk.x + 1 && game.origin.y == remote.chunk.y &&
                    game.chunks.size() == 25 && game.hero->position.x >= 0 &&
                    game.hero->position.x < ChunkSize,
                "walking_into_a_remote_neighbor_streams_a_bounded_world_without_a_map_wall");
        bool accessible = true;
        for (Actor* base : game.scene.Snapshot())
        {
            const auto* actor = dynamic_cast<const WorldActor*>(base);
            if (actor && actor->kind == Kind::Exploration)
            {
                accessible &= !actor->solid && game.Walkable(actor->WorldPosition(), 16);
            }
        }
        require(accessible, "exploration_sites_preserve_the_open_movement_corridors");
        relocate(previous);
        game.camera = camera;
    }
} // namespace Ashen
