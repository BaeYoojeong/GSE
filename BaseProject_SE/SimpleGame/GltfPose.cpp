#include "GltfModel.h"
#include "GltfMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace GltfModel
{
    namespace
    {
        Rotation Sample(const Channel& channel, float time)
        {
            const size_t multiplier = channel.interpolation == Interpolation::Cubic ? 3 : 1;
            const size_t valueOffset = multiplier == 3 ? 1 : 0;
            if (time <= channel.times.front())
            {
                return channel.values[valueOffset];
            }
            if (time >= channel.times.back())
            {
                return channel.values[(channel.times.size() - 1) * multiplier + valueOffset];
            }
            const size_t end = static_cast<size_t>(
                std::upper_bound(channel.times.begin(), channel.times.end(), time) - channel.times.begin());
            const size_t start = end - 1;
            const auto a = channel.values[start * multiplier + valueOffset];
            const auto b = channel.values[end * multiplier + valueOffset];
            if (channel.interpolation == Interpolation::Step)
            {
                return a;
            }
            const float span = channel.times[end] - channel.times[start];
            const float t = (time - channel.times[start]) / span;
            if (channel.interpolation == Interpolation::Linear && channel.path == Path::Rotation)
            {
                return Slerp(a, b, t);
            }
            Rotation result = {};
            for (unsigned i = 0; i < 4; ++i)
            {
                if (multiplier == 3)
                {
                    const float t2 = t * t, t3 = t2 * t;
                    result[i] = (2 * t3 - 3 * t2 + 1) * a[i] +
                                (t3 - 2 * t2 + t) * span * channel.values[start * 3 + 2][i] +
                                (-2 * t3 + 3 * t2) * b[i] + (t3 - t2) * span * channel.values[end * 3][i];
                }
                else
                {
                    result[i] = a[i] + (b[i] - a[i]) * t;
                }
            }
            return channel.path == Path::Rotation ? Normalize(result) : result;
        }

        void Apply(const Model& model, int index, float seconds, bool loop, std::vector<Transform>& pose)
        {
            if (index < 0 || static_cast<size_t>(index) >= model.clips.size())
            {
                return;
            }
            const auto& clip = model.clips[index];
            float time = (std::max)(0.0f, seconds);
            time =
                loop && clip.duration > 0 ? std::fmod(time, clip.duration) : (std::min)(time, clip.duration);
            for (const auto& channel : clip.channels)
            {
                const auto sampled = Sample(channel, time);
                auto& target = pose[channel.node];
                if (channel.path == Path::Rotation)
                {
                    target.rotation = Normalize(sampled);
                }
                else
                {
                    auto& value = channel.path == Path::Translation ? target.translation : target.scale;
                    std::copy_n(sampled.begin(), 3, value.begin());
                }
            }
        }

        std::vector<Matrix> Globals(const Model& model, const Pose& request)
        {
            std::vector<Transform> pose;
            pose.reserve(model.nodes.size());
            for (const auto& node : model.nodes)
            {
                pose.push_back(node.bind);
            }
            Apply(model, request.clip, request.seconds, request.loop, pose);
            if (request.previousClip >= 0 && request.blend < 1)
            {
                std::vector<Transform> previous;
                for (const auto& node : model.nodes)
                {
                    previous.push_back(node.bind);
                }
                Apply(model, request.previousClip, request.previousSeconds, request.previousLoop, previous);
                const float blend = (std::clamp)(request.blend, 0.0f, 1.0f);
                for (unsigned node = 0; node < pose.size(); ++node)
                {
                    pose[node].rotation = Slerp(previous[node].rotation, pose[node].rotation, blend);
                    for (unsigned i = 0; i < 3; ++i)
                    {
                        pose[node].translation[i] =
                            previous[node].translation[i] +
                            (pose[node].translation[i] - previous[node].translation[i]) * blend;
                        pose[node].scale[i] =
                            previous[node].scale[i] + (pose[node].scale[i] - previous[node].scale[i]) * blend;
                    }
                }
            }
            if (request.suppressRootRotation)
            {
                for (size_t i = 0; i < model.nodes.size(); ++i)
                {
                    if (model.nodes[i].name == request.rootBone)
                    {
                        pose[i].rotation = model.nodes[i].bind.rotation;
                    }
                }
            }
            std::vector<Matrix> global(model.nodes.size());
            for (unsigned index : model.hierarchy)
            {
                const auto& node = model.nodes[index];
                const Matrix local = node.matrixOnly ? node.matrix : Compose(pose[index]);
                global[index] = node.parent < 0 ? local : Multiply(global[node.parent], local);
            }
            return global;
        }

        std::vector<std::vector<Matrix>> SkinMatrices(const Model& model, const std::vector<Matrix>& global)
        {
            std::vector<std::vector<Matrix>> result;
            for (const auto& skin : model.skins)
            {
                std::vector<Matrix> joints;
                for (unsigned i = 0; i < skin.joints.size(); ++i)
                {
                    joints.push_back(Multiply(global[skin.joints[i]], skin.inverseBind[i]));
                }
                result.push_back(std::move(joints));
            }
            return result;
        }

        Vector Deform(const Vertex& vertex, const Primitive& primitive, const std::vector<Matrix>& global,
                      const std::vector<std::vector<Matrix>>& skins)
        {
            if (primitive.skin < 0)
            {
                return Point(global[primitive.node], vertex.position);
            }
            Vector result = {};
            for (unsigned joint = 0; joint < 4; ++joint)
            {
                if (vertex.weights[joint] <= 0)
                {
                    continue;
                }
                const auto point = Point(skins[primitive.skin][vertex.joints[joint]], vertex.position);
                for (unsigned i = 0; i < 3; ++i)
                {
                    result[i] += point[i] * vertex.weights[joint];
                }
            }
            return result;
        }

        bool Selected(const Primitive& primitive, const Projection& projection)
        {
            const auto found = projection.meshSelection.find(primitive.meshName);
            return projection.meshSelection.empty() ||
                   (found != projection.meshSelection.end() && found->second);
        }

        float Srgb(float value)
        {
            value = (std::clamp)(value, 0.0f, 1.0f);
            return value <= .0031308f ? 12.92f * value : 1.055f * std::pow(value, 1 / 2.4f) - .055f;
        }
    } // namespace

    void ComputeRestBounds(Model& model)
    {
        const auto global = Globals(model, {});
        const auto skins = SkinMatrices(model, global);
        for (auto& primitive : model.primitives)
        {
            primitive.restLow = {1e30f, 1e30f, 1e30f};
            primitive.restHigh = {-1e30f, -1e30f, -1e30f};
            for (const auto& vertex : primitive.vertices)
            {
                const auto point = Deform(vertex, primitive, global, skins);
                for (unsigned i = 0; i < 3; ++i)
                {
                    primitive.restLow[i] = (std::min)(primitive.restLow[i], point[i]);
                    primitive.restHigh[i] = (std::max)(primitive.restHigh[i], point[i]);
                }
            }
            primitive.restBoundsValid = !primitive.vertices.empty();
        }
    }

    bool Project(const Model& model, const Pose& pose, const Projection& projection,
                 std::vector<MeshCache::Vertex>& vertices)
    {
        if (!std::isfinite(projection.unitsPerMeter) || projection.unitsPerMeter <= 0 ||
            !std::isfinite(projection.yawRadians) || !std::isfinite(projection.maximumFootprint) ||
            projection.maximumFootprint < 0 || !std::isfinite(pose.seconds) ||
            !std::isfinite(pose.previousSeconds) || !std::isfinite(pose.blend))
        {
            return false;
        }
        Vector low = {1e30f, 1e30f, 1e30f}, high = {-1e30f, -1e30f, -1e30f};
        size_t selected = 0;
        bool fallback = false;
        for (const auto& primitive : model.primitives)
        {
            if (Selected(primitive, projection) && !primitive.restBoundsValid)
            {
                fallback = true;
            }
        }
        const auto rest = fallback ? Globals(model, {}) : std::vector<Matrix>{};
        const auto restSkins = fallback ? SkinMatrices(model, rest) : std::vector<std::vector<Matrix>>{};
        for (const auto& primitive : model.primitives)
        {
            if (!Selected(primitive, projection))
            {
                continue;
            }
            if (primitive.restBoundsValid)
            {
                for (unsigned i = 0; i < 3; ++i)
                {
                    low[i] = (std::min)(low[i], primitive.restLow[i]);
                    high[i] = (std::max)(high[i], primitive.restHigh[i]);
                }
                selected += primitive.vertices.size();
            }
            else
            {
                for (const auto& vertex : primitive.vertices)
                {
                    const auto point = Deform(vertex, primitive, rest, restSkins);
                    for (unsigned i = 0; i < 3; ++i)
                    {
                        low[i] = (std::min)(low[i], point[i]);
                        high[i] = (std::max)(high[i], point[i]);
                    }
                    ++selected;
                }
            }
        }
        if (selected == 0)
        {
            return false;
        }
        float scale = projection.unitsPerMeter;
        if (projection.maximumFootprint > 0)
        {
            const float footprint = (std::max)(high[0] - low[0], high[2] - low[2]);
            if (footprint > .00001f)
            {
                scale = (std::min)(scale, projection.maximumFootprint / footprint);
            }
        }
        const auto global = Globals(model, pose);
        const auto skins = SkinMatrices(model, global);
        const float cosine = std::cos(projection.yawRadians), sine = std::sin(projection.yawRadians);
        const float centerX = (low[0] + high[0]) * .5f, centerZ = (low[2] + high[2]) * .5f;

        struct Triangle
        {
            std::array<Vector, 3> points;
            std::array<float, 4> color;
            Vector emission;
            bool twoSided;
            float depth;
        };

        std::vector<Triangle> triangles;
        for (const auto& primitive : model.primitives)
        {
            if (!Selected(primitive, projection))
            {
                continue;
            }
            std::vector<Vector> points;
            points.reserve(primitive.vertices.size());
            for (const auto& vertex : primitive.vertices)
            {
                const auto value = Deform(vertex, primitive, global, skins);
                const float x = (value[0] - centerX) * scale, y = -(value[2] - centerZ) * scale;
                points.push_back({cosine * x - sine * y, sine * x + cosine * y, (value[1] - low[1]) * scale});
            }
            for (size_t i = 0; i < primitive.indices.size(); i += 3)
            {
                Triangle triangle;
                triangle.depth = 0;
                for (unsigned corner = 0; corner < 3; ++corner)
                {
                    triangle.points[corner] = points[primitive.indices[i + corner]];
                    const auto& point = triangle.points[corner];
                    if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2]))
                    {
                        return false;
                    }
                    triangle.depth += point[0] + point[1] + 2 * point[2];
                }
                triangle.color = primitive.color;
                triangle.emission = primitive.emission;
                triangle.twoSided = primitive.doubleSided;
                triangles.push_back(triangle);
            }
        }
        std::stable_sort(triangles.begin(), triangles.end(),
                         [](const Triangle& a, const Triangle& b) { return a.depth < b.depth; });
        const auto initial = vertices.size();
        for (const auto& triangle : triangles)
        {
            Vector a, b, n;
            for (unsigned i = 0; i < 3; ++i)
            {
                a[i] = triangle.points[1][i] - triangle.points[0][i];
                b[i] = triangle.points[2][i] - triangle.points[0][i];
            }
            n = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
            const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length < .00000001f)
            {
                continue;
            }
            for (float& value : n)
            {
                value /= length;
            }
            if (!triangle.twoSided && n[0] + n[1] + 2 * n[2] <= 0)
            {
                continue;
            }
            // A restrained sky fill keeps dark faces readable while preserving
            // enough directional contrast to distinguish the original facets.
            const float sky = .5f + .5f * n[2];
            const float diffuse = (std::max)(0.0f, -.324f * n[0] + .487f * n[1] + .811f * n[2]);
            const float shade = .30f + .12f * sky + .58f * diffuse;
            float emission = 0;
            for (float value : triangle.emission)
            {
                emission = (std::max)(emission, value);
            }
            const float radiance = 1 + (std::min)(15.0f, emission);
            std::array<float, 3> color;
            for (unsigned i = 0; i < 3; ++i)
            {
                color[i] = Srgb(emission > 0 ? (triangle.color[i] * shade + triangle.emission[i]) / radiance
                                             : triangle.color[i] * shade);
            }
            for (const auto& point : triangle.points)
            {
                vertices.push_back({.707106781f * (point[0] - point[1]),
                                    .3535533905f * (point[0] + point[1]) - point[2], color[0], color[1],
                                    color[2], triangle.color[3], 0, 0, radiance});
            }
        }
        return vertices.size() > initial;
    }
} // namespace GltfModel
