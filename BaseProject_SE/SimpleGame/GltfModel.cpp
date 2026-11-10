#include "GltfModel.h"
#include "GltfMath.h"
#ifdef _MSC_VER
#pragma warning(push)
// cgltf uses portable C runtime calls; keep MSVC's deprecation suppression
// local to its implementation.
#pragma warning(disable : 4996)
#endif
#define CGLTF_IMPLEMENTATION
#include "ThirdParty/cgltf.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>
#include <utility>

namespace GltfModel
{
    namespace
    {
        constexpr size_t FileLimit = 64 * 1024 * 1024;

        void Require(bool valid, const char* message)
        {
            if (!valid)
            {
                throw std::runtime_error(message);
            }
        }

        void Finite(const float* values, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
            {
                Require(std::isfinite(values[i]) && std::fabs(values[i]) < 1000000,
                        "non_finite_or_excessive_value");
            }
        }

        const cgltf_accessor* Attribute(const cgltf_primitive& primitive, cgltf_attribute_type type)
        {
            for (size_t i = 0; i < primitive.attributes_count; ++i)
            {
                if (primitive.attributes[i].type == type && primitive.attributes[i].index == 0)
                {
                    return primitive.attributes[i].data;
                }
            }
            return nullptr;
        }
    } // namespace

    std::shared_ptr<const Model> Load(const std::filesystem::path& path, std::string& error,
                                      const Limits& limits)
    {
        try
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            Require(static_cast<bool>(in), "file_unavailable");
            const auto length = in.tellg();
            Require(length > 0 && static_cast<std::uint64_t>(length) <= FileLimit, "file_size_limit");
            std::vector<char> bytes(static_cast<size_t>(length));
            in.seekg(0);
            Require(static_cast<bool>(in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))),
                    "incomplete_read");
            cgltf_options options = {};
            cgltf_data* raw = nullptr;
            const auto parsed = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
            std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
            Require(parsed == cgltf_result_success && raw, "invalid_glb");
            Require(raw->file_type == cgltf_file_type_glb && raw->buffers_count == 1 && !raw->buffers[0].uri,
                    "embedded_glb_required");
            Require(raw->nodes_count <= limits.nodes && raw->skins_count <= 64 &&
                        raw->animations_count <= limits.animations,
                    "scene_limit");
            Require(raw->extensions_required_count == 0, "required_extension_unsupported");
            Require(cgltf_load_buffers(&options, raw, nullptr) == cgltf_result_success, "invalid_buffer");
            Require(cgltf_validate(raw) == cgltf_result_success, "gltf_validation_failed");
            auto model = std::make_shared<Model>();
            model->bytes = bytes.size();
            model->revision = 14695981039346656037ull;
            for (unsigned char value : bytes)
            {
                model->revision = (model->revision ^ value) * 1099511628211ull;
            }
            for (size_t i = 0; i < raw->nodes_count; ++i)
            {
                const auto& source = raw->nodes[i];
                Node node;
                node.name = source.name ? source.name : "";
                node.parent = source.parent ? static_cast<int>(source.parent - raw->nodes) : -1;
                node.matrixOnly = source.has_matrix != 0;
                std::copy_n(source.translation, 3, node.bind.translation.begin());
                std::copy_n(source.rotation, 4, node.bind.rotation.begin());
                std::copy_n(source.scale, 3, node.bind.scale.begin());
                cgltf_node_transform_local(&source, node.matrix.data());
                Finite(node.matrix.data(), 16);
                model->nodes.push_back(node);
            }
            std::vector<unsigned char> colors(model->nodes.size());
            std::vector<unsigned> depths(model->nodes.size());
            std::function<void(unsigned, unsigned)> visit = [&](unsigned node, unsigned depth)
            {
                Require(depth <= 256, "hierarchy_depth_limit");
                Require(colors[node] != 1, "cyclic_hierarchy");
                if (colors[node] == 2)
                {
                    return;
                }
                colors[node] = 1;
                const int parent = model->nodes[node].parent;
                if (parent >= 0)
                {
                    Require(static_cast<size_t>(parent) < model->nodes.size(), "invalid_parent");
                    visit(parent, depth + 1);
                }
                depths[node] = parent < 0 ? 0 : depths[parent] + 1;
                Require(depths[node] <= 256, "hierarchy_depth_limit");
                colors[node] = 2;
                model->hierarchy.push_back(node);
            };
            for (unsigned i = 0; i < model->nodes.size(); ++i)
            {
                visit(i, 0);
            }
            for (size_t i = 0; i < raw->skins_count; ++i)
            {
                const auto& source = raw->skins[i];
                Require(source.joints_count > 0 && source.joints_count <= limits.joints, "joint_limit");
                Require(!source.inverse_bind_matrices ||
                            (source.inverse_bind_matrices->type == cgltf_type_mat4 &&
                             source.inverse_bind_matrices->count == source.joints_count &&
                             !source.inverse_bind_matrices->is_sparse),
                        "inverse_bind_shape");
                Skin skin;
                for (size_t j = 0; j < source.joints_count; ++j)
                {
                    skin.joints.push_back(static_cast<unsigned>(source.joints[j] - raw->nodes));
                    Matrix inverse = Identity();
                    if (source.inverse_bind_matrices)
                    {
                        Require(
                            cgltf_accessor_read_float(source.inverse_bind_matrices, j, inverse.data(), 16),
                            "inverse_bind_read_failed");
                    }
                    Finite(inverse.data(), 16);
                    skin.inverseBind.push_back(inverse);
                }
                model->skins.push_back(std::move(skin));
            }
            size_t totalVertices = 0, totalIndices = 0;
            std::set<const cgltf_node*> sceneNodes;
            std::function<void(const cgltf_node*)> visible = [&](const cgltf_node* node)
            {
                if (!sceneNodes.insert(node).second)
                {
                    return;
                }
                for (size_t c = 0; c < node->children_count; ++c)
                {
                    visible(node->children[c]);
                }
            };
            const auto* scene = raw->scene ? raw->scene : (raw->scenes_count ? raw->scenes : nullptr);
            Require(scene != nullptr, "scene_missing");
            for (size_t i = 0; i < scene->nodes_count; ++i)
            {
                visible(scene->nodes[i]);
            }
            for (unsigned n = 0; n < raw->nodes_count; ++n)
            {
                const auto& node = raw->nodes[n];
                if (!node.mesh || !sceneNodes.count(&node))
                {
                    continue;
                }
                Require(!node.has_mesh_gpu_instancing, "gpu_instancing_extension_unsupported");
                for (size_t p = 0; p < node.mesh->primitives_count; ++p)
                {
                    const auto& source = node.mesh->primitives[p];
                    Require(source.type == cgltf_primitive_type_triangles && source.targets_count == 0 &&
                                !source.has_draco_mesh_compression,
                            "triangles_without_morph_or_draco_required");
                    const auto* positions = Attribute(source, cgltf_attribute_type_position);
                    const auto* joints = Attribute(source, cgltf_attribute_type_joints);
                    const auto* weights = Attribute(source, cgltf_attribute_type_weights);
                    Require(positions && !positions->is_sparse && positions->type == cgltf_type_vec3,
                            "positions_unsupported");
                    totalVertices += positions->count;
                    Require(totalVertices <= limits.vertices, "vertex_limit");
                    Primitive primitive;
                    primitive.node = n;
                    primitive.skin = node.skin ? static_cast<int>(node.skin - raw->skins) : -1;
                    primitive.meshName = !model->nodes[n].name.empty()
                                             ? model->nodes[n].name
                                             : (node.mesh->name ? node.mesh->name : "");
                    if (node.skin)
                    {
                        Require(joints && weights && !joints->is_sparse && !weights->is_sparse &&
                                    joints->count == positions->count && weights->count == positions->count,
                                "skin_attributes_missing");
                        Require(joints->type == cgltf_type_vec4 && weights->type == cgltf_type_vec4,
                                "four_weights_required");
                    }
                    Require(!Attribute(source, cgltf_attribute_type_color), "vertex_colors_unsupported");
                    for (size_t v = 0; v < positions->count; ++v)
                    {
                        Vertex vertex;
                        Require(cgltf_accessor_read_float(positions, v, vertex.position.data(), 3),
                                "vertex_read_failed");
                        Finite(vertex.position.data(), 3);
                        if (node.skin)
                        {
                            Require(cgltf_accessor_read_uint(joints, v, vertex.joints.data(), 4) &&
                                        cgltf_accessor_read_float(weights, v, vertex.weights.data(), 4),
                                    "weights_read_failed");
                            float sum = 0;
                            for (unsigned j = 0; j < 4; ++j)
                            {
                                Require(vertex.joints[j] < node.skin->joints_count &&
                                            std::isfinite(vertex.weights[j]) && vertex.weights[j] >= 0 &&
                                            vertex.weights[j] <= 1,
                                        "invalid_weight");
                                sum += vertex.weights[j];
                            }
                            Require(sum > .00001f, "zero_weight_sum");
                            for (float& weight : vertex.weights)
                            {
                                weight /= sum;
                            }
                        }
                        primitive.vertices.push_back(vertex);
                    }
                    const size_t count = source.indices ? source.indices->count : positions->count;
                    totalIndices += count;
                    Require(count % 3 == 0 && totalIndices <= static_cast<size_t>(limits.vertices) * 3 &&
                                (!source.indices || !source.indices->is_sparse),
                            "index_limit");
                    for (size_t index = 0; index < count; ++index)
                    {
                        const size_t value =
                            source.indices ? cgltf_accessor_read_index(source.indices, index) : index;
                        Require(value < positions->count, "index_out_of_bounds");
                        primitive.indices.push_back(static_cast<unsigned>(value));
                    }
                    if (source.material)
                    {
                        const auto& material = *source.material;
                        Require(!material.pbr_metallic_roughness.base_color_texture.texture &&
                                    !material.normal_texture.texture && !material.emissive_texture.texture,
                                "textured_material_unsupported");
                        Require(material.alpha_mode == cgltf_alpha_mode_opaque, "alpha_material_unsupported");
                        std::copy_n(material.pbr_metallic_roughness.base_color_factor, 4,
                                    primitive.color.begin());
                        std::copy_n(material.emissive_factor, 3, primitive.emission.begin());
                        const float strength =
                            material.has_emissive_strength ? material.emissive_strength.emissive_strength : 1;
                        for (float& value : primitive.emission)
                        {
                            value *= strength;
                        }
                        primitive.doubleSided = material.double_sided != 0;
                        Finite(primitive.color.data(), 4);
                        Finite(primitive.emission.data(), 3);
                        for (float value : primitive.color)
                        {
                            Require(value >= 0 && value <= 1, "invalid_base_color");
                        }
                        for (float value : primitive.emission)
                        {
                            Require(value >= 0, "invalid_emissive_color");
                        }
                        primitive.color[3] = 1; // OPAQUE materials ignore their base color alpha.
                    }
                    model->primitives.push_back(std::move(primitive));
                }
            }
            size_t keyValues = 0;
            for (size_t a = 0; a < raw->animations_count; ++a)
            {
                const auto& source = raw->animations[a];
                Clip clip;
                clip.name = source.name ? source.name : "clip_" + std::to_string(a);
                std::set<std::pair<unsigned, int>> destinations;
                for (size_t c = 0; c < source.channels_count; ++c)
                {
                    const auto& channel = source.channels[c];
                    Require(channel.target_node && channel.sampler, "animation_target_missing");
                    Channel target;
                    target.node = static_cast<unsigned>(channel.target_node - raw->nodes);
                    Require(!model->nodes[target.node].matrixOnly, "matrix_node_animation_unsupported");
                    Require(destinations.insert({target.node, static_cast<int>(channel.target_path)}).second,
                            "duplicate_animation_target");
                    switch (channel.target_path)
                    {
                    case cgltf_animation_path_type_translation:
                        target.path = Path::Translation;
                        break;
                    case cgltf_animation_path_type_rotation:
                        target.path = Path::Rotation;
                        break;
                    case cgltf_animation_path_type_scale:
                        target.path = Path::Scale;
                        break;
                    default:
                        Require(false, "morph_animation_unsupported");
                    }
                    const auto& sampler = *channel.sampler;
                    Require(sampler.interpolation == cgltf_interpolation_type_linear ||
                                sampler.interpolation == cgltf_interpolation_type_step ||
                                sampler.interpolation == cgltf_interpolation_type_cubic_spline,
                            "unknown_interpolation");
                    Require(sampler.input && sampler.output && !sampler.input->is_sparse &&
                                !sampler.output->is_sparse,
                            "animation_accessors_missing");
                    const size_t multiple =
                        sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
                    const size_t components = target.path == Path::Rotation ? 4 : 3;
                    Require(sampler.input->type == cgltf_type_scalar && sampler.input->count > 0 &&
                                sampler.output->count == sampler.input->count * multiple &&
                                cgltf_num_components(sampler.output->type) == components,
                            "animation_accessor_shape");
                    keyValues += sampler.output->count;
                    Require(keyValues <= limits.keys, "animation_key_limit");
                    target.interpolation = sampler.interpolation == cgltf_interpolation_type_step
                                               ? Interpolation::Step
                                           : multiple == 3 ? Interpolation::Cubic
                                                           : Interpolation::Linear;
                    for (size_t k = 0; k < sampler.input->count; ++k)
                    {
                        float seconds = 0;
                        Require(cgltf_accessor_read_float(sampler.input, k, &seconds, 1) &&
                                    std::isfinite(seconds) && seconds >= 0 && seconds <= 86400 &&
                                    (k == 0 || seconds > target.times.back()),
                                "invalid_key_times");
                        target.times.push_back(seconds);
                    }
                    clip.duration = (std::max)(clip.duration, target.times.back());
                    for (size_t k = 0; k < sampler.output->count; ++k)
                    {
                        Rotation value = {};
                        Require(cgltf_accessor_read_float(sampler.output, k, value.data(), components),
                                "key_read_failed");
                        Finite(value.data(), components);
                        target.values.push_back(value);
                    }
                    clip.channels.push_back(std::move(target));
                }
                model->clips.push_back(std::move(clip));
            }
            model->bytes += totalVertices * sizeof(Vertex) + totalIndices * sizeof(unsigned) +
                            keyValues * (sizeof(Rotation) + sizeof(float));
            Require(!model->primitives.empty(), "no_visible_geometry");
            ComputeRestBounds(*model);
            return model;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return {};
        }
    }

    int FindClip(const Model& model, const std::string& name)
    {
        for (unsigned i = 0; i < model.clips.size(); ++i)
        {
            const auto& id = model.clips[i].name;
            if (id == name || (id.size() > name.size() + 2 &&
                               id.compare(id.size() - name.size() - 2, name.size() + 2, "__" + name) == 0))
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
} // namespace GltfModel
