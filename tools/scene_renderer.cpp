#include "scene_renderer.hpp"
#include "odh/skinning.hpp"
#include "odh/mesh.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <utility>

namespace {
std::string fold(std::string name) {
    for (auto& c : name)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
    return name;
}

float finite_float(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw odh::FormatError("scene renderer coordinate overflow or nonfinite value");
    return static_cast<float>(value);
}

std::array<float, 3> rotate_avatar(std::array<float, 3> point, std::uint8_t direction) {
    const double angle = std::acos(-1.0) - direction * std::acos(-1.0) / 4;
    const double c = std::cos(angle), s = std::sin(angle);
    return {finite_float(point[0] * c + point[2] * s), point[1],
            finite_float(-point[0] * s + point[2] * c)};
}

// Every View is consumed while this owning stream remains alive. No Views are
// retained in the renderer's immutable asset or decoded-resource caches.
class Archive {
public:
    explicit Archive(const std::string& path) : input_(path, std::ios::binary) {
        if (!input_) throw odh::FormatError("scene renderer cannot open archive: " + path);
        size_ = std::filesystem::file_size(path);
        entries_ = odh::far1_index(odh::View(input_, size_));
    }
    odh::View exact(const std::string& name) {
        return view(select(name, false));
    }
    const odh::FarEntry& folded(const std::string& name) const {
        return select(name, true);
    }
    odh::View view(const odh::FarEntry& entry) {
        return odh::View(input_, size_).subview(entry.offset, entry.length);
    }
private:
    const odh::FarEntry& select(const std::string& name, bool insensitive) const {
        const auto key = insensitive ? fold(name) : name;
        const odh::FarEntry* selected = nullptr;
        for (const auto& entry : entries_) {
            if ((insensitive ? fold(entry.name) : entry.name) != key) continue;
            if (selected) throw odh::FormatError("scene renderer ambiguous archive entry: " + name);
            selected = &entry;
        }
        if (!selected) throw odh::FormatError("scene renderer archive entry not found: " + name);
        return *selected;
    }
    std::ifstream input_;
    std::uint64_t size_ = 0;
    std::vector<odh::FarEntry> entries_;
};

const odh::Chunk& unique_chunk(const std::vector<odh::Chunk>& chunks,
                              const std::string& kind, std::uint16_t id) {
    const odh::Chunk* selected = nullptr;
    for (const auto& chunk : chunks) {
        if (chunk.type != kind || chunk.id != id) continue;
        if (selected) throw odh::FormatError("scene renderer ambiguous " + kind + " chunk: " + std::to_string(id));
        selected = &chunk;
    }
    if (!selected) throw odh::FormatError("scene renderer missing " + kind + " chunk: " + std::to_string(id));
    return *selected;
}

odh::AppearanceCollection loose_cmx(const std::string& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw odh::FormatError("scene renderer cannot open loose CMX: "+path);
    auto result=odh::read_cmx_appearances(odh::View(input,std::filesystem::file_size(path)));
    if(result.remaining_bytes)throw odh::FormatError("scene renderer trailing CMX bytes");
    return result;
}
odh::Mesh loose_skn(const std::string& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw odh::FormatError("scene renderer cannot open loose SKN: "+path);
    auto result=odh::read_skn(odh::View(input,std::filesystem::file_size(path)));
    if(result.remaining_bytes)throw odh::FormatError("scene renderer trailing SKN bytes");
    return result;
}
odh::BitmapImage loose_bmp(const std::string& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw odh::FormatError("scene renderer cannot open loose BMP: "+path);
    return odh::read_bmp(odh::View(input,std::filesystem::file_size(path)));
}
} // namespace

struct SceneRenderer::Impl {
    struct Clip {
        std::string entry_name; // Retain the actual archive spelling for evidence/errors.
        std::uint32_t translations, rotations;
        odh::CfpPlanes planes;
    };
    using SpriteKey = std::pair<std::uint16_t, std::uint32_t>;
    Archive animation_archive;
    SceneCfpResolver cfp_resolver;
    odh::Skeleton skeleton;
    std::vector<odh::Mesh> meshes;
    std::vector<odh::BitmapImage> textures;
    std::vector<std::optional<SceneHandPart>> hands;
    std::optional<odh::Dgrp> group;
    std::map<SpriteKey, odh::Spr2Frame> sprites;
    std::map<std::string, Clip> clips;
    std::uint64_t sample_count = 0, vertex_count = 0, triangle_count = 0;
    std::uint64_t texture_size = 0, sprite_size = 0;

    explicit Impl(const SceneConfig& config, SceneCfpResolver resolver) : animation_archive(config.animation_far), cfp_resolver(std::move(resolver)) {
        if (config.parts.empty() || config.parts.size() > 16)
            throw odh::FormatError("scene renderer requires 1..16 avatar parts");
        const auto collection = odh::read_bcf_skeletons(animation_archive.exact(config.skeleton_entry));
        const odh::Skeleton* selected_skeleton = nullptr;
        for (const auto& candidate : collection.skeletons) {
            if (candidate.name != config.skeleton_name) continue;
            if (selected_skeleton) throw odh::FormatError("scene renderer ambiguous skeleton name: " + config.skeleton_name);
            selected_skeleton = &candidate;
        }
        if (!selected_skeleton) throw odh::FormatError("scene renderer exact skeleton name not found: " + config.skeleton_name);
        skeleton = *selected_skeleton;
        Archive texture_archive(config.textures_far);
        std::uint64_t geometry_vertices = 0, texture_pixels = 0;
        for (const auto& part : config.parts) {
            if (part.hand && (!odh::valid_hand_gesture(part.hand->gesture) || part.hand->gesture == odh::HandGesture::none))
                throw odh::FormatError("scene renderer hand part must specify idle, pointing or fist");
            odh::Mesh mesh;odh::BitmapImage texture;std::string attachment_bone;
            if(part.loose_skin_assets){
                const auto appearances=loose_cmx(part.appearance_entry);const odh::Appearance* appearance=nullptr;
                for(const auto& candidate:appearances.appearances)if(fold(candidate.name)==fold(part.appearance_name)){
                    if(appearance)throw odh::FormatError("scene renderer ambiguous CMX appearance name: "+part.appearance_name);
                    appearance=&candidate;}
                if(!appearance)throw odh::FormatError("scene renderer CMX appearance not found: "+part.appearance_name);
                const odh::AppearanceBinding* binding=nullptr;
                for(const auto& candidate:appearance->bindings)if(fold(candidate.mesh_name+".skn")==fold(std::filesystem::path(part.bmf_entry).filename().string())){
                    if(binding)throw odh::FormatError("scene renderer ambiguous CMX SKN binding: "+part.bmf_entry);
                    binding=&candidate;}
                if(!binding)throw odh::FormatError("scene renderer CMX appearance has no selected SKN binding: "+part.bmf_entry);
                attachment_bone=binding->bone;
                mesh=loose_skn(part.bmf_entry);
                if(fold(mesh.skin_name)!=fold(binding->mesh_name))throw odh::FormatError("scene renderer SKN skin name differs from CMX binding");
                texture=loose_bmp(part.texture_entry);
            }else{
                const auto appearances=odh::read_bcf_appearances(animation_archive.exact(part.appearance_entry));
                const odh::Appearance* appearance=nullptr;
                for(const auto& candidate:appearances.appearances){if(candidate.name!=part.appearance_name)continue;
                    if(appearance)throw odh::FormatError("scene renderer ambiguous appearance name: "+part.appearance_name);
                    appearance=&candidate;}
                if(!appearance)throw odh::FormatError("scene renderer exact appearance name not found: "+part.appearance_name);
                const odh::AppearanceBinding* binding=nullptr;
                for(const auto& candidate:appearance->bindings){if(fold(candidate.mesh_name+".bmf")!=fold(part.bmf_entry))continue;
                    if(binding)throw odh::FormatError("scene renderer ambiguous appearance mesh binding: "+part.bmf_entry);
                    binding=&candidate;}
                if(!binding)throw odh::FormatError("scene renderer appearance has no selected BMF binding: "+part.bmf_entry);
                attachment_bone=binding->bone;
                mesh=odh::read_bmf(animation_archive.exact(part.bmf_entry));
                if(fold(mesh.skin_name)!=fold(binding->mesh_name))throw odh::FormatError("scene renderer BMF skin name differs from appearance binding");
                texture=odh::read_bmp(texture_archive.exact(part.texture_entry));
            }
            std::size_t bones = 0;
            for(const auto& bone:skeleton.bones)if(fold(bone.name)==fold(attachment_bone))++bones;
            if(bones!=1)throw odh::FormatError("scene renderer appearance bone must resolve uniquely in skeleton");
            geometry_vertices += mesh.real_vertices.size() + mesh.blend_vertices.size();
            if (geometry_vertices > odh::MeshLimits{}.vertices)
                throw odh::FormatError("scene renderer cumulative geometry vertex limit exceeded");
            triangle_count += mesh.faces.size();
            if (triangle_count > odh::RasterLimits{}.triangles)
                throw odh::FormatError("scene renderer cumulative triangle limit exceeded");
            vertex_count += mesh.real_vertices.size();
            texture_pixels += texture.pixels.size();
            if (texture_pixels > odh::RasterLimits{}.texture_pixels)
                throw odh::FormatError("scene renderer cumulative texture pixel limit exceeded");
            texture_size += texture.pixels.size() * 4;
            meshes.push_back(std::move(mesh));
            textures.push_back(std::move(texture));
            hands.push_back(part.hand);
        }
        if (!config.object_far.empty() || !config.object_entry.empty() || config.object_id!=0) {
            if (config.object_far.empty() || config.object_entry.empty())
                throw odh::FormatError("scene renderer incomplete object resource selection");
            load_object(config);
        }
    }

    void load_object(const SceneConfig& config) {
        Archive archive(config.object_far);
        const auto object = archive.exact(config.object_entry);
        std::vector<odh::Chunk> chunks;
        odh::iff_chunks(object, [&](const auto& chunk) { chunks.push_back(chunk); });
        const auto& definition = unique_chunk(chunks, "OBJD", config.object_id).payload;
        if (odh::read_objd_binding(definition).master_id != 0)
            throw odh::FormatError("scene renderer requires single-part OBJD");
        const auto graphics = odh::read_objd_graphics(definition);
        if (graphics.num_graphics == 0) throw odh::FormatError("scene renderer OBJD has no base graphic");
        group = odh::read_dgrp(unique_chunk(chunks, "DGRP", graphics.base_graphic).payload);
        std::map<std::uint16_t, odh::Palette> palettes;
        std::uint64_t sprite_pixels = 0;
        for (const auto& image : group->images) {
            for (const auto& reference : image.sprites) {
                if (reference.sprite_id > 65535 || (reference.flags & ~1U))
                    throw odh::FormatError("scene renderer unsupported DGRP sprite reference or flags");
                const SpriteKey key{static_cast<std::uint16_t>(reference.sprite_id), reference.frame};
                if (sprites.count(key)) continue;
                const auto& raw = unique_chunk(chunks, "SPR2", key.first).payload;
                // The public SPR2 1000 profile uses the low 16 bits of its
                // preserved 32-bit default palette field as the effective ID.
                const auto id = static_cast<std::uint16_t>(raw.le32(8));
                auto palette = palettes.find(id);
                if (palette == palettes.end())
                    palette = palettes.emplace(id, odh::read_palt(unique_chunk(chunks, "PALT", id).payload)).first;
                auto limits = odh::SpriteLimits{};
                limits.pixels = static_cast<std::uint32_t>(odh::CompositorLimits{}.pixels - sprite_pixels);
                auto frame = odh::read_spr2_frame(raw, key.second, palette->second, limits);
                if ((frame.flags & 2U) == 0)
                    throw odh::FormatError("scene renderer requires sprite Z samples");
                sprite_pixels += frame.pixels.size();
                sprite_size += frame.pixels.size() * 4 + frame.indices.size() + frame.z.size();
                sprites.emplace(key, std::move(frame));
            }
        }
    }

    const odh::CfpPlanes& planes(const odh::AnimationHeader& animation) {
        if (animation.xskill.empty()) throw odh::FormatError("scene renderer animation has no xskill");
        const auto key = fold(animation.xskill + ".cfp");
        const auto found = clips.find(key);
        if (found != clips.end()) {
            if (found->second.translations != animation.translations || found->second.rotations != animation.rotations)
                throw odh::FormatError("scene renderer cached CFP sample counts differ from animation: " + found->second.entry_name);
            return found->second.planes;
        }
        if (clips.size() >= 64) throw odh::FormatError("scene renderer CFP clip cache limit exceeded");
        const auto count = std::uint64_t{animation.translations} * 3 + std::uint64_t{animation.rotations} * 4;
        if (count > odh::AnimationLimits{}.samples - sample_count)
            throw odh::FormatError("scene renderer cumulative CFP sample cache limit exceeded");
        const auto& entry = animation_archive.folded(animation.xskill + ".cfp");
        auto decoded = odh::read_cfp(animation_archive.view(entry), animation.translations, animation.rotations);
        const auto inserted = clips.emplace(key, Clip{entry.name, animation.translations, animation.rotations, std::move(decoded)});
        sample_count += count;
        return inserted.first->second.planes;
    }

    odh::CompositeFrame render(const SceneState& state, const odh::WorldProjection& camera, odh::RasterOptions options) {
        if (state.avatar_direction > 7 || state.object_direction > 7)
            throw odh::FormatError("scene renderer direction must be 0..7");
        if (!odh::valid_hand_gesture(state.hands.left) || !odh::valid_hand_gesture(state.hands.right))
            throw odh::FormatError("scene renderer invalid hand gesture");
        if (!std::isfinite(state.frame)) throw odh::FormatError("scene renderer frame must be finite");
        // Validate the camera and both tile coordinates before decoding any CFP.
        odh::project_tile(camera, state.avatar_tile);
        const odh::DgrpImage* image=nullptr;
        if (group) {
            odh::project_tile(camera, state.object_tile);
            image = odh::dgrp_image(*group, static_cast<std::uint8_t>(1U << state.object_direction), camera.zoom, camera.rotation);
            if (!image) throw odh::FormatError("scene renderer drawing direction/zoom missing");
        }
        // The provider is authoritative when installed: a rejection or missing
        // result never falls back to the base rig archive. Keep its owner alive
        // through sampling/rasterization without retaining a second CFP cache.
        std::shared_ptr<const odh::CfpPlanes> external;
        const odh::CfpPlanes* cfp = nullptr;
        if (state.animation) {
            if (cfp_resolver) {
                external = cfp_resolver(*state.animation);
                if (!external) throw odh::FormatError("scene renderer CFP resolver returned no planes");
                cfp = external.get();
            } else cfp = &planes(*state.animation);
        }
        auto pose = odh::sample_pose(skeleton, state.animation, cfp, state.frame);
        if(state.head_only) {
            std::optional<std::size_t> head;
            for(std::size_t i=0;i<pose.bones.size();++i)if(fold(pose.bones[i].name)=="head") {
                if(head)throw odh::FormatError("scene renderer head-only pose has ambiguous HEAD bone");
                head=i;
            }
            if(!head)throw odh::FormatError("scene renderer head-only pose has no HEAD bone");
            // Simitone's StripAllButHead zeros the absolute matrices of every
            // bone other than HEAD. Keep the same deformation rule here; the
            // portrait camera can be added independently from lot projection.
            for(std::size_t i=0;i<pose.bones.size();++i)if(i!=*head) {
                pose.bones[i].absolute_matrix.fill(0.0f);
                pose.bones[i].absolute_position.fill(0.0f);
            }
        }
        std::vector<odh::RasterTriangle> triangles;
        triangles.reserve(static_cast<std::size_t>(triangle_count));
        for (std::size_t index = 0; index < meshes.size(); ++index) {
            if (hands[index] && hands[index]->gesture != (hands[index]->right ? state.hands.right : state.hands.left))
                continue;
            const auto mesh = odh::skin_mesh(meshes[index], pose);
            std::vector<odh::RasterVertex> projected;
            projected.reserve(mesh.vertices.size());
            for (const auto& vertex : mesh.vertices) {
                auto world = rotate_avatar(vertex.position, state.avatar_direction);
                world[0] = finite_float(double(world[0]) + 3 * double(state.avatar_tile[0]));
                world[1] = finite_float(double(world[1]) + 3 * double(state.avatar_tile[2]));
                world[2] = finite_float(double(world[2]) + 3 * double(state.avatar_tile[1]));
                projected.push_back({odh::project_world(camera, world), vertex.texcoord,
                                     rotate_avatar(vertex.normal, state.avatar_direction)});
            }
            for (const auto& face : mesh.faces)
                triangles.push_back({{projected[face[0]], projected[face[1]], projected[face[2]]}, index,
                    state.owner_id?state.owner_id:static_cast<std::uint16_t>(index)});
        }
        const auto base = odh::rasterize(camera.width, camera.height, triangles, textures, options);
        if (!image) return {base};
        std::vector<odh::SpriteLayer> layers;
        layers.reserve(image->sprites.size());
        for (const auto& reference : image->sprites) {
            const auto& frame = sprites.at({static_cast<std::uint16_t>(reference.sprite_id), reference.frame});
            const auto placement = odh::place_sprite(camera, reference, frame, state.object_tile,
                finite_float((state.object_direction%2==0?state.object_direction:0) * std::acos(-1.0) / 4));
            layers.push_back({&frame, placement.x, placement.y, placement.back_depth, placement.front_depth_delta, placement.flip});
        }
        return odh::composite_sprites(base, layers);
    }
};

SceneRenderer::SceneRenderer(const SceneConfig& config, SceneCfpResolver resolver) : impl_(std::make_unique<Impl>(config,std::move(resolver))) {}
SceneRenderer::~SceneRenderer() = default;
SceneRenderer::SceneRenderer(SceneRenderer&&) noexcept = default;
SceneRenderer& SceneRenderer::operator=(SceneRenderer&&) noexcept = default;

odh::CompositeFrame SceneRenderer::render(const SceneState& state, const odh::WorldProjection& camera, odh::RasterOptions options) {
    if (!impl_) throw odh::FormatError("scene renderer used after move");
    return impl_->render(state, camera, options);
}
std::uint64_t SceneRenderer::cache_samples() const { return impl_ ? impl_->sample_count : 0; }
std::size_t SceneRenderer::cache_clips() const { return impl_ ? impl_->clips.size() : 0; }
std::uint64_t SceneRenderer::vertices() const { return impl_ ? impl_->vertex_count : 0; }
std::uint64_t SceneRenderer::triangles() const { return impl_ ? impl_->triangle_count : 0; }
std::uint64_t SceneRenderer::texture_bytes() const { return impl_ ? impl_->texture_size : 0; }
std::uint64_t SceneRenderer::sprite_bytes() const { return impl_ ? impl_->sprite_size : 0; }
