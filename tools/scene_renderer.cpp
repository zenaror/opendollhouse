#include "scene_renderer.hpp"
#include "odh/skinning.hpp"
#include "odh/mesh.hpp"
#include <algorithm>
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

using Vector3d = std::array<double,3>;
Vector3d add(Vector3d a,Vector3d b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
Vector3d subtract(Vector3d a,Vector3d b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vector3d multiply(Vector3d a,double scale){return {a[0]*scale,a[1]*scale,a[2]*scale};}
double dot(Vector3d a,Vector3d b){return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2];}
Vector3d cross(Vector3d a,Vector3d b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vector3d normalized(Vector3d value,const char* error){
    const auto length=std::sqrt(dot(value,value));
    if(!std::isfinite(length)||length<=1e-12)throw odh::FormatError(error);
    return multiply(value,1.0/length);
}
void validate_portrait_camera(const HeadPortraitCamera& camera){
    constexpr auto pi=3.14159265358979323846;
    if(!std::isfinite(camera.vertical_fov_radians)||camera.vertical_fov_radians<=0.05||
       camera.vertical_fov_radians>=pi-0.05||!std::isfinite(camera.zoom)||
       camera.zoom<=0.0||camera.zoom>64.0||!std::isfinite(camera.distance)||
       camera.distance<=camera.near_plane||camera.distance>1000.0||
       !std::isfinite(camera.yaw_radians)||std::abs(camera.yaw_radians)>2*pi||
       !std::isfinite(camera.pitch_radians)||std::abs(camera.pitch_radians)>2*pi||
       !std::isfinite(camera.target_y_offset)||std::abs(camera.target_y_offset)>1000.0||
       !std::isfinite(camera.near_plane)||camera.near_plane<0.01||camera.near_plane>1000.0)
        throw odh::FormatError("scene renderer portrait camera is outside its bounded profile");
}
struct PortraitView {
    Vector3d position,right,up,forward;
    double focal,near_plane;
};
PortraitView portrait_view(const HeadPortraitCamera& camera,const std::array<float,3>& head){
    const Vector3d target{head[0],double(head[1])+camera.target_y_offset,head[2]};
    // Simitone rotates the camera offset around Y and then X before looking
    // toward HEAD with world-up Y.
    const auto cy=std::cos(camera.yaw_radians),sy=std::sin(camera.yaw_radians);
    const auto cx=std::cos(camera.pitch_radians),sx=std::sin(camera.pitch_radians);
    const Vector3d offset{camera.distance*sy,-camera.distance*cy*sx,camera.distance*cy*cx};
    const auto position=add(target,offset);
    const auto forward=normalized(subtract(target,position),"scene renderer portrait forward vector is invalid");
    const auto right=normalized(cross(forward,{0.0,1.0,0.0}),"scene renderer portrait up vector is parallel to view");
    const auto up=normalized(cross(right,forward),"scene renderer portrait up vector is invalid");
    const auto focal=(74.0/2.0)/std::tan(camera.vertical_fov_radians/2.0)*camera.zoom;
    if(!std::isfinite(focal)||focal>1e7)throw odh::FormatError("scene renderer portrait focal length exceeds limit");
    return {position,right,up,forward,focal,camera.near_plane};
}
std::array<float,3> project_portrait_vertex(const PortraitView& camera,const std::array<float,3>& point){
    const Vector3d relative{double(point[0])-camera.position[0],
        double(point[1])-camera.position[1],double(point[2])-camera.position[2]};
    const auto measured_depth=dot(relative,camera.forward);
    const auto tolerance=1e-5*std::max(1.0,camera.near_plane);
    if(!std::isfinite(measured_depth)||measured_depth<camera.near_plane-tolerance)
        throw odh::FormatError("scene renderer portrait vertex is behind the near plane");
    const auto depth=std::max(measured_depth,camera.near_plane);
    const auto x=37.0+camera.focal*dot(relative,camera.right)/depth;
    const auto y=37.0-camera.focal*dot(relative,camera.up)/depth;
    return {finite_float(x),finite_float(y),finite_float(depth)};
}
std::array<float,3> portrait_normal(const PortraitView& camera,const std::array<float,3>& normal){
    const Vector3d value{normal[0],normal[1],normal[2]};
    return {finite_float(dot(value,camera.right)),finite_float(dot(value,camera.up)),
            finite_float(dot(value,camera.forward))};
}
struct PortraitVertex {
    std::array<float,3> position;
    std::array<float,2> texcoord;
    std::array<float,3> normal;
};
double portrait_depth(const PortraitView& camera,const std::array<float,3>& point){
    const Vector3d relative{double(point[0])-camera.position[0],
        double(point[1])-camera.position[1],double(point[2])-camera.position[2]};
    const auto depth=dot(relative,camera.forward);
    if(!std::isfinite(depth))throw odh::FormatError("scene renderer portrait depth is nonfinite");
    return depth;
}
PortraitVertex interpolate(const PortraitVertex& a,const PortraitVertex& b,double t){
    PortraitVertex result;
    for(std::size_t i=0;i<3;++i){
        result.position[i]=finite_float(double(a.position[i])+(double(b.position[i])-a.position[i])*t);
        result.normal[i]=finite_float(double(a.normal[i])+(double(b.normal[i])-a.normal[i])*t);
    }
    for(std::size_t i=0;i<2;++i)
        result.texcoord[i]=finite_float(double(a.texcoord[i])+(double(b.texcoord[i])-a.texcoord[i])*t);
    return result;
}
struct PortraitPolygon {
    std::array<PortraitVertex,4> vertices{};
    std::size_t size=0;
};
PortraitPolygon clip_portrait_triangle(const PortraitView& camera,
    const std::array<PortraitVertex,3>& triangle){
    PortraitPolygon polygon;
    auto previous=triangle.back();
    auto previous_depth=portrait_depth(camera,previous.position);
    auto previous_inside=previous_depth>=camera.near_plane;
    for(const auto& current:triangle){
        const auto current_depth=portrait_depth(camera,current.position);
        const auto current_inside=current_depth>=camera.near_plane;
        if(current_inside!=previous_inside){
            const auto t=(camera.near_plane-previous_depth)/(current_depth-previous_depth);
            polygon.vertices[polygon.size++]=interpolate(previous,current,t);
        }
        if(current_inside)polygon.vertices[polygon.size++]=current;
        previous=current;
        previous_depth=current_depth;
        previous_inside=current_inside;
    }
    return polygon;
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

    odh::CompositeFrame render(const SceneState& state, const odh::WorldProjection& camera,
                               odh::RasterOptions options,const HeadPortraitCamera* portrait=nullptr,
                               ScenePoseAnchors* anchors=nullptr) {
        if (state.avatar_direction > 7 || state.object_direction > 7)
            throw odh::FormatError("scene renderer direction must be 0..7");
        if (!odh::valid_hand_gesture(state.hands.left) || !odh::valid_hand_gesture(state.hands.right))
            throw odh::FormatError("scene renderer invalid hand gesture");
        if (!std::isfinite(state.frame)) throw odh::FormatError("scene renderer frame must be finite");
        // Validate the camera before decoding any CFP. Portraits deliberately
        // use a separate perspective camera and never the lot's tile projection.
        if(portrait)validate_portrait_camera(*portrait);
        else odh::project_tile(camera, state.avatar_tile);
        const odh::DgrpImage* image=nullptr;
        if (group) {
            if(portrait)throw odh::FormatError("scene renderer portrait does not include an object graphic");
            odh::project_tile(camera, state.object_tile);
            image = odh::dgrp_image(*group, static_cast<std::uint8_t>(1U << state.object_direction), camera.zoom, camera.rotation);
            if (!image) throw odh::FormatError("scene renderer drawing direction/zoom missing");
        }
        // The provider is authoritative when installed: a rejection or missing
        // result never falls back to the base rig archive. Keep its owner alive
        // through sampling/rasterization without retaining a second CFP cache.
        std::shared_ptr<const odh::CfpPlanes> external,carry_external;
        const auto resolve_planes=[&](const odh::AnimationHeader* animation,
                                     std::shared_ptr<const odh::CfpPlanes>& owner)->const odh::CfpPlanes* {
            if(!animation)return nullptr;
            if(cfp_resolver){
                owner=cfp_resolver(*animation);
                if(!owner)throw odh::FormatError("scene renderer CFP resolver returned no planes");
                return owner.get();
            }
            return &planes(*animation);
        };
        const auto* cfp=resolve_planes(state.animation,external);
        const auto* carry_cfp=resolve_planes(state.carry_animation,carry_external);
        auto pose = odh::sample_pose(skeleton,state.animation,cfp,state.frame,state.carry_animation,carry_cfp);
        ScenePoseAnchors prepared_anchors;
        if(anchors){
            constexpr std::array<const char*,3> names{"r_finger0","head","pelvis"};
            for(const auto& bone:pose.bones)for(std::size_t slot=0;slot<names.size();++slot){
                if(fold(bone.name)!=names[slot])continue;
                if(prepared_anchors.avatar_slots[slot])
                    throw odh::FormatError("scene renderer avatar slot bone is ambiguous");
                const auto point=rotate_avatar(bone.absolute_position,state.avatar_direction);
                prepared_anchors.avatar_slots[slot]=std::array<float,3>{
                    finite_float(double(point[0])/3+state.avatar_tile[0]-.5),
                    finite_float(double(point[2])/3+state.avatar_tile[1]-.5),
                    finite_float(double(point[1])/3+state.avatar_tile[2])};
            }
        }
        std::optional<std::array<float,3>> head_position;
        if(state.head_only) {
            std::optional<std::size_t> head;
            for(std::size_t i=0;i<pose.bones.size();++i)if(fold(pose.bones[i].name)=="head") {
                if(head)throw odh::FormatError("scene renderer head-only pose has ambiguous HEAD bone");
                head=i;
            }
            if(!head)throw odh::FormatError("scene renderer head-only pose has no HEAD bone");
            head_position=pose.bones[*head].absolute_position;
            // Simitone's StripAllButHead zeros the absolute matrices of every
            // bone other than HEAD. Keep the same deformation rule here; the
            // portrait camera can be added independently from lot projection.
            for(std::size_t i=0;i<pose.bones.size();++i)if(i!=*head) {
                pose.bones[i].absolute_matrix.fill(0.0f);
                pose.bones[i].absolute_position.fill(0.0f);
            }
        }
        std::optional<PortraitView> view;
        if(portrait) {
            if(!head_position)throw odh::FormatError("scene renderer portrait requires head-only pose");
            view=portrait_view(*portrait,*head_position);
        }
        std::vector<odh::RasterTriangle> triangles;
        triangles.reserve(static_cast<std::size_t>(triangle_count)*(portrait?2U:1U));
        for (std::size_t index = 0; index < meshes.size(); ++index) {
            if (hands[index] && hands[index]->gesture != (hands[index]->right ? state.hands.right : state.hands.left))
                continue;
            const auto mesh = odh::skin_mesh(meshes[index], pose);
            if(view) {
                for(const auto& face:mesh.faces) {
                    const std::array<PortraitVertex,3> source{{
                        {mesh.vertices[face[0]].position,mesh.vertices[face[0]].texcoord,mesh.vertices[face[0]].normal},
                        {mesh.vertices[face[1]].position,mesh.vertices[face[1]].texcoord,mesh.vertices[face[1]].normal},
                        {mesh.vertices[face[2]].position,mesh.vertices[face[2]].texcoord,mesh.vertices[face[2]].normal}}};
                    const auto polygon=clip_portrait_triangle(*view,source);
                    if(polygon.size<3)continue;
                    std::array<odh::RasterVertex,4> projected{};
                    for(std::size_t i=0;i<polygon.size;++i)
                        projected[i]={project_portrait_vertex(*view,polygon.vertices[i].position),polygon.vertices[i].texcoord,
                                      portrait_normal(*view,polygon.vertices[i].normal)};
                    for(std::size_t i=1;i+1<polygon.size;++i)
                        triangles.push_back({{projected[0],projected[i],projected[i+1]},index,
                            state.owner_id?state.owner_id:static_cast<std::uint16_t>(index)});
                }
                continue;
            }
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
        if(portrait)options.perspective_correct=true;
        const auto base = portrait ? odh::rasterize(74,74,triangles,textures,options)
                                   : odh::rasterize(camera.width,camera.height,triangles,textures,options);
        if (!image) {
            if(anchors)*anchors=prepared_anchors;
            return {base};
        }
        std::vector<odh::SpriteLayer> layers;
        layers.reserve(image->sprites.size());
        for (const auto& reference : image->sprites) {
            const auto& frame = sprites.at({static_cast<std::uint16_t>(reference.sprite_id), reference.frame});
            const auto placement = odh::place_sprite(camera, reference, frame, state.object_tile,
                finite_float((state.object_direction%2==0?state.object_direction:0) * std::acos(-1.0) / 4));
            layers.push_back({&frame, placement.x, placement.y, placement.back_depth, placement.front_depth_delta, placement.flip});
        }
        auto composite=odh::composite_sprites(base,layers);
        if(anchors)*anchors=prepared_anchors;
        return composite;
    }
};

SceneRenderer::SceneRenderer(const SceneConfig& config, SceneCfpResolver resolver) : impl_(std::make_unique<Impl>(config,std::move(resolver))) {}
SceneRenderer::~SceneRenderer() = default;
SceneRenderer::SceneRenderer(SceneRenderer&&) noexcept = default;
SceneRenderer& SceneRenderer::operator=(SceneRenderer&&) noexcept = default;

odh::CompositeFrame SceneRenderer::render(const SceneState& state, const odh::WorldProjection& camera, odh::RasterOptions options, ScenePoseAnchors* anchors) {
    if (!impl_) throw odh::FormatError("scene renderer used after move");
    return impl_->render(state,camera,options,nullptr,anchors);
}

odh::CompositeFrame SceneRenderer::render_head_portrait(const SceneState& state,
    HeadPortraitCamera camera,odh::RasterOptions options) {
    if(!impl_)throw odh::FormatError("scene renderer used after move");
    auto portrait_state=state;
    portrait_state.avatar_tile={0,0,0};
    portrait_state.avatar_direction=0;
    portrait_state.head_only=true;
    return impl_->render(portrait_state,odh::WorldProjection{},options,&camera);
}
std::uint64_t SceneRenderer::cache_samples() const { return impl_ ? impl_->sample_count : 0; }
std::size_t SceneRenderer::cache_clips() const { return impl_ ? impl_->clips.size() : 0; }
std::uint64_t SceneRenderer::vertices() const { return impl_ ? impl_->vertex_count : 0; }
std::uint64_t SceneRenderer::triangles() const { return impl_ ? impl_->triangle_count : 0; }
std::uint64_t SceneRenderer::texture_bytes() const { return impl_ ? impl_->texture_size : 0; }
std::uint64_t SceneRenderer::sprite_bytes() const { return impl_ ? impl_->sprite_size : 0; }
