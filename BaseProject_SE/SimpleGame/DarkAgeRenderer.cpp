#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "RenderProfiler.h"
#include "RenderBatch.h"
#include "DiskMeshCache.h"
#include <chrono>
#include "Dependencies/glew.h"
#include "DarkAgeRenderer.h"
#include "DarkAgeGeometry.h"
#include "ChapterGeometry.h"
#include "BlenderAssets.h"
#include "AssetCatalog.h"
#include "PostProcessing.h"
#include "MeshCache.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    using namespace Ashen;
    using AshenRenderer::Color;
    using Vertex = MeshCache::Vertex;
    const Color Ink = {.055f, .065f, .085f, 1};
    const Color Stone = {.30f, .33f, .34f, 1};
    const Color StoneLight = {.45f, .45f, .41f, 1};
    const Color Wood = {.22f, .17f, .15f, 1};
    const Color Gold = {.93f, .65f, .32f, 1};
    GLuint program = 0, vao = 0, vbo = 0;
    GLint modeLocation = -1, cameraLocation = -1, pixelLocation = -1, anchorLocation = -1;
    GLint meshLocation = -1, textureLocation = -1, linearLocation = -1;
    GLint emissionScaleLocation = -1;
    GLint opacityLocation = -1;
    GLint scaleLocation = -1;
    GLint tintLocation = -1;
    GLint instancingLocation = -1, radianceLocation = -1, localLocation = -1;
    RenderBatch::Instance current;
    GLuint currentTexture = 0;
    int viewportX = 0, viewportY = 0, viewportWidth = 1000, viewportHeight = 700;
    std::vector<Vertex> vertices;
    std::uint64_t textFrame = 0;

    struct Texture
    {
        GLuint id;
        int width, height;
        std::uint64_t lastUse = 0;
    };

    std::map<std::pair<std::wstring, int>, Texture> textCache;

    Vertex V(float x, float y, Color color, float emission = 1, float u = 0, float v = 0)
    {
        return {x, y, color.r, color.g, color.b, color.a, u, v, emission};
    }

    void Tri(float x, float y, float x2, float y2, float x3, float y3, Color color, float emission = 1)
    {
        vertices.push_back(V(x, y, color, emission));
        vertices.push_back(V(x2, y2, color, emission));
        vertices.push_back(V(x3, y3, color, emission));
    }

    void Rect(float x, float y, float w, float h, Color color, float emission = 1)
    {
        Tri(x, y, x + w, y, x + w, y + h, color, emission);
        Tri(x, y, x + w, y + h, x, y + h, color, emission);
    }

    void Disc(float x, float y, float rx, float ry, Color color, float emission = 1)
    {
        for (float dy = -ry; dy < ry; dy += 4)
        {
            float t = (dy + 2) / ry;
            float half = std::floor(rx * std::sqrt((std::max)(0.0f, 1 - t * t)) / 2) * 2;

            if (half > 0)
            {
                Rect(x - half, y + dy, half * 2, (std::min)(4.0f, ry - dy), color, emission);
            }
        }
    }

    void Upload()
    {
        if (vertices.empty())
        {
            return;
        }
        RenderBatch::Flush("fallback");
        glUseProgram(program);
        glUniform1i(instancingLocation, 0);
        glUniform2f(meshLocation, current.x, current.y);
        glUniform2f(anchorLocation, current.x, current.y);
        glUniform2f(scaleLocation, current.sx, current.sy);
        glUniform2f(localLocation, current.localX, current.localY);
        glUniform4f(tintLocation, current.r, current.g, current.b, current.a);
        glUniform1i(modeLocation, static_cast<int>(current.projection));
        glUniform1f(opacityLocation, current.opacity);
        glUniform1f(emissionScaleLocation, current.emissionScale);
        glUniform1f(radianceLocation, current.radiance);
        glUniform1i(textureLocation, currentTexture ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        const auto bytes = vertices.size() * sizeof(Vertex);
        glBufferData(GL_ARRAY_BUFFER, bytes, vertices.data(), GL_STREAM_DRAW);
        RenderProfiler::Uploaded(bytes);
        RenderProfiler::Event("fallback_draw", "cache admission failed", bytes);
        RenderProfiler::DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        vertices.clear();
    }

    void Transform(Vec2 anchor, int projection)
    {
        current = {};
        current.x = anchor.x;
        current.y = anchor.y;
        current.projection = static_cast<float>(projection);
        currentTexture = 0;
    }

    template <typename Builder>
    void DrawCachedMesh(const MeshCache::Key& key, Builder build, bool persist = true)
    {
        auto mesh = MeshCache::Find(key, persist);
        if (!mesh)
        {
            vertices.clear();
            const auto start = std::chrono::steady_clock::now();
            build();
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            RenderProfiler::MeshBuilt(vertices.size(), ms);
            RenderProfiler::Event("mesh_build", DiskMeshCache::KeyName(key), vertices.size() * sizeof(Vertex),
                                  ms);
            mesh = MeshCache::Store(key, vertices, persist, persist);
            if (!mesh)
            {
                // Release leases before retrying eviction; never overwrite geometry still queued for drawing.
                RenderBatch::Flush("cache_pressure");
                mesh = MeshCache::Store(key, vertices, persist, persist);
            }
        }
        if (mesh)
        {
            RenderBatch::Submit(mesh, current, currentTexture);
            vertices.clear();
        }
        else
        {
            Upload();
        }
    }

    void Quad(float x, float y, float width, float height, Color color, float radiance = 1,
              GLuint texture = 0)
    {
        if (width <= 0 || height <= 0 || color.a <= 0)
        {
            return;
        }
        const auto saved = current;
        const auto savedTexture = currentTexture;
        current.localX = x;
        current.localY = y;
        current.sx = width;
        current.sy = height;
        current.r = color.r;
        current.g = color.g;
        current.b = color.b;
        current.a = color.a;
        current.radiance = radiance;
        currentTexture = texture;
        MeshCache::Key key = {};
        key[0] = 0x55495144;
        DrawCachedMesh(key,
                       []()
                       {
                           const Color white = {1, 1, 1, 1};
                           vertices = {V(0, 0, white, 1, 0, 0), V(1, 0, white, 1, 1, 0),
                                       V(1, 1, white, 1, 1, 1), V(0, 0, white, 1, 0, 0),
                                       V(1, 1, white, 1, 1, 1), V(0, 1, white, 1, 0, 1)};
                       });
        current = saved;
        currentTexture = savedTexture;
    }

    void UiQuad(float x, float y, float width, float height, Color color, GLuint texture = 0)
    {
        Transform({x, y}, 0);
        Quad(0, 0, width, height, color, 1, texture);
    }

    Texture MakeText(const std::wstring& s, int size)
    {
        RenderProfiler::TextBuilt();
        Texture result = {0, 0, 0};
        HDC dc = CreateCompatibleDC(NULL);

        if (!dc)
        {
            return result;
        }

        HFONT font =
            CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");

        if (!font)
        {
            DeleteDC(dc);
            return result;
        }

        HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE extent = {};
        GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &extent);
        result.width = (std::max)(1, static_cast<int>(extent.cx) + 4);
        result.height = (std::max)(size + 6, static_cast<int>(extent.cy) + 4);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = result.width;
        info.bmiHeader.biHeight = -result.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = NULL;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);

        if (bitmap)
        {
            HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
            PatBlt(dc, 0, 0, result.width, result.height, BLACKNESS);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            TextOutW(dc, 2, 0, s.c_str(), static_cast<int>(s.size()));
            GdiFlush();
            glGenTextures(1, &result.id);
            glBindTexture(GL_TEXTURE_2D, result.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result.width, result.height, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                         pixels);
            SelectObject(dc, oldBitmap);
            DeleteObject(bitmap);
        }

        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        return result;
    }

    void Ground(const Game& game, const WorldActor& actor)
    {
        unsigned biome = game.Biome(actor.chunk);
        const bool village = game.society.IsVillage(actor.chunk);

        for (int row = 0; row < 24; ++row)
        {
            for (int col = 0; col < 24; ++col)
            {
                auto h = game.Hash(actor.chunk, row * 24 + col + 9000);
                float variation = static_cast<float>(h % 5) * .009f;
                bool road = col >= 10 && col <= 13 || row >= 10 && row <= 13;
                Color color = road ? (village ? Color{.25f, .25f, .23f, 1} : Color{.22f, .23f, .18f, 1})
                              : biome == 1 ? Color{.15f, .20f, .18f, 1}
                              : biome == 2 ? Color{.23f, .22f, .23f, 1}
                                           : Color{.19f, .22f, .20f, 1};
                color.r += variation;
                color.g += variation;
                color.b += variation;
                float x = col * 32.0f, y = row * 32.0f;
                Rect(x, y, 32, 32, color);

                if (road && village)
                {
                    Rect(x + 2, y + 2, 26, 2, {.36f, .35f, .30f, 1});
                    Rect(x + (row % 2 ? 8 : 20), y + 2, 2, 22, {.18f, .20f, .19f, 1});
                }
                else if (h % 4 == 0)
                {
                    Rect(x + 8, y + 18, 4, 8, {.30f, .31f, .25f, 1});
                    Rect(x + 14, y + 22, 2, 6, {.36f, .36f, .28f, 1});
                }
                else if (biome == 1 && h % 6 == 0)
                {
                    Rect(x + 4, y + 8, 22, 10, {.13f, .20f, .23f, 1});
                    Rect(x + 8, y + 10, 12, 2, {.27f, .36f, .37f, 1});
                }
            }
        }

        if (TownWorld::Index(actor.chunk) >= 0)
        {
            Disc(384, 384, 90, 54, {.30f, .28f, .23f, 1});
            for (int i = 0; i < 6; ++i)
            {
                Rect(305 + i * 30.0f, 326, 18, 6, Stone);
                Rect(305 + i * 30.0f, 434, 18, 6, Stone);
            }
        }
        else
        {
            // Small roadside camp, without a settlement plaza or repeated town buildings.
            Disc(384, 384, 28, 18, {.24f, .22f, .17f, 1});
        }
    }

    AshenGeometry::CharacterRole HeirRole(const Memory* memory)
    {
        using AshenGeometry::CharacterRole;

        if (!memory)
        {
            return CharacterRole::Wanderer;
        }

        switch (memory->nature)
        {
        case Nature::Guardian:
            return CharacterRole::Guardian;
        case Nature::Giver:
            return CharacterRole::Giver;
        case Nature::Cautious:
            return CharacterRole::Cautious;
        default:
            return CharacterRole::Wanderer;
        }
    }

    bool CharacterAttackPose(const Game& game, const WorldActor& actor)
    {
        if (actor.kind == Kind::Enemy || actor.kind == Kind::Citizen)
        {
            return actor.windup > 0;
        }

        const Memory* memory = actor.kind == Kind::Heir ? game.MemoryAt(actor.memory) : nullptr;
        return memory && memory->nature == Nature::Guardian && actor.flash > 0;
    }

    void Model(const Game& game, const WorldActor& actor, unsigned frame)
    {
        bool lit = game.Lit(actor.chunk);

        switch (actor.kind)
        {
        case Kind::Ground:
            Ground(game, actor);
            break;
        case Kind::Residence:
            ChapterGeometry::Residence(vertices, actor.variant);
            break;
        case Kind::BellAltar:
            ChapterGeometry::Tower(vertices, game.chapter.Complete());
            break;
        case Kind::BellKeeper:
            ChapterGeometry::Keeper(vertices, actor.health <= game.chapter.BossMaximumHealth() * .5f);
            break;
        case Kind::MarketStall:
            Rect(-35, -28, 70, 28, Wood);
            Rect(-40, -66, 80, 13, {.28f, .40f, .37f, 1});
            Rect(-35, -60, 5, 65, Stone);
            Rect(30, -60, 5, 65, Stone);
            Rect(-26, -36, 13, 10, Gold);
            Rect(-4, -38, 14, 12, {.43f, .61f, .35f, 1});
            Rect(16, -32, 10, 6, Gold, .5f);
            break;
        case Kind::Citizen:
        {
            const AshenGeometry::CharacterRole roles[] = {
                AshenGeometry::CharacterRole::Hunter, AshenGeometry::CharacterRole::Raider,
                AshenGeometry::CharacterRole::Merchant, AshenGeometry::CharacterRole::Cautious};
            AshenGeometry::Character(vertices, roles[actor.variant % 4], frame, actor.facing, false,
                                     CharacterAttackPose(game, actor));
            break;
        }
        case Kind::Quarry:
            AshenGeometry::Character(vertices, AshenGeometry::CharacterRole::Enemy, 0, actor.facing);
            break;
        case Kind::House:
            AshenGeometry::Building(vertices, false, lit, actor.variant);
            break;
        case Kind::Chapel:
            AshenGeometry::Building(vertices, true, lit, actor.variant);
            break;
        case Kind::Tree:
            Rect(-8, -92, 16, 96, Ink);
            Rect(-4, -88, 8, 86, {.27f, .24f, .22f, 1});
            Rect(-24, -68, 20, 6, Wood);
            Rect(-28, -89, 6, 26, Wood);
            Rect(4, -51, 24, 6, Wood);
            Rect(24, -75, 6, 29, Wood);

            if (actor.variant % 2)
            {
                Rect(-28, -104, 46, 16, {.18f, .24f, .21f, 1});
                Rect(-20, -118, 24, 18, {.22f, .28f, .22f, 1});
            }

            break;
        case Kind::Rubble:
            AshenGeometry::Rubble(vertices);
            break;
        case Kind::Fire:
            Disc(0, 4, 28, 10, Ink);

            for (int i = -2; i <= 2; ++i)
            {
                Rect(i * 10.0f - 4, 2 + (i % 2) * 4.0f, 8, 8, StoneLight);
            }

            Rect(-19, -2, 38, 6, Wood);
            Rect(-5, -12, 10, 24, Wood);

            if (lit)
            {
                // Small stepped flame tongues are the light source. The halo exists only in post-processing.
                Rect(-14, -10, 28, 8, {.82f, .22f, .055f, 1}, 2.8f);
                Rect(-10, -20, 6, 16, {.98f, .39f, .085f, 1}, 4.0f);
                Rect(-4, -30, 8, 26, {1, .57f, .15f, 1}, 5.0f);
                Rect(4, -24, 6, 20, {.98f, .45f, .10f, 1}, 4.3f);
                Rect(-2, -38, 4, 10, Gold, 3.1f);
                Rect(-4, -16, 8, 12, {1, .80f, .39f, 1}, 4.5f);
                Rect(10, -38, 2, 3, Gold, 2.4f);
                Rect(-8, -47, 2, 2, {.98f, .43f, .12f, 1}, 2.2f);
            }

            break;
        case Kind::Projectile:
            Rect(-6, -35, 12, 8, {.39f, .68f, .94f, 1}, 2.4f);
            Rect(-2, -39, 4, 16, {.80f, .90f, 1, 1}, 3.5f);
            break;
        case Kind::Drop:
        {
            const Color colors[] = {
                {.97f, .70f, .26f, 1}, {.86f, .29f, .33f, 1}, {.31f, .68f, .96f, 1}, {.65f, .40f, .95f, 1}};
            const Color color = colors[actor.variant % 4];
            const float x = actor.variant == static_cast<unsigned>(ItemKind::Experience) ? -8.0f : 8.0f;

            switch (static_cast<ItemKind>(actor.variant))
            {
            case ItemKind::Weapon:
                Rect(x - 2, -27, 4, 22, color, 2.0f);
                Rect(x - 8, -14, 16, 4, Gold, 1.6f);
                break;
            case ItemKind::Healing:
                Rect(x - 4, -24, 8, 6, StoneLight);
                Rect(x - 7, -18, 14, 13, color, 1.8f);
                Rect(x - 1, -17, 2, 10, StoneLight);
                Rect(x - 5, -13, 10, 2, StoneLight);
                break;
            case ItemKind::Experience:
                Rect(x - 4, -25, 8, 20, color, 2.6f);
                Rect(x - 8, -21, 16, 12, color, 2.1f);
                Rect(x - 2, -23, 4, 8, {.76f, .88f, 1, 1}, 2.8f);
                break;
            case ItemKind::Magnet:
                Rect(x - 9, -25, 5, 19, color, 2.0f);
                Rect(x + 4, -25, 5, 19, color, 2.0f);
                Rect(x - 9, -10, 18, 5, color, 2.0f);
                Rect(x - 9, -27, 5, 5, StoneLight);
                Rect(x + 4, -27, 5, 5, StoneLight);
                break;
            }
        }

        break;
        case Kind::Herb:
            Rect(-10, -12, 20, 14, Wood);
            Rect(-6, -20, 12, 10, {.47f, .52f, .35f, 1});
            Rect(-12, -26, 8, 8, {.38f, .48f, .33f, 1});
            Rect(4, -28, 8, 8, {.58f, .61f, .39f, 1});
            Rect(-2, -12, 4, 10, Gold);
            break;
        case Kind::Grave:
            AshenGeometry::Grave(vertices);
            break;
        case Kind::Player:
            AshenGeometry::Player(vertices, frame, actor.facing, game.Dead());
            break;
        case Kind::Enemy:
            AshenGeometry::Character(vertices, AshenGeometry::CharacterRole::Enemy, frame, actor.facing,
                                     false, CharacterAttackPose(game, actor));
            break;
        case Kind::Settler:
            AshenGeometry::Character(vertices, AshenGeometry::CharacterRole::Settler, frame, actor.facing);
            break;
        case Kind::Heir:
            AshenGeometry::Character(vertices, HeirRole(game.MemoryAt(actor.memory)), frame, actor.facing,
                                     false, CharacterAttackPose(game, actor));
            break;
        }
    }

    bool OnScreen(const Game& game, const WorldActor& actor)
    {
        Vec2 position = actor.WorldPosition();
        const Vec2 camera = game.Camera();
        float padding = 300;

        if (actor.kind == Kind::Ground)
        {
            position.x += 384;
            position.y += 384;
            padding = 650;
        }

        const float x = CameraCos * (position.x - camera.x) - CameraSin * (position.y - camera.y) + 500;
        const float y =
            CameraDepth * (CameraSin * (position.x - camera.x) + CameraCos * (position.y - camera.y)) + 350;

        return x >= -padding && x <= 1000 + padding && y >= -padding && y <= 700 + padding;
    }

    void Shadow(const Game& game, const WorldActor& actor)
    {
        if (actor.kind == Kind::Ground || actor.kind == Kind::Fire)
        {
            return;
        }

        float radius = 16;

        switch (actor.kind)
        {
        case Kind::Residence:
            radius = 32;
            break;
        case Kind::BellAltar:
            radius = 40;
            break;
        case Kind::BellKeeper:
            radius = 24;
            break;
        case Kind::House:
            radius = 48;
            break;
        case Kind::Chapel:
            radius = 60;
            break;
        case Kind::Tree:
            radius = 22;
            break;
        case Kind::Herb:
        case Kind::Drop:
            radius = 10;
            break;
        case Kind::Projectile:
            radius = 5;
            break;
        default:
            break;
        }

        const bool fallenPlayer = actor.kind == Kind::Player && game.Dead();

        if (fallenPlayer)
        {
            radius = 30;
        }

        // Contact shadows belong to the ground plane, not to the upright sprite plane.
        Transform(actor.WorldPosition(), 0);
        current.emissionScale = 1;
        current.opacity = 1;

        MeshCache::Key key = {};
        key[0] = 0x53484145;
        key[1] = static_cast<unsigned>(radius);
        DrawCachedMesh(key,
                       [radius]()
                       {
                           Disc(8, 6, radius * 1.25f, radius, {.015f, .020f, .028f, .18f});
                           Disc(0, 0, radius, radius * .8f, {.015f, .020f, .028f, .22f});
                       });
    }

    float OcclusionOpacity(const Game& game, const WorldActor& actor)
    {
        if (game.Dead() ||
            (actor.kind != Kind::House && actor.kind != Kind::Chapel && actor.kind != Kind::Tree &&
             actor.kind != Kind::Residence && actor.kind != Kind::BellAltar))
        {
            return 1;
        }

        const Vec2 position = actor.WorldPosition();
        const Vec2 hero = game.Hero().WorldPosition();
        const float dx = CameraCos * (hero.x - position.x) - CameraSin * (hero.y - position.y);
        const float dy =
            CameraDepth * (CameraSin * (hero.x - position.x) + CameraCos * (hero.y - position.y));
        const float height = actor.kind == Kind::Residence   ? 90.0f
                             : actor.kind == Kind::BellAltar ? 150.0f
                             : actor.kind == Kind::Chapel    ? 232.0f
                                                             : (actor.kind == Kind::House ? 150.0f : 124.0f);
        const float width = actor.kind == Kind::Tree ? 38.0f : 76.0f;

        return dy <= 0 && dy >= -height && std::fabs(dx) < width ? .38f : 1.0f;
    }

    MeshCache::Key ModelKey(const Game& game, const WorldActor& actor, unsigned frame)
    {
        MeshCache::Key key = {};
        key[0] = 0x41534849;
        key[1] = static_cast<unsigned>(actor.kind);

        // Include only inputs read by Model/Character. Position, time and opacity remain uniforms.
        switch (actor.kind)
        {
        case Kind::Residence:
            key[2] = actor.variant % 4;
            key[9] = BlenderAssets::Revision("cottage_" + std::to_string(actor.variant % 4));
            break;
        case Kind::BellAltar:
            key[2] = game.chapter.Complete() ? 1 : 0;
            key[9] = BlenderAssets::Revision(game.chapter.Complete() ? "tower_lit" : "tower_dark");
            break;
        case Kind::BellKeeper:
            key[2] = actor.health <= game.chapter.BossMaximumHealth() * .5f ? 1 : 0;
            key[9] = BlenderAssets::Revision(key[2] ? "keeper_enraged" : "keeper_dark");
            break;
        case Kind::Ground:
        {
            const auto hash = game.Hash(actor.chunk);
            key[5] = static_cast<unsigned>(hash);
            key[6] = static_cast<unsigned>(hash >> 32);
            key[7] = game.Biome(actor.chunk);
            key[8] = TownWorld::Index(actor.chunk) >= 0 ? 2 : game.society.IsVillage(actor.chunk) ? 1 : 0;
            key[9] = 3; // Ground geometry now distinguishes wilderness and settlements.
            break;
        }
        case Kind::House:
        case Kind::Chapel:
            key[2] = actor.variant;
            key[3] = game.Lit(actor.chunk) ? 1 : 0;
            break;
        case Kind::Fire:
            key[3] = game.Lit(actor.chunk) ? 1 : 0;
            break;
        case Kind::Tree:
            key[2] = actor.variant % 2;
            break;
        case Kind::Drop:
            key[2] = actor.variant;
            break;
        case Kind::Player:
            key[4] = frame;
            key[5] = actor.facing;
            key[6] = game.Dead() ? 1 : 0;
            break;
        case Kind::Citizen:
        case Kind::Quarry:
        case Kind::Enemy:
        case Kind::Settler:
        case Kind::Heir:
        {
            const Memory* memory = actor.kind == Kind::Heir ? game.MemoryAt(actor.memory) : nullptr;
            key[2] = actor.kind == Kind::Citizen ? actor.variant : 0;
            // Invalidate persisted citizen meshes after the profession-model change.
            key[9] = actor.kind == Kind::Citizen ? 1 : 0;
            key[4] = frame;
            key[5] = memory ? static_cast<unsigned>(memory->nature) : 0;
            key[7] = static_cast<unsigned>(actor.facing);
            key[8] = CharacterAttackPose(game, actor) ? 1 : 0;
            break;
        }
        default:
            break;
        }

        return key;
    }

    void CitizenAttackEffect(const WorldActor& actor)
    {
        if (actor.kind != Kind::Citizen || actor.attackVisualSeconds <= 0)
        {
            return;
        }
        Transform(actor.WorldPosition(), 1);
        const Vec2 delta = {actor.attackVisualTarget.x - actor.position.x,
                            actor.attackVisualTarget.y - actor.position.y};
        const Vec2 end = {CameraCos * delta.x - CameraSin * delta.y,
                          CameraDepth * (CameraSin * delta.x + CameraCos * delta.y)};
        const float progress =
            (std::clamp)(1 - actor.attackVisualSeconds / WorldSociety::AttackVisualDuration, 0.0f, 1.0f);
        const Color color = actor.variant == static_cast<unsigned>(Profession::Raider)
                                ? Color{1, .40f, .24f, 1}
                                : Color{.60f, .91f, 1, 1};
        // Six instances of the shared unit quad show direction without rebuilding a mesh.
        const float head = (std::min)(1.0f, progress * 3);
        for (int i = 0; i < 6; ++i)
        {
            const float t = head - i * .10f;
            if (t < 0)
            {
                continue;
            }
            Color trail = color;
            trail.a = (1 - progress) * (1 - i * .12f);
            Quad(std::round(end.x * t / 2) * 2 - 3, std::round(end.y * t / 2) * 2 - 39, 6, 6, trail, 1.7f);
        }
        if (progress >= .25f)
        {
            Color impact = color;
            impact.a = (1 - progress) * 1.2f;
            const float spread = 4 + progress * 12;
            Quad(end.x - spread, end.y - 37, spread * 2, 4, impact, 2);
            Quad(end.x - 2, end.y - 35 - spread, 4, spread * 2, impact, 2);
        }
    }

    void BossAttackEffect(const WorldActor& actor)
    {
        if (actor.kind != Kind::BellKeeper || (actor.windup <= 0 && actor.attackVisualSeconds <= 0))
        {
            return;
        }
        Vec2 anchor = actor.WorldPosition();
        anchor.x += actor.attackVisualTarget.x - actor.position.x;
        anchor.y += actor.attackVisualTarget.y - actor.position.y;
        Transform(anchor, 0);
        const Color color = actor.windup > 0 ? Color{1, .18f, .08f, .8f} : Color{1, .8f, .3f, .9f};
        // Shared unit-quad instances; no transient circle mesh.
        for (unsigned i = 0; i < 40; ++i)
        {
            const float angle = static_cast<float>(i) * .1570796327f;
            Quad(std::cos(angle) * actor.remainingRange - 3, std::sin(angle) * actor.remainingRange - 3, 6, 6,
                 color, 1.5f);
        }
        Quad(-12, -2, 24, 4, color, 1.5f);
        Quad(-2, -12, 4, 24, color, 1.5f);
    }

    AssetCatalog::Prepared ImportedModel(const Game& game, const WorldActor& actor)
    {
        AssetCatalog::Request request;
        request.seconds = actor.motionSeconds;
        request.clip = AssetCatalog::ClipName(WorldActor::MotionName(actor.motion));
        request.loop = WorldActor::Loops(actor.motion);
        request.previousClip = AssetCatalog::ClipName(WorldActor::MotionName(actor.previousMotion));
        request.previousSeconds = actor.previousMotionSeconds;
        request.previousLoop = WorldActor::Loops(actor.previousMotion);
        request.blend = (std::min)(1.f, actor.blendSeconds / .12f);
        request.suppressRootRotation =
            actor.motion == WorldActor::Motion::TurnLeft || actor.motion == WorldActor::Motion::TurnRight ||
            (request.blend < 1 && (actor.previousMotion == WorldActor::Motion::TurnLeft ||
                                   actor.previousMotion == WorldActor::Motion::TurnRight));
        request.yawRadians = std::round(actor.visualYaw / .3926990817f) * .3926990817f;
        switch (actor.kind)
        {
        case Kind::Player:
            if (game.Dead())
            {
                return {};
            }
            request.asset = AssetCatalog::RoleAsset("citizen", actor.memory);
            request.asset = AssetCatalog::RoleAsset("player", game.life.id);
            request.parts = AssetCatalog::NpcParts(game.life.id, 2);
            break;
        case Kind::Citizen:
        {
            const auto* npc = game.society.Find(actor.memory);
            if (!npc)
            {
                return {};
            }
            request.asset = AssetCatalog::RoleAsset("citizen", actor.memory);
            const unsigned outfit = npc->profession == Profession::Hunter   ? 2
                                    : npc->profession == Profession::Raider ? 1
                                                                            : 0;
            request.parts = AssetCatalog::NpcParts(npc->id, outfit);
            if (actor.moving && npc->activity == Activity::Raid)
            {
                request.clip = AssetCatalog::ClipName("run");
            }
            break;
        }
        case Kind::Settler:
        case Kind::Heir:
            request.asset = AssetCatalog::RoleAsset("citizen", actor.memory);
            request.parts = AssetCatalog::NpcParts(actor.memory, 0);
            break;
        case Kind::Enemy:
        case Kind::Quarry:
        {
            request.asset = AssetCatalog::RoleAsset("enemy", game.Hash(actor.chunk, 410 + actor.slot));
            request.maximumFootprint = 70;
            break;
        }
        case Kind::Residence:
        case Kind::House:
            request.asset =
                AssetCatalog::RoleAsset(actor.kind == Kind::Residence ? "residence" : "house", actor.memory);
            request.maximumFootprint = actor.kind == Kind::Residence ? 64.0f : 96.0f;
            // Cottage doors/windows face glTF +Z, away from the default camera.
            request.yawRadians = 3.141592654f;
            break;
        case Kind::Chapel:
            request.asset = AssetCatalog::RoleAsset("chapel", 0);
            request.maximumFootprint = 120;
            request.yawRadians = 0;
            break;
        case Kind::Tree:
            request.asset = AssetCatalog::RoleAsset("tree", actor.variant);
            request.maximumFootprint = 48;
            request.yawRadians = 0;
            break;
        case Kind::Rubble:
            request.asset = AssetCatalog::RoleAsset("rubble", actor.variant);
            request.maximumFootprint = 30;
            request.yawRadians = 0;
            break;
        case Kind::Herb:
            request.asset = AssetCatalog::RoleAsset("herb", 0);
            request.maximumFootprint = 26;
            request.yawRadians = 0;
            break;
        default:
            return {};
        }
        return AssetCatalog::Prepare(request);
    }

    void Draw(const Game& game, const WorldActor& actor)
    {
        Vec2 anchor = actor.WorldPosition();
        const int projection = actor.kind == Kind::Ground ? 0 : 1;

        if (actor.kind == Kind::Drop)
        {
            anchor.y -= 3 + std::sin(game.elapsed * 3 + actor.slot) * 2;
        }

        Transform(anchor, projection);
        current.opacity = OcclusionOpacity(game, actor);
        float emissionScale = 1;

        if (actor.kind == Kind::Fire || actor.kind == Kind::Player)
        {
            const float phase = static_cast<float>(game.Hash(actor.chunk, 190 + actor.slot) % 97);
            const float flicker = std::sin(game.elapsed * 4.3f + phase) * .08f +
                                  std::sin(game.elapsed * 9.7f + phase * 1.7f) * .035f;
            emissionScale += actor.kind == Kind::Fire ? flicker : flicker * .4f;
        }

        // Animate only emission. Cached geometry and the non-emissive surfaces stay stable.
        current.emissionScale = emissionScale;
        const bool npc = actor.kind == Kind::Enemy || actor.kind == Kind::Settler ||
                         actor.kind == Kind::Heir || actor.kind == Kind::Citizen;
        const unsigned frame = actor.kind == Kind::Player
                                   ? (actor.moving ? static_cast<unsigned>(game.elapsed * 9) % 4 : 0)
                                   : (npc && actor.moving && (!game.Dead() || actor.kind == Kind::Citizen)
                                          ? static_cast<unsigned>(actor.walkCycle) % 4
                                          : 0);
        const auto imported = ImportedModel(game, actor);
        if (imported)
        {
            DrawCachedMesh(
                imported.key,
                [&]()
                {
                    if (!GltfModel::Project(*imported.model, imported.pose, imported.projection, vertices))
                    {
                        vertices.clear();
                        Model(game, actor, frame);
                        RenderProfiler::Event("gltf_projection_fallback",
                                              "asset geometry could not be projected");
                    }
                },
                !imported.animated);
        }
        else
        {
            DrawCachedMesh(ModelKey(game, actor, frame), [&]() { Model(game, actor, frame); });
        }

        if (actor.kind == Kind::Citizen || actor.kind == Kind::Quarry)
        {
            const auto* citizen = actor.kind == Kind::Citizen ? game.society.Find(actor.memory) : nullptr;
            const float maximum = citizen ? citizen->maximumHealth : HuntingGround::MaximumHealth;
            const Color jobColors[] = {
                {.35f, .72f, .46f, 1}, {.9f, .28f, .24f, 1}, {.95f, .76f, .28f, 1}, {.48f, .60f, .94f, 1}};
            const Color tint =
                citizen ? jobColors[static_cast<unsigned>(citizen->profession)] : Color{.7f, .3f, .3f, 1};
            Quad(-21, -87, 42, 4, Ink);
            Quad(-21, -87, 42 * (std::max)(0.0f, actor.health) / maximum, 4, tint);
            if (citizen)
            {
                if (citizen->journeyKind != JourneyKind::None)
                {
                    const Color travel = citizen->journeyKind == JourneyKind::Migration
                                             ? Color{.4f, 1, .65f, 1}
                                             : Color{.75f, .88f, 1, 1};
                    Quad(24, -105, 18, 12, travel, .6f);
                    Quad(27, -102, 12, 2, Ink);
                    Quad(27, -98, 8, 2, Ink);
                }
                if (citizen->raidResultSeconds > 0)
                {
                    // A confirmed theft leaves a visible gold marker for four seconds.
                    Quad(25, -105, 16, 14, Ink);
                    Quad(29, -103, 8, 10, Gold, 1.5f);
                    Quad(26, -100, 14, 4, Gold, 1.5f);
                }
                else if (citizen->intention == Activity::Raid && citizen->raidCooldown <= 0)
                {
                    const Color pursuit = {1, .32f, .18f, 1};
                    Quad(29, -106, 4, 12, pursuit, 1.5f);
                    Quad(29, -91, 4, 4, pursuit, 1.5f);
                }
                if (citizen->profession == Profession::Merchant)
                {
                    // Pixel coin icon: the gold hat identifies the body, the badge identifies service.
                    const bool open =
                        !game.society.IsHostile(citizen->id) && game.society.IsAtMarket(*citizen);
                    Quad(-10, -112, 20, 18, Ink);
                    Quad(-6, -111, 12, 16, tint, 1.3f);
                    Quad(-8, -108, 16, 10, tint, 1.3f);
                    Quad(-2, -108, 4, 10, Ink);
                    Quad(-10, -92, 20, 3, open ? Color{.4f, .85f, .6f, 1} : Stone);
                }
                else
                {
                    Quad(-5, -99, 10, 6, tint, .4f);
                }
            }
            if (actor.windup > 0)
            {
                Quad(-2, -115, 4, 10, Gold, 2);
            }
        }

        if (actor.kind == Kind::BellKeeper)
        {
            Quad(-36, -114, 72, 6, Ink);
            Quad(-36, -114, 72 * (std::clamp)(actor.health / game.chapter.BossMaximumHealth(), 0.0f, 1.0f), 6,
                 {.8f, .2f, .15f, 1});
        }
        if (actor.kind == Kind::Enemy)
        {
            Quad(-21, -82, 42, 4, Ink);
            Quad(-21, -82, 42 * (std::max)(0.0f, actor.health) / Game::EnemyMaximumHealth, 4,
                 {.69f, .27f, .23f, 1});

            if (actor.windup > 0)
            {
                Quad(-3, -104, 6, 13, Gold, 2.5f);
                Quad(-3, -87, 6, 4, Gold, 2.5f);
            }
        }

        if (actor.flash > 0)
        {
            Quad(-28, -40, 56, 4, {.91f, .83f, .62f, .75f}, 2);
            Quad(22, -44, 4, 12, Gold, 2);
        }
    }
} // namespace

bool AshenRenderer::Initialize()
{
    RenderProfiler::Initialize();
    program = ShaderProgram::Load(L"World.vs", L"World.fs");

    if (!program)
    {
        return false;
    }

    if (!MeshCache::Initialize() || !RenderBatch::Initialize(program))
    {
        RenderProfiler::Event("renderer_init", "atlas or batch allocation failed");
        return false;
    }
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    MeshCache::BindVertexLayout();
    modeLocation = glGetUniformLocation(program, "objectProjection");
    cameraLocation = glGetUniformLocation(program, "cameraOffset");
    pixelLocation = glGetUniformLocation(program, "pixelSize");
    anchorLocation = glGetUniformLocation(program, "objectAnchor");
    meshLocation = glGetUniformLocation(program, "meshOffset");
    textureLocation = glGetUniformLocation(program, "useTexture");
    linearLocation = glGetUniformLocation(program, "linearOutput");
    emissionScaleLocation = glGetUniformLocation(program, "emissionScale");
    opacityLocation = glGetUniformLocation(program, "objectOpacity");
    scaleLocation = glGetUniformLocation(program, "meshScale");
    tintLocation = glGetUniformLocation(program, "meshTint");
    instancingLocation = glGetUniformLocation(program, "useInstancing");
    radianceLocation = glGetUniformLocation(program, "meshRadiance");
    localLocation = glGetUniformLocation(program, "meshLocalOffset");
    glUseProgram(program);
    glUniform3f(glGetUniformLocation(program, "cameraBasis"), CameraCos, CameraSin, CameraDepth);
    glUniform1f(glGetUniformLocation(program, "fade"), 1);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    current.emissionScale = 1;
    current.opacity = 1;
    PostProcessing::Initialize();
    PostProcessing::Reset();
    return true;
}

void AshenRenderer::Resize(int width, int height)
{
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    float scale = (std::min)(width / 1000.0f, height / 700.0f);
    viewportWidth = (std::max)(1, static_cast<int>(1000 * scale));
    viewportHeight = (std::max)(1, static_cast<int>(700 * scale));

    if (width >= 500 && height >= 350)
    {
        int pixels = (std::min)(width / 500, height / 350);
        viewportWidth = pixels * 500;
        viewportHeight = pixels * 350;
    }

    viewportX = (width - viewportWidth) / 2;
    viewportY = (height - viewportHeight) / 2;
    PostProcessing::Resize(500, 350);
}

void AshenRenderer::World(const Game& game)
{
    DiskMeshCache::Poll();
    AssetCatalog::Poll();
    ++textFrame;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
    glClearColor(.035f, .045f, .06f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    bool hdr = PostProcessing::BeginScene();
    glUseProgram(program);
    Vec2 camera = game.Camera();
    glUniform2f(cameraLocation, camera.x, camera.y);
    glUniform1f(pixelLocation, 2);
    glUniform1i(linearLocation, hdr ? 1 : 0);
    std::vector<const WorldActor*> actors;
    for (const Actor* base : game.Scene().RenderSnapshot({CameraSin, CameraCos}))
    {
        const auto* actor = dynamic_cast<const WorldActor*>(base);
        if (!actor)
        {
            continue;
        }
        const bool visible = OnScreen(game, *actor);
        RenderProfiler::Visibility(visible);
        if (visible)
        {
            actors.push_back(actor);
        }
    }

    // Preserve painter order within each pass; never regroup translucent objects by material.
    const RenderProfiler::Pass passes[] = {RenderProfiler::Pass::Ground, RenderProfiler::Pass::Shadow,
                                           RenderProfiler::Pass::World};
    for (int pass = 0; pass < 3; ++pass)
    {
        RenderProfiler::SetPass(passes[pass]);
        for (const WorldActor* actor : actors)
        {
            if (pass == 1)
            {
                Shadow(game, *actor);
            }
            else if ((pass == 0) == (actor->kind == Kind::Ground))
            {
                Draw(game, *actor);
            }
        }
        RenderBatch::Flush("pass_end");
    }

    // Draw combat cues after world geometry so the target's model cannot hide the hit.
    for (const WorldActor* actor : actors)
    {
        CitizenAttackEffect(*actor);
        BossAttackEffect(*actor);
    }
    current.opacity = 1;
    current.emissionScale = 1;

    if (!game.Dead() && game.dialogue.empty())
    {
        if (const WorldActor* target = game.AimTarget())
        {
            Transform(target->WorldPosition(), 1);
            Quad(-28, -72, 10, 3, {.43f, .76f, .95f, 1}, 1.5f);
            Quad(18, -72, 10, 3, {.43f, .76f, .95f, 1}, 1.5f);
            Quad(-28, -72, 3, 12, {.43f, .76f, .95f, 1}, 1.5f);
            Quad(25, -72, 3, 12, {.43f, .76f, .95f, 1}, 1.5f);
        }

        const WorldActor* nearest = game.Nearest();

        if (nearest)
        {
            Transform(nearest->WorldPosition(), 1);
            Quad(-25, 6, 12, 3, Gold);
            Quad(13, 6, 12, 3, Gold);
        }
    }

    RenderBatch::Flush("before_post");
    RenderProfiler::SetPass(RenderProfiler::Pass::Post);
    if (hdr)
    {
        PostProcessing::EndScene(viewportX, viewportY, viewportWidth, viewportHeight, 1);
    }

    glUseProgram(program);
    RenderProfiler::SetPass(RenderProfiler::Pass::UI);
    glUniform1i(linearLocation, 0);
    current.opacity = 1;
    current.emissionScale = 1;
    glUniform1f(pixelLocation, 0);
    Transform({}, 0);
    glActiveTexture(GL_TEXTURE0);
    // Sparse drifting ash gives motion without obscuring the readable pixel silhouettes.
    for (int i = 0; i < 30; ++i)
    {
        float x = std::fmod(i * 73.0f + game.elapsed * (3 + i % 4), 1000.0f);
        float y = std::fmod(i * 127.0f + game.elapsed * 9, 700.0f);
        Quad(x, y, 2, 2, {.68f, .66f, .57f, .18f});
    }
}

void AshenRenderer::Panel(float x, float y, float width, float height, Color color)
{
    UiQuad(x, y, width, height, color);
}

void AshenRenderer::Text(float x, float y, const std::wstring& text, int size, Color color)
{
    if (text.empty())
    {
        return;
    }

    auto key = std::make_pair(text, size);
    auto found = textCache.find(key);

    if (found == textCache.end())
    {
        if (textCache.size() >= 192)
        {
            auto oldest =
                std::min_element(textCache.begin(), textCache.end(), [](const auto& a, const auto& b)
                                 { return a.second.lastUse < b.second.lastUse; });
            RenderBatch::Flush("text_eviction");
            RenderProfiler::Event("text_evict", "text texture cache limit=192");
            glDeleteTextures(1, &oldest->second.id);
            textCache.erase(oldest);
        }

        found = textCache.emplace(key, MakeText(text, size)).first;
    }

    found->second.lastUse = textFrame;
    const Texture& texture = found->second;

    if (!texture.id)
    {
        return;
    }

    UiQuad(x, y, static_cast<float>(texture.width), static_cast<float>(texture.height), color, texture.id);
}

void AshenRenderer::EndFrame()
{
    RenderBatch::Flush("frame_end");
    RenderProfiler::SetPass(RenderProfiler::Pass::Other);
}

void AshenRenderer::Shutdown()
{
    AssetCatalog::Shutdown();
    BlenderAssets::Clear();
    RenderBatch::Shutdown();
    for (const auto& pair : textCache)
    {
        glDeleteTextures(1, &pair.second.id);
    }

    textCache.clear();
    MeshCache::Shutdown();
    PostProcessing::Shutdown();

    if (vbo)
    {
        glDeleteBuffers(1, &vbo);
    }

    if (vao)
    {
        glDeleteVertexArrays(1, &vao);
    }

    if (program)
    {
        glDeleteProgram(program);
    }

    program = vao = vbo = 0;
    RenderProfiler::Shutdown();
}
