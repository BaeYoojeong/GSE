#include "WorldMap.h"
#include "DarkAgeGame.h"
#include "DarkAgeRenderer.h"
#include "RenderProfiler.h"
#include "WorldLayout.h"
#include <chrono>
#include <limits>
#include <array>
#include <algorithm>
#include <cmath>

namespace WorldMap
{
    namespace
    {
        View view, cachedView, dragView;
        bool dragging = false, dragged = false;
        Ashen::Vec2 dragStart;
        std::chrono::steady_clock::time_point nextBuild;

        struct Landmark
        {
            Ashen::ChunkKey chunk;
            Ashen::WorldLayout::Site site;
        };

        std::vector<Landmark> landmarks;
        unsigned texture = 0;
        std::uint64_t revision = 0;
        const AshenRenderer::Color Gold = {.91f, .70f, .40f, 1};
        const AshenRenderer::Color Cyan = {.28f, .87f, .98f, 1};

        // Chunk offsets are subtracted as integers before conversion to floating point.
        void Shift(Ashen::Location& center, double x, double y)
        {
            constexpr auto limit = (std::numeric_limits<std::int64_t>::max)() / 4 - 128;
            const auto move = [&](std::int64_t& chunk, float& local, double delta)
            {
                const double offset = local / Ashen::ChunkSize + delta;
                const auto whole = static_cast<std::int64_t>(std::floor(offset));
                chunk = (std::clamp)(chunk + whole, -limit, limit);
                local = static_cast<float>((offset - whole) * Ashen::ChunkSize);
                if (local >= Ashen::ChunkSize)
                {
                    local = 0;
                    chunk = (std::min)(limit, chunk + 1);
                }
            };
            move(center.chunk.x, center.local.x, x);
            move(center.chunk.y, center.local.y, y);
        }

        bool SameView(const View& a, const View& b)
        {
            return a.center.chunk == b.center.chunk && a.center.local.x == b.center.local.x &&
                   a.center.local.y == b.center.local.y && a.pixelsPerChunk == b.pixelsPerChunk;
        }

        struct Axis
        {
            int chunk, tile;
        };

        template <std::size_t Size> std::array<Axis, Size> Coordinates(float local)
        {
            std::array<Axis, Size> result;
            for (std::size_t pixel = 0; pixel < Size; ++pixel)
            {
                const double coordinate =
                    (static_cast<double>(pixel) + .5 - Size / 2.0) / view.pixelsPerChunk +
                    local / Ashen::ChunkSize;
                const int chunk = static_cast<int>(std::floor(coordinate));
                result[pixel] = {chunk, (std::clamp)(static_cast<int>((coordinate - chunk) * 24), 0, 23)};
            }
            return result;
        }

        void CacheLandmarks(const Ashen::Game& game)
        {
            landmarks.clear();
            const auto xs = Coordinates<Width>(view.center.local.x);
            const auto ys = Coordinates<Height>(view.center.local.y);
            for (int y = ys.front().chunk; y <= ys.back().chunk; ++y)
            {
                for (int x = xs.front().chunk; x <= xs.back().chunk; ++x)
                {
                    const Ashen::ChunkKey chunk = {view.center.chunk.x + x, view.center.chunk.y + y};
                    const auto site = Ashen::WorldLayout::Feature(game, chunk);
                    if (site != Ashen::WorldLayout::Site::None)
                    {
                        landmarks.push_back({chunk, site});
                    }
                }
            }
        }

        std::uint64_t Revision(const Ashen::Game& game)
        {
            auto value = game.Hash({}, 81273);
            for (const auto& pair : game.society.citizens)
            {
                const auto& npc = pair.second;
                for (auto part :
                     {npc.id, static_cast<std::uint64_t>(npc.village.x),
                      static_cast<std::uint64_t>(npc.village.y), static_cast<std::uint64_t>(npc.houseLot)})
                {
                    value = (value ^ part) * 1099511628211ull;
                }
            }
            return value;
        }
    } // namespace

    View CurrentView()
    {
        return view;
    }

    void Focus(const Ashen::Location& location)
    {
        CancelDrag();
        view.center = location;
    }

    void FocusPlayer(const Ashen::Game& game)
    {
        Focus(game.PlayerLocation());
    }

    void Overview()
    {
        CancelDrag();
        view = {};
    }

    void Pan(float x, float y)
    {
        if (std::isfinite(x) && std::isfinite(y))
        {
            Shift(view.center, -(std::clamp)(x, -10000.f, 10000.f) / view.pixelsPerChunk,
                  -(std::clamp)(y, -10000.f, 10000.f) / view.pixelsPerChunk);
        }
    }

    bool Contains(float x, float y)
    {
        return x >= Left && x < Left + Width && y >= Top && y < Top + Height;
    }

    void Zoom(float x, float y, double steps)
    {
        if (!Contains(x, y) || !std::isfinite(steps) || dragging)
        {
            return;
        }
        const float previous = view.pixelsPerChunk;
        view.pixelsPerChunk =
            (std::clamp)(previous * static_cast<float>(std::pow(1.25, (std::clamp)(steps, -8., 8.))), 8.f,
                         78.f);
        Shift(view.center, (x - Left - Width / 2.f) * (1.0 / previous - 1.0 / view.pixelsPerChunk),
              (y - Top - Height / 2.f) * (1.0 / previous - 1.0 / view.pixelsPerChunk));
    }

    bool BeginDrag(float x, float y)
    {
        if (!Contains(x, y))
        {
            return false;
        }
        dragging = true;
        dragged = false;
        dragStart = {x, y};
        dragView = view;
        return true;
    }

    void Drag(float x, float y)
    {
        if (!dragging)
        {
            return;
        }
        dragged |= Ashen::DistanceSquared(dragStart, {x, y}) >= 25;
        if (dragged)
        {
            view = dragView;
            Pan(x - dragStart.x, y - dragStart.y);
        }
    }

    bool EndDrag(float x, float y)
    {
        if (!dragging)
        {
            return false;
        }
        Drag(x, y);
        const bool click = !dragged && Contains(x, y);
        CancelDrag();
        return click;
    }

    void CancelDrag()
    {
        dragging = dragged = false;
    }

    bool Project(const Ashen::Location& position, Ashen::Vec2& point)
    {
        const double x = Width / 2.0 + (static_cast<double>(position.chunk.x - view.center.chunk.x) +
                                        (position.local.x - view.center.local.x) / Ashen::ChunkSize) *
                                           view.pixelsPerChunk;
        const double y = Height / 2.0 + (static_cast<double>(position.chunk.y - view.center.chunk.y) +
                                         (position.local.y - view.center.local.y) / Ashen::ChunkSize) *
                                            view.pixelsPerChunk;
        point = {Left + static_cast<float>(x), Top + static_cast<float>(y)};
        return x >= 0 && x < Width && y >= 0 && y < Height;
    }

    Ashen::Vec2 Marker(unsigned town)
    {
        Ashen::Vec2 point;
        Project(Ashen::Waypoints::Position(town), point);
        return point;
    }

    int Hit(float x, float y)
    {
        if (!Contains(x, y))
        {
            return -1;
        }
        for (unsigned town = 0; town < Ashen::TownWorld::Count; ++town)
        {
            Ashen::Vec2 point;
            if (Project(Ashen::Waypoints::Position(town), point) &&
                Ashen::DistanceSquared({x, y}, point) <= 13 * 13)
            {
                return static_cast<int>(town);
            }
        }
        return -1;
    }

    bool TravelButton(float x, float y)
    {
        return x >= 640 && x <= 894 && y >= 490 && y <= 536;
    }

    std::vector<unsigned char> BuildImage(const Ashen::Game& game)
    {
        using namespace Ashen;
        std::vector<unsigned char> pixels(Width * Height * 4);
        const std::array<std::array<int, 3>, 3> terrain = {{{55, 68, 55}, {42, 59, 53}, {73, 68, 75}}};
        const auto xs = Coordinates<Width>(view.center.local.x);
        const auto ys = Coordinates<Height>(view.center.local.y);
        const int columns = xs.back().chunk - xs.front().chunk + 1;
        const int rows = ys.back().chunk - ys.front().chunk + 1;

        struct Tile
        {
            unsigned biome, noise;
            bool village, vertical, horizontal;
        };

        std::vector<Tile> tiles(columns * rows);
        // Only classify the visible window. Previewing a faraway area creates no saved regions.
        for (int y = 0; y < rows; ++y)
        {
            for (int x = 0; x < columns; ++x)
            {
                const ChunkKey chunk = {view.center.chunk.x + xs.front().chunk + x,
                                        view.center.chunk.y + ys.front().chunk + y};
                tiles[y * columns + x] = {game.Biome(chunk), static_cast<unsigned>(game.Hash(chunk, 630)),
                                          TownWorld::At(chunk) >= 0, WorldLayout::Road(chunk, 11, 0),
                                          WorldLayout::Road(chunk, 0, 11)};
            }
        }
        for (int y = 0; y < Height; ++y)
        {
            for (int x = 0; x < Width; ++x)
            {
                const auto& tile =
                    tiles[(ys[y].chunk - ys.front().chunk) * columns + xs[x].chunk - xs.front().chunk];
                const bool road = (tile.vertical && xs[x].tile >= 10 && xs[x].tile <= 13) ||
                                  (tile.horizontal && ys[y].tile >= 10 && ys[y].tile <= 13);
                const int grain =
                    static_cast<int>((tile.noise ^ (xs[x].tile * 73856093u) ^ (ys[y].tile * 19349663u)) % 7) -
                    3;
                const auto offset = (y * Width + x) * 4;
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    const int color = road           ? (tile.village ? 123 : 104)
                                      : tile.village ? std::array<int, 3>{86, 92, 72}[channel]
                                                     : terrain[tile.biome % 3][channel];
                    pixels[offset + channel] = static_cast<unsigned char>(color + grain);
                }
                pixels[offset + 3] = 255;
            }
        }
        // Houses use their authoritative home lots, so migration changes the cached map image.
        for (const auto& pair : game.society.citizens)
        {
            const auto& npc = pair.second;
            Vec2 point;
            if (!Project(WorldSociety::Locate(npc.village, TownWorld::House(npc.houseLot)), point))
            {
                continue;
            }
            const int x = static_cast<int>(point.x - Left), y = static_cast<int>(point.y - Top);
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (x + dx < 0 || x + dx >= Width || y + dy < 0 || y + dy >= Height)
                    {
                        continue;
                    }
                    const auto offset = ((y + dy) * Width + x + dx) * 4;
                    pixels[offset] = 151;
                    pixels[offset + 1] = 113;
                    pixels[offset + 2] = 71;
                }
            }
        }
        return pixels;
    }

    void Render(const Ashen::Game& game, int selectedTown)
    {
        using namespace AshenRenderer;
        using namespace Ashen;
        const auto next = Revision(game);
        const auto now = std::chrono::steady_clock::now();
        if (!dragging && (!texture || next != revision || !SameView(view, cachedView)) && now >= nextBuild)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto pixels = BuildImage(game);
            const auto replacement = CreateImage(Width, Height, pixels);
            if (replacement)
            {
                DeleteImage(texture);
                texture = replacement;
                revision = next;
                cachedView = view;
                CacheLandmarks(game);
            }
            nextBuild = now + std::chrono::milliseconds(replacement ? 120 : 1000);
            RenderProfiler::Event(
                "world_map_image", replacement ? "cached_viewport" : "upload_failed", pixels.size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        Panel(54, 169, 892, 450, {.025f, .035f, .045f, .99f});
        Text(72, 180, L"세계 지도 · 경계 너머로 이어지는 변경", 22, Gold);
        Panel(Left, Top, Width, Height, {.08f, .12f, .10f, 1});
        Clip(Left, Top, Width, Height);
        if (texture)
        {
            // Reuse the cached bitmap while dragging; no per-frame texture upload.
            Vec2 center;
            Project(cachedView.center, center);
            const float scale = view.pixelsPerChunk / cachedView.pixelsPerChunk;
            Image(center.x - Width * scale / 2, center.y - Height * scale / 2, Width * scale, Height * scale,
                  texture);
        }
        for (const auto& landmark : landmarks)
        {
            Vec2 point;
            const Vec2 local = landmark.site == WorldLayout::Site::Camp ? Vec2{440, 440} : Vec2{384, 574};
            if (!Project({landmark.chunk, local}, point))
            {
                continue;
            }
            const Color color = landmark.site == WorldLayout::Site::Camp    ? Color{.92f, .56f, .25f, 1}
                                : landmark.site == WorldLayout::Site::Ruins ? Color{.73f, .72f, .84f, 1}
                                                                            : Color{.46f, .75f, .42f, 1};
            Panel(point.x - 2, point.y - 2, 4, 4, color);
            if (view.pixelsPerChunk >= 52)
            {
                Text(point.x + 5, point.y, WorldLayout::SiteName(landmark.site), 10, color);
            }
        }
        for (unsigned town = 0; town < TownWorld::Count; ++town)
        {
            Vec2 point;
            if (!Project(Waypoints::Position(town), point))
            {
                continue;
            }
            const bool active = game.waypoints.Active(town);
            const Color color = active ? Cyan : Color{.54f, .55f, .57f, 1};
            if (selectedTown == static_cast<int>(town))
            {
                Panel(point.x - 11, point.y - 11, 22, 22, Gold);
            }
            Panel(point.x - 8, point.y - 8, 16, 16, {.025f, .035f, .045f, 1});
            Panel(point.x - 3, point.y - 7, 6, 14, color);
            Panel(point.x - 7, point.y - 3, 14, 6, color);
            Text(point.x + 12, point.y - 6, TownWorld::Towns()[town].name, 11,
                 active ? Cyan : Color{.74f, .72f, .66f, 1});
        }
        Vec2 player;
        if (Project(game.PlayerLocation(), player))
        {
            Panel(player.x - 4, player.y - 4, 8, 8, {.98f, .88f, .38f, 1});
            Text(player.x + 6, player.y + 8, L"나", 12, Gold);
        }
        EndClip();
        Text(72, 603, L"드래그 이동 · 휠 확대/축소 · F 현재 위치 · G 일곱 마을", 12, Gold);
        Text(614, 220, L"청록: 활성 포탈 / 회색: 미활성", 14, Cyan);
        Text(614, 247, L"주황: 야영지 · 보라: 폐허 · 초록: 약초", 12, Gold);
        Text(614, 271,
             L"보기 청크 " + std::to_wstring(view.center.chunk.x) + L", " +
                 std::to_wstring(view.center.chunk.y),
             12);
        if (selectedTown >= 0 && selectedTown < static_cast<int>(TownWorld::Count))
        {
            const auto town = static_cast<unsigned>(selectedTown);
            const bool active = game.waypoints.Active(town);
            Text(614, 309, TownWorld::Towns()[town].name, 23, Gold);
            Text(614, 354, active ? L"활성화된 순간이동 포탈" : L"아직 활성화하지 않은 포탈", 16,
                 active ? Cyan : Gold);
            Text(614, 391, active ? L"아래 버튼으로 이동할 수 있습니다." : L"현장에서 E로 먼저 활성화하세요.",
                 14);
            Panel(640, 490, 254, 46, active ? Color{.11f, .37f, .42f, 1} : Color{.15f, .17f, .18f, 1});
            Text(686, 503, active ? L"순간이동" : L"미활성 포탈", 20, active ? Cyan : Gold);
        }
        else
        {
            Text(614, 321, L"지도에서 포탈을 클릭하세요.", 17, Gold);
        }
        if (!game.notice.empty())
        {
            Text(614, 547, game.notice.substr(0, 22), 12, Gold);
            if (game.notice.size() > 22)
            {
                Text(614, 563, game.notice.substr(22, 22), 12, Gold);
            }
        }
        Text(614, 580, L"클릭 / 1~7 선택 · Enter 이동", 14);
        Text(614, 603, L"M / Esc 닫기 · 시간은 계속 흐릅니다", 12);
    }

    void Shutdown()
    {
        AshenRenderer::DeleteImage(texture);
        texture = 0;
        revision = 0;
        view = {};
        landmarks.clear();
        CancelDrag();
        nextBuild = {};
    }
} // namespace WorldMap
