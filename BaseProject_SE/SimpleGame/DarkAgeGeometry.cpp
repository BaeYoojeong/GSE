#include "DarkAgeGeometry.h"
#include "DarkAgeGame.h"
#include "DarkAgeRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    using AshenRenderer::Color;
    using Vertex = MeshCache::Vertex;

    struct Point
    {
        float x;
        float y;
        float z;
    };

    const Color Stone = {.33f, .36f, .37f, 1};
    const Color Wood = {.26f, .20f, .16f, 1};
    const Color Dark = {.09f, .10f, .13f, 1};
    const Color Gold = {.98f, .64f, .25f, 1};

    Color Shade(Color color, float light)
    {
        return {color.r * light, color.g * light, color.b * light, color.a};
    }

    Vertex Project(Point point, Color color, float emission)
    {
        const float x = Ashen::CameraCos * point.x - Ashen::CameraSin * point.y;
        const float y =
            Ashen::CameraDepth * (Ashen::CameraSin * point.x + Ashen::CameraCos * point.y) - point.z;

        return {x, y, color.r, color.g, color.b, color.a, 0, 0, emission};
    }

    void Triangle(std::vector<Vertex>& vertices, Point a, Point b, Point c, Color color, float emission = 1)
    {
        vertices.push_back(Project(a, color, emission));
        vertices.push_back(Project(b, color, emission));
        vertices.push_back(Project(c, color, emission));
    }

    void Quad(std::vector<Vertex>& vertices, Point a, Point b, Point c, Point d, Color color,
              float emission = 1)
    {
        Triangle(vertices, a, b, c, color, emission);
        Triangle(vertices, a, c, d, color, emission);
    }

    void Front(std::vector<Vertex>& vertices, float x, float y, float z, float width, float height,
               Color color, float emission = 1)
    {
        Quad(vertices, {x, y, z}, {x + width, y, z}, {x + width, y, z + height}, {x, y, z + height}, color,
             emission);
    }

    void Side(std::vector<Vertex>& vertices, float x, float y, float z, float width, float height,
              Color color, float emission = 1)
    {
        Quad(vertices, {x, y, z}, {x, y + width, z}, {x, y + width, z + height}, {x, y, z + height}, color,
             emission);
    }

    void Box(std::vector<Vertex>& vertices, Point low, Point high, Color color)
    {
        Side(vertices, high.x, low.y, low.z, high.y - low.y, high.z - low.z, Shade(color, .67f));
        Front(vertices, low.x, high.y, low.z, high.x - low.x, high.z - low.z, color);
        Quad(vertices, {low.x, low.y, high.z}, {high.x, low.y, high.z}, {high.x, high.y, high.z},
             {low.x, high.y, high.z}, Shade(color, 1.24f));
    }

    struct CharacterFace
    {
        std::array<Point, 4> points;
        Color color;
        float emission;
        float depth;
    };

    class CharacterMesh
    {
      public:
        CharacterMesh(int facing, bool fallen) : dead(fallen)
        {
            const Ashen::Vec2 directions[] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
            const Ashen::Vec2 direction = Ashen::ScreenToGround(directions[facing & 3]);
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);

            // A slight shoulder turn keeps the side planes readable even when walking toward the camera.
            constexpr float cosTurn = .951056516f;
            constexpr float sinTurn = .309016994f;
            forward = {(direction.x * cosTurn - direction.y * sinTurn) / length,
                       (direction.x * sinTurn + direction.y * cosTurn) / length};
            right = {forward.y, -forward.x};
        }

        void Block(Point low, Point high, Color color, float emission = 1)
        {
            Face({{{low.x, high.y, low.z},
                   {high.x, high.y, low.z},
                   {high.x, high.y, high.z},
                   {low.x, high.y, high.z}}},
                 {0, 1, 0}, color, emission);
            Face({{{low.x, low.y, low.z},
                   {high.x, low.y, low.z},
                   {high.x, low.y, high.z},
                   {low.x, low.y, high.z}}},
                 {0, -1, 0}, color, emission);
            Face({{{high.x, low.y, low.z},
                   {high.x, high.y, low.z},
                   {high.x, high.y, high.z},
                   {high.x, low.y, high.z}}},
                 {1, 0, 0}, color, emission);
            Face({{{low.x, low.y, low.z},
                   {low.x, high.y, low.z},
                   {low.x, high.y, high.z},
                   {low.x, low.y, high.z}}},
                 {-1, 0, 0}, color, emission);
            Face({{{low.x, low.y, high.z},
                   {high.x, low.y, high.z},
                   {high.x, high.y, high.z},
                   {low.x, high.y, high.z}}},
                 {0, 0, 1}, color, emission);
            Face({{{low.x, low.y, low.z},
                   {high.x, low.y, low.z},
                   {high.x, high.y, low.z},
                   {low.x, high.y, low.z}}},
                 {0, 0, -1}, color, emission);
        }

        void AppendTo(std::vector<Vertex>& vertices)
        {
            // Sort visible body/equipment faces together so direction changes also change occlusion.
            std::stable_sort(faces.begin(), faces.end(), [](const CharacterFace& a, const CharacterFace& b)
                             { return a.depth < b.depth; });

            for (const CharacterFace& face : faces)
            {
                Quad(vertices, face.points[0], face.points[1], face.points[2], face.points[3], face.color,
                     face.emission);
            }
        }

      private:
        Point Rotate(Point point, bool normal) const
        {
            if (dead)
            {
                point = {point.x, point.z - (normal ? 0 : 32.0f), (normal ? 0 : 12.0f) - point.y};
            }

            return {right.x * point.x + forward.x * point.y, right.y * point.x + forward.y * point.y,
                    point.z};
        }

        void Face(std::array<Point, 4> points, Point normal, Color color, float emission)
        {
            const Point worldNormal = Rotate(normal, true);
            const float visibility = worldNormal.x * Ashen::CameraSin + worldNormal.y * Ashen::CameraCos +
                                     worldNormal.z * Ashen::CameraDepth;

            if (visibility <= .001f)
            {
                return;
            }

            float depth = 0;

            for (Point& point : points)
            {
                point = Rotate(point, false);
                depth +=
                    point.x * Ashen::CameraSin + point.y * Ashen::CameraCos + point.z * Ashen::CameraDepth;
            }

            const float light = worldNormal.z > .5f ? 1.18f : .76f + worldNormal.y * .20f;
            faces.push_back({points, Shade(color, emission > 1 ? 1 : light), emission, depth * .25f});
        }

        Ashen::Vec2 forward;
        Ashen::Vec2 right;
        bool dead;
        std::vector<CharacterFace> faces;
    };
} // namespace

void AshenGeometry::Building(std::vector<Vertex>& vertices, bool chapel, bool lit, unsigned variant)
{
    // The solid footprint fits the existing circular collision radius (48 / 60).
    const float halfWidth = chapel ? 44.0f : 38.0f;
    const float halfDepth = chapel ? 38.0f : 28.0f;
    const float height = chapel ? 132.0f : 88.0f;
    const float roofHeight = chapel ? 66.0f : 48.0f;
    const Color wall = Shade(Stone, 1 + variant * .025f);

    Box(vertices, {-halfWidth, -halfDepth, 0}, {halfWidth, halfDepth, height}, wall);
    Front(vertices, -halfWidth, halfDepth, 0, halfWidth * 2, 7, Dark);
    Side(vertices, halfWidth, -halfDepth, 0, halfDepth * 2, 7, Dark);

    for (int row = 0; row < static_cast<int>(height / 14); ++row)
    {
        const float z = row * 14.0f + 9;
        Front(vertices, -halfWidth, halfDepth, z, halfWidth * 2, 2, Shade(wall, .77f));
        Side(vertices, halfWidth, -halfDepth, z, halfDepth * 2, 2, Shade(wall, .48f));

        for (int col = 0; col < 4; ++col)
        {
            const float x = -halfWidth + 8 + col * 18.0f + (row % 2) * 5;

            if (x < halfWidth - 3)
            {
                Front(vertices, x, halfDepth, z - 8, 2, 8, Shade(wall, .80f));
            }
        }
    }

    Front(vertices, -13, halfDepth, 7, 26, 43, Dark);
    Triangle(vertices, {-13, halfDepth, 50}, {0, halfDepth, 63}, {13, halfDepth, 50}, Dark);
    Front(vertices, -9, halfDepth, 7, 18, 40, Wood);
    Front(vertices, 5, halfDepth, 25, 2, 3, Gold);

    for (int side : {-1, 1})
    {
        const float x = side * (halfWidth - 13) - 5;
        Front(vertices, x - 3, halfDepth, height - 39, 16, 26, Dark);
        Front(vertices, x, halfDepth, height - 36, 10, 20, lit ? Gold : Shade(wall, .45f), lit ? 3.0f : 1);
        Front(vertices, x + 4, halfDepth, height - 36, 2, 20, Wood);
    }

    Side(vertices, halfWidth, -12, height - 44, 25, 29, Dark);
    Side(vertices, halfWidth, -9, height - 41, 19, 23, lit ? Gold : Shade(wall, .40f), lit ? 2.4f : 1);
    Side(vertices, halfWidth, -1, height - 41, 3, 23, Wood);

    // A gable prism: triangular facade plus an exposed roof plane, with stepped slate strips.
    const float roofX = halfWidth + 6;
    const float roofY = halfDepth + 6;
    const Color slate = {.23f, .29f, .35f, 1};
    Triangle(vertices, {-roofX, roofY, height}, {roofX, roofY, height}, {0, roofY, height + roofHeight},
             Shade(wall, .82f));
    Quad(vertices, {-roofX, -roofY, height}, {0, -roofY, height + roofHeight},
         {0, roofY, height + roofHeight}, {-roofX, roofY, height}, Shade(slate, 1.15f));
    Quad(vertices, {0, -roofY, height + roofHeight}, {roofX, -roofY, height}, {roofX, roofY, height},
         {0, roofY, height + roofHeight}, Shade(slate, .82f));

    for (int row = 0; row < 8; ++row)
    {
        const float t = (row + .25f) / 8;
        const float next = t + .035f;
        Quad(vertices, {roofX * t, -roofY, height + roofHeight * (1 - t)},
             {roofX * next, -roofY, height + roofHeight * (1 - next)},
             {roofX * next, roofY, height + roofHeight * (1 - next)},
             {roofX * t, roofY, height + roofHeight * (1 - t)}, Shade(slate, 1.22f));
    }

    Box(vertices, {-3, -roofY, height + roofHeight - 2}, {3, roofY, height + roofHeight + 3}, slate);

    if (chapel)
    {
        Box(vertices, {-2, roofY - 5, height + roofHeight}, {2, roofY - 1, height + roofHeight + 26}, Stone);
        Box(vertices, {-10, roofY - 5, height + roofHeight + 16}, {10, roofY - 1, height + roofHeight + 20},
            Stone);
    }
}

void AshenGeometry::Rubble(std::vector<Vertex>& vertices)
{
    Box(vertices, {-11, -8, 0}, {8, 8, 17}, Stone);
    Box(vertices, {4, 1, 0}, {12, 9, 9}, Shade(Stone, .74f));
    Box(vertices, {-6, -5, 17}, {5, 4, 24}, Shade(Stone, 1.10f));
}

void AshenGeometry::Grave(std::vector<Vertex>& vertices)
{
    Box(vertices, {-17, -10, 0}, {17, 10, 5}, Shade(Stone, .70f));
    Box(vertices, {-11, -4, 5}, {11, 4, 32}, Stone);
    Front(vertices, -2, 4, 12, 4, 16, Dark);
    Front(vertices, -7, 4, 21, 14, 3, Dark);
    Box(vertices, {11, 5, 5}, {15, 9, 12}, Wood);
    Front(vertices, 11, 9, 12, 4, 5, Gold, 2.6f);
}

void AshenGeometry::Player(std::vector<Vertex>& vertices, unsigned frame, int facing, bool dead)
{
    Character(vertices, CharacterRole::Traveler, frame, facing, dead);
}

void AshenGeometry::Character(std::vector<Vertex>& vertices, CharacterRole role, unsigned frame, int facing,
                              bool dead, bool attacking)
{
    CharacterMesh mesh(facing, dead);
    Color cloak = {.28f, .43f, .46f, 1};
    Color trim = {.48f, .62f, .61f, 1};

    switch (role)
    {
    case CharacterRole::Enemy:
        cloak = {.39f, .19f, .20f, 1};
        trim = {.40f, .37f, .36f, 1};
        break;
    case CharacterRole::Settler:
        cloak = {.43f, .34f, .25f, 1};
        trim = {.61f, .52f, .36f, 1};
        break;
    case CharacterRole::Wanderer:
        cloak = {.28f, .40f, .53f, 1};
        trim = {.49f, .62f, .66f, 1};
        break;
    case CharacterRole::Guardian:
        cloak = {.46f, .28f, .26f, 1};
        trim = {.57f, .57f, .54f, 1};
        break;
    case CharacterRole::Giver:
        cloak = {.33f, .46f, .29f, 1};
        trim = {.63f, .66f, .44f, 1};
        break;
    case CharacterRole::Merchant:
        cloak = {.72f, .43f, .16f, 1};
        trim = {.96f, .76f, .32f, 1};
        break;
    case CharacterRole::Hunter:
        cloak = {.24f, .46f, .33f, 1};
        trim = {.55f, .70f, .48f, 1};
        break;
    case CharacterRole::Raider:
        cloak = {.42f, .27f, .32f, 1};
        trim = {.72f, .39f, .29f, 1};
        break;
    case CharacterRole::Cautious:
        cloak = {.35f, .29f, .46f, 1};
        trim = {.55f, .48f, .62f, 1};
        break;
    default:
        break;
    }

    const bool enemy = role == CharacterRole::Enemy;
    const bool guardian = role == CharacterRole::Guardian;
    const bool heir = role == CharacterRole::Wanderer || guardian || role == CharacterRole::Giver ||
                      role == CharacterRole::Cautious;
    const float weaponLift = !dead && attacking ? 10.0f : 0;
    const Color leather = {.25f, .20f, .17f, 1};
    const Color skin = {.69f, .56f, .44f, 1};
    const float stride[] = {0, 5, 0, -5};
    const float step = dead ? 0 : stride[frame % 4];
    const float bob = !dead && frame % 2 ? 2.0f : 0;

    for (int side : {-1, 1})
    {
        const float x = side * 6.0f;
        const float swing = step * side;
        const float lift = (std::max)(0.0f, swing * .6f);

        mesh.Block({x - 4, swing - 4, lift}, {x + 4, swing + 7, 7 + lift}, leather);
        mesh.Block({x - 3, swing - 3, 7 + lift}, {x + 3, swing + 3, 25 + bob}, Dark);

        const float arm = side * 15.0f;
        const float liftArm = side > 0 ? weaponLift : 0;
        mesh.Block({arm - 3, -swing - 4, 29 + bob + liftArm}, {arm + 3, -swing + 4, 44 + bob + liftArm},
                   cloak);
        mesh.Block({arm - 3, -swing - 3, 24 + bob + liftArm}, {arm + 3, -swing + 3, 30 + bob + liftArm},
                   skin);
    }

    // Stepped cuboids make the cloak, shoulders and hood read as a pixel-sized volume.
    mesh.Block({-12, -8, 20 + bob}, {12, 8, 29 + bob}, Shade(cloak, .85f));
    mesh.Block({-10, -7, 29 + bob}, {10, 7, 43 + bob}, cloak);
    mesh.Block({-13, -8, 40 + bob}, {13, 8, 47 + bob}, trim);
    mesh.Block({-11, -8, 29 + bob}, {11, 8, 33 + bob}, leather);
    mesh.Block({-3, 8, 29 + bob}, {3, 10, 34 + bob}, Gold);
    mesh.Block({-8, -12, 32 + bob}, {8, -8, 43 + bob}, leather);

    mesh.Block({-10, -8, 48 + bob}, {10, 8, 65 + bob}, cloak);
    mesh.Block({-8, -7, 65 + bob}, {8, 7, 69 + bob}, trim);
    mesh.Block({-7, 8, 50 + bob}, {7, 9, 62 + bob}, skin);
    mesh.Block({-12, 7, 60 + bob}, {12, 11, 65 + bob}, Shade(cloak, .75f));
    const Color eyes = enemy ? Color{.91f, .29f, .18f, 1} : Dark;
    mesh.Block({-5, 9, 56 + bob}, {-2, 10, 59 + bob}, eyes, enemy ? 2.2f : 1);
    mesh.Block({2, 9, 56 + bob}, {5, 10, 59 + bob}, eyes, enemy ? 2.2f : 1);
    mesh.Block({-2, 9, 52 + bob}, {2, 12, 56 + bob}, skin);

    if (role == CharacterRole::Traveler || role == CharacterRole::Cautious)
    {
        const float lanternY = -step;
        const float lanternZ = 11 + bob;
        mesh.Block({14, lanternY - 2, lanternZ + 12}, {16, lanternY + 2, lanternZ + 16}, leather);
        mesh.Block({11, lanternY - 4, lanternZ}, {19, lanternY + 4, lanternZ + 2}, Dark);
        mesh.Block({11, lanternY - 4, lanternZ + 10}, {19, lanternY + 4, lanternZ + 12}, Dark);
        mesh.Block({13, lanternY - 2, lanternZ + 2}, {17, lanternY + 2, lanternZ + 10}, dead ? Wood : Gold,
                   dead ? 1.0f : 4.2f);

        for (int x : {11, 18})
        {
            for (int y : {-4, 3})
            {
                mesh.Block({static_cast<float>(x), lanternY + y, lanternZ + 2},
                           {x + 1.0f, lanternY + y + 1, lanternZ + 10}, Dark);
            }
        }
    }

    if (enemy || guardian || role == CharacterRole::Hunter || role == CharacterRole::Raider)
    {
        const float swordY = -step;
        const float swordZ = 25 + bob + weaponLift;
        const Color steel = enemy ? Color{.49f, .43f, .36f, 1} : Color{.61f, .67f, .69f, 1};
        mesh.Block({14, swordY - 2, swordZ - 8}, {17, swordY + 2, swordZ}, Wood);
        mesh.Block({10, swordY - 3, swordZ}, {21, swordY + 3, swordZ + 3}, steel);
        mesh.Block({14, swordY - 1, swordZ + 3}, {17, swordY + 1, swordZ + 30}, steel);
        mesh.Block({15, swordY - 1, swordZ + 30}, {16, swordY + 1, swordZ + 34}, steel);
        mesh.Block({-14, -7, 41 + bob}, {-8, 6, 48 + bob}, steel);
        mesh.Block({8, -7, 41 + bob}, {14, 6, 48 + bob}, steel);
    }

    if (guardian)
    {
        mesh.Block({-20, step + 2, 19 + bob}, {-10, step + 5, 40 + bob}, Dark);
        mesh.Block({-19, step + 5, 21 + bob}, {-11, step + 7, 38 + bob}, trim);
        mesh.Block({-16, step + 7, 24 + bob}, {-14, step + 8, 35 + bob}, Gold);
    }

    if (role == CharacterRole::Settler || role == CharacterRole::Giver)
    {
        mesh.Block({-7, 8, 22 + bob}, {7, 9, 40 + bob}, trim);
        mesh.Block({10, -step - 4, 12 + bob}, {20, -step + 4, 24 + bob}, leather);

        if (role == CharacterRole::Giver)
        {
            mesh.Block({13, -step - 1, 24 + bob}, {15, -step + 2, 33 + bob}, {.31f, .51f, .26f, 1});
            mesh.Block({15, -step, 27 + bob}, {20, -step + 2, 31 + bob}, {.53f, .65f, .35f, 1});
        }
    }

    if (role == CharacterRole::Wanderer)
    {
        mesh.Block({-17, step - 1, 1}, {-14, step + 2, 47 + bob}, Wood);
        mesh.Block({-18, step - 2, 47 + bob}, {-13, step + 3, 51 + bob}, trim);
    }

    if (role == CharacterRole::Merchant)
    {
        // A broad gold hat and a coin satchel distinguish the shopkeeper at every facing.
        mesh.Block({-15, -11, 62 + bob}, {15, 12, 66 + bob}, trim);
        mesh.Block({-9, -7, 66 + bob}, {9, 7, 74 + bob}, cloak);
        mesh.Block({-8, -8, 68 + bob}, {8, 8, 71 + bob}, Gold);
        mesh.Block({10, -step - 5, 14 + bob}, {23, -step + 6, 29 + bob}, leather);
        mesh.Block({14, -step + 6, 20 + bob}, {20, -step + 8, 26 + bob}, Gold);
    }

    if (heir)
    {
        mesh.Block({4, 9, 36 + bob}, {8, 11, 43 + bob}, {.65f, .88f, .79f, 1}, 2.4f);
    }

    mesh.AppendTo(vertices);
}
