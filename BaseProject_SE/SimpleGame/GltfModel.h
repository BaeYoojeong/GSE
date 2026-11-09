#pragma once
#include "MeshCache.h"
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GltfModel
{
    struct Limits
    {
        unsigned nodes = 4096, joints = 256, vertices = 250000, animations = 128, keys = 1000000;
    };

    using Vector = std::array<float, 3>;
    using Rotation = std::array<float, 4>;
    using Matrix = std::array<float, 16>;

    struct Transform
    {
        Vector translation = {0, 0, 0};
        Rotation rotation = {0, 0, 0, 1};
        Vector scale = {1, 1, 1};
    };

    struct Node
    {
        std::string name;
        int parent = -1;
        Transform bind;
        Matrix matrix;
        bool matrixOnly = false;
    };

    struct Vertex
    {
        Vector position;
        std::array<unsigned, 4> joints = {};
        std::array<float, 4> weights = {};
    };

    struct Skin
    {
        std::vector<unsigned> joints;
        std::vector<Matrix> inverseBind;
    };

    struct Primitive
    {
        unsigned node = 0;
        int skin = -1;
        std::string meshName;
        std::vector<Vertex> vertices;
        std::vector<unsigned> indices;
        std::array<float, 4> color = {1, 1, 1, 1};
        Vector emission = {};
        bool doubleSided = false;
    };
    enum class Path
    {
        Translation,
        Rotation,
        Scale
    };
    enum class Interpolation
    {
        Linear,
        Step,
        Cubic
    };

    struct Channel
    {
        unsigned node = 0;
        Path path = Path::Translation;
        Interpolation interpolation = Interpolation::Linear;
        std::vector<float> times;
        // Cubic channels retain in-tangent/value/out-tangent triples.
        std::vector<Rotation> values;
    };

    struct Clip
    {
        std::string name;
        float duration = 0;
        std::vector<Channel> channels;
    };

    struct Model
    {
        std::vector<Node> nodes;
        std::vector<unsigned> hierarchy;
        std::vector<Skin> skins;
        std::vector<Primitive> primitives;
        std::vector<Clip> clips;
        std::uint64_t revision = 0;
        std::size_t bytes = 0;
    };

    struct Pose
    {
        int clip = -1;
        float seconds = 0;
        bool loop = true;
        // Optional transition: blend local TRS before evaluating the hierarchy.
        int previousClip = -1;
        float previousSeconds = 0;
        float blend = 1;
        bool previousLoop = true;
        bool suppressRootRotation = false;
        std::string rootBone = "root";
    };

    struct Projection
    {
        float unitsPerMeter = 30;
        float yawRadians = 0;
        float maximumFootprint = 0;
        std::map<std::string, bool> meshSelection;
    };

    std::shared_ptr<const Model> Load(const std::filesystem::path& path, std::string& error,
                                      const Limits& limits = {});
    int FindClip(const Model& model, const std::string& name);
    bool Project(const Model& model, const Pose& pose, const Projection& projection,
                 std::vector<MeshCache::Vertex>& vertices);
} // namespace GltfModel
