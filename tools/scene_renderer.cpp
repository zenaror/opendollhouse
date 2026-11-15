#include "resource.hpp"
#include "scene_renderer.hpp"
#include "odh/skinning.hpp"
#include "odh/mesh.hpp"
#include "odh/bcf.hpp"
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

std::string resource_basename(std::string value){
    std::replace(value.begin(),value.end(),'\\','/');const auto at=value.find_last_of('/');
    return fold(at==std::string::npos?value:value.substr(at+1));
}

float finite_float(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw odh::FormatError("scene renderer coordinate overflow or nonfinite value");
    return static_cast<float>(value);
}

std::array<float, 3> rotate_avatar(std::array<float, 3> point, std::uint8_t direction,
        std::optional<float> radians={}) {
    const double angle = std::acos(-1.0) - (radians?static_cast<double>(*radians):direction * std::acos(-1.0) / 4);
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
    if(camera.viewport_size<16||camera.viewport_size>512||
       !std::isfinite(camera.vertical_fov_radians)||camera.vertical_fov_radians<=0.05||
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
    double focal,near_plane,center;
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
    const auto focal=(camera.viewport_size/2.0)/std::tan(camera.vertical_fov_radians/2.0)*camera.zoom;
    if(!std::isfinite(focal)||focal>1e7)throw odh::FormatError("scene renderer portrait focal length exceeds limit");
    return {position,right,up,forward,focal,camera.near_plane,camera.viewport_size/2.0};
}
std::array<float,3> project_portrait_vertex(const PortraitView& camera,const std::array<float,3>& point){
    const Vector3d relative{double(point[0])-camera.position[0],
        double(point[1])-camera.position[1],double(point[2])-camera.position[2]};
    const auto measured_depth=dot(relative,camera.forward);
    const auto tolerance=1e-5*std::max(1.0,camera.near_plane);
    if(!std::isfinite(measured_depth)||measured_depth<camera.near_plane-tolerance)
        throw odh::FormatError("scene renderer portrait vertex is behind the near plane");
    const auto depth=std::max(measured_depth,camera.near_plane);
    const auto x=camera.center+camera.focal*dot(relative,camera.right)/depth;
    const auto y=camera.center-camera.focal*dot(relative,camera.up)/depth;
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
    explicit Archive(const std::string& path) : input_(path) {
        if (!input_) throw odh::FormatError("scene renderer cannot open archive: " + path);
        size_ = std::filesystem::file_size(path);
        entries_ = shared_far_manifest(path,odh::View(input_, size_));
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
        for (const auto& entry : *entries_) {
            if ((insensitive ? fold(entry.name) : entry.name) != key) continue;
            if (selected) throw odh::FormatError("scene renderer ambiguous archive entry: " + name);
            selected = &entry;
        }
        if (!selected) throw odh::FormatError("scene renderer archive entry not found: " + name);
        return *selected;
    }
    BufferedResourceFile input_;
    std::uint64_t size_ = 0;
    std::shared_ptr<const std::vector<odh::FarEntry>> entries_;
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
    BufferedResourceFile input(path);
    if(!input)throw odh::FormatError("scene renderer cannot open loose CMX: "+path);
    auto result=odh::read_cmx_appearances(odh::View(input,std::filesystem::file_size(path)));
    if(result.remaining_bytes)throw odh::FormatError("scene renderer trailing CMX bytes");
    return result;
}
odh::Mesh loose_skn(const std::string& path) {
    BufferedResourceFile input(path);
    if(!input)throw odh::FormatError("scene renderer cannot open loose SKN: "+path);
    auto result=odh::read_skn(odh::View(input,std::filesystem::file_size(path)));
    if(result.remaining_bytes)throw odh::FormatError("scene renderer trailing SKN bytes");
    return result;
}
odh::BitmapImage loose_bmp(const std::string& path) {
    BufferedResourceFile input(path);
    if(!input)throw odh::FormatError("scene renderer cannot open loose BMP: "+path);
    return odh::read_bmp(odh::View(input,std::filesystem::file_size(path)));
}
} // namespace

static std::array<float,3> align_avatar_normal(const std::array<float,3>& point,const std::array<float,3>& normal){
    const auto dot=[](const auto& a,const auto& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    const auto unit=[&](std::array<float,3> v){const float length=std::sqrt(dot(v,v));if(!std::isfinite(length)||length<1e-6f)
        throw odh::FormatError("avatar ground alignment basis is singular");for(auto& x:v)x/=length;return v;};
    const auto u1=unit(normal);std::array<float,3> u2{0,0,1};const float a=dot(u1,u2);for(unsigned c=0;c<3;++c)u2[c]-=a*u1[c];u2=unit(u2);
    std::array<float,3> u3{1,0,0};const float b=dot(u1,u3),c=dot(u2,u3);for(unsigned i=0;i<3;++i)u3[i]-=b*u1[i]+c*u2[i];u3=unit(u3);
    return {u3[0]*point[0]+u1[0]*point[1]+u2[0]*point[2],u3[1]*point[0]+u1[1]*point[1]+u2[1]*point[2],u3[2]*point[0]+u1[2]*point[1]+u2[2]*point[2]};
}
struct SceneRenderer::Impl {
    std::array<float,3> visual_normal{0,1,0};
    std::optional<std::uint64_t> normal_tick;

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
        std::map<std::string,std::unique_ptr<Archive>> component_archives;
        Archive* rig_source=&animation_archive;
        if(!config.skeleton_far.empty()&&config.skeleton_far!=config.animation_far){
            if(config.skeleton_far.size()>4096||config.skeleton_far.find('\0')!=std::string::npos)
                throw odh::FormatError("scene skeleton archive path exceeds bound");
            auto inserted=component_archives.emplace(config.skeleton_far,std::make_unique<Archive>(config.skeleton_far));
            rig_source=inserted.first->second.get();
        }
        const auto collection=[&]{
            if(config.skeleton_far.empty())return odh::read_bcf_skeletons(rig_source->exact(config.skeleton_entry));
            auto decoded=odh::read_bcf_resources(rig_source->exact(config.skeleton_entry));
            return odh::SkeletonCollection{std::move(decoded.skeletons),decoded.remaining_bytes};
        }();
        if(!config.skeleton_far.empty()&&collection.remaining_bytes)
            throw odh::FormatError("scene renderer trailing skeleton BCF bytes");
        const odh::Skeleton* selected_skeleton = nullptr;
        for (const auto& candidate : collection.skeletons) {
            if (candidate.name != config.skeleton_name) continue;
            if (selected_skeleton) throw odh::FormatError("scene renderer ambiguous skeleton name: " + config.skeleton_name);
            selected_skeleton = &candidate;
        }
        if (!selected_skeleton) throw odh::FormatError("scene renderer exact skeleton name not found: " + config.skeleton_name);
        skeleton = *selected_skeleton;
        Archive texture_archive(config.textures_far);
        const auto archive_for=[&](const std::string& path,Archive& fallback)->Archive&{
            if(path.empty())return fallback;
            if(path.size()>4096||path.find('\0')!=std::string::npos)throw odh::FormatError("scene component archive path exceeds bound");
            if(path==config.animation_far)return animation_archive;
            if(path==config.textures_far)return texture_archive;
            auto found=component_archives.find(path);
            if(found==component_archives.end()){
                if(component_archives.size()>=48)throw odh::FormatError("scene component archive count exceeds bound");
                found=component_archives.emplace(path,std::make_unique<Archive>(path)).first;}
            return *found->second;
        };
        std::uint64_t geometry_vertices = 0, texture_pixels = 0;
        for (const auto& part : config.parts) {
            if (part.hand && (!odh::valid_hand_gesture(part.hand->gesture) || part.hand->gesture == odh::HandGesture::none))
                throw odh::FormatError("scene renderer hand part must specify idle, pointing or fist");
            odh::Mesh mesh;odh::BitmapImage texture;std::string attachment_bone;
            odh::AppearanceCollection appearances;
            if(part.loose_skin_assets){
                appearances=part.appearance_far.empty()?loose_cmx(part.appearance_entry):
                    odh::read_cmx_appearances(archive_for(part.appearance_far,animation_archive).exact(part.appearance_entry));
            }else{
                auto decoded=odh::read_bcf_resources(archive_for(part.appearance_far,animation_archive).exact(part.appearance_entry));
                if(decoded.remaining_bytes)throw odh::FormatError("scene renderer trailing appearance BCF bytes");
                appearances={std::move(decoded.appearances),0};
            }
            const odh::Appearance* appearance=nullptr;
            for(const auto& candidate:appearances.appearances)if(fold(candidate.name)==fold(part.appearance_name)){
                if(appearance)throw odh::FormatError("scene renderer ambiguous selected appearance name");appearance=&candidate;}
            if(!appearance)throw odh::FormatError("scene renderer selected appearance not found: "+part.appearance_name);
            const odh::AppearanceBinding* binding=nullptr;
            const auto selected_mesh=resource_basename(part.bmf_entry);
            for(const auto& candidate:appearance->bindings)
                if(resource_basename(candidate.mesh_name+".bmf")==selected_mesh||
                   resource_basename(candidate.mesh_name+".skn")==selected_mesh){
                    if(binding)throw odh::FormatError("scene renderer ambiguous selected appearance mesh");binding=&candidate;}
            if(!binding)throw odh::FormatError("scene renderer appearance has no selected mesh binding: "+part.bmf_entry);
            attachment_bone=binding->bone;
            if(part.loose_mesh_asset||(part.loose_skin_assets&&part.mesh_far.empty()))mesh=loose_skn(part.bmf_entry);
            else {auto& source=archive_for(part.mesh_far,animation_archive);
                const auto skn=selected_mesh.size()>=4&&selected_mesh.substr(selected_mesh.size()-4)==".skn";
                mesh=skn?odh::read_skn(source.exact(part.bmf_entry)):odh::read_bmf(source.exact(part.bmf_entry));}
            // Identity was checked against the selected resource basename
            // above. The decoded SkinName is exporter metadata, not a second
            // lookup key (shipped TS1 BMFs can retain a different internal name).
            if(part.loose_texture_asset||(part.loose_skin_assets&&part.texture_far.empty()))texture=loose_bmp(part.texture_entry);
            else texture=odh::read_bmp(archive_for(part.texture_far,texture_archive).exact(part.texture_entry));
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
                               ScenePoseAnchors* anchors=nullptr,bool pose_only=false) {
        if ((state.avatar_visible&&state.avatar_direction > 7) || state.object_direction > 7)
            throw odh::FormatError("scene renderer direction must be 0..7");
        if (state.avatar_visible&&(!odh::valid_hand_gesture(state.hands.left) || !odh::valid_hand_gesture(state.hands.right)))
            throw odh::FormatError("scene renderer invalid hand gesture");
        if (state.avatar_visible&&!std::isfinite(state.frame)) throw odh::FormatError("scene renderer frame must be finite");
        if (state.avatar_visible&&state.avatar_radians&&!std::isfinite(*state.avatar_radians))
            throw odh::FormatError("scene renderer continuous direction must be finite");
        // Validate the camera before decoding any CFP. Portraits deliberately
        // use a separate perspective camera and never the lot's tile projection.
        if(portrait)validate_portrait_camera(*portrait);
        else odh::project_tile(camera, state.avatar_visible?state.avatar_tile:camera.center_tile);
        const odh::DgrpImage* image=nullptr;
        if (group&&!pose_only) {
            if(portrait)throw odh::FormatError("scene renderer portrait does not include an object graphic");
            odh::project_tile(camera, state.object_tile);
            image = odh::dgrp_image(*group, static_cast<std::uint8_t>(1U << state.object_direction), camera.zoom, camera.rotation);
            if (!image) throw odh::FormatError("scene renderer drawing direction/zoom missing");
        }
        odh::SkeletonPose pose;
        if(state.avatar_visible){
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
            std::vector<std::shared_ptr<const odh::CfpPlanes>> layer_owners;
            std::vector<odh::PoseClipSample> layer_samples;
            if(!state.animation_layers.empty()){
                if(state.animation_layers.size()>3)throw odh::FormatError("scene animation collection exceeds three clips");
                layer_owners.resize(state.animation_layers.size());
                layer_samples.reserve(state.animation_layers.size());
                for(std::size_t i=0;i<state.animation_layers.size();++i){
                    const auto& layer=state.animation_layers[i];
                    layer_samples.push_back({layer.animation,resolve_planes(layer.animation,layer_owners[i]),layer.frame,layer.weight});
                }
            }
            pose = layer_samples.empty()?
                odh::sample_pose(skeleton,state.animation,cfp,state.frame,state.carry_animation,carry_cfp,state.carry_frame):
                odh::sample_pose_collection(skeleton,layer_samples,state.carry_animation,carry_cfp,state.carry_frame);
        }
        if(state.avatar_visible&&state.head_seek)pose=odh::apply_head_seek(pose,*state.head_seek);
        const bool ground_align=(state.display_flags&4096)&&!portrait&&state.avatar_visible;
        if(ground_align&&state.ground_normal){const auto& normal=*state.ground_normal;
            float length=0;for(float v:normal){if(!std::isfinite(v))throw odh::FormatError("avatar actual ground normal is not finite");length+=v*v;}
            if(std::abs(length-1)>0.001f)throw odh::FormatError("avatar actual ground normal is not normalized");
            (void)align_avatar_normal({0,0,0},normal);}
        ScenePoseAnchors prepared_anchors;
        if(anchors&&state.avatar_visible){
            std::optional<std::size_t> head;
            for(std::size_t i=0;i<pose.bones.size();++i)if(fold(pose.bones[i].name)=="head"){
                if(head)throw odh::FormatError("head seek HEAD bone is ambiguous");head=i;}
            if(head&&pose.bones[*head].parent_index>=0)prepared_anchors.neck_matrix=pose.bones[static_cast<std::size_t>(pose.bones[*head].parent_index)].absolute_matrix;
            constexpr std::array<const char*,3> names{"r_finger0","head","pelvis"};
            for(const auto& bone:pose.bones)for(std::size_t slot=0;slot<names.size();++slot){
                if(fold(bone.name)!=names[slot])continue;
                if(prepared_anchors.avatar_slots[slot])
                    throw odh::FormatError("scene renderer avatar slot bone is ambiguous");
                auto point=rotate_avatar(bone.absolute_position,state.avatar_direction,state.avatar_radians);
                if(slot==1)prepared_anchors.headline_target=std::array<float,3>{finite_float(double(point[0])/3+state.avatar_tile[0]),
                    finite_float(double(point[2])/3+state.avatar_tile[1]),finite_float(double(point[1])/3+state.avatar_tile[2])};
                if(ground_align)point=align_avatar_normal(point,visual_normal);
                prepared_anchors.avatar_slots[slot]=std::array<float,3>{
                    finite_float(double(point[0])/3+state.avatar_tile[0]-.5),
                    finite_float(double(point[2])/3+state.avatar_tile[1]-.5),
                    finite_float(double(point[1])/3+state.avatar_tile[2])};
            }
        }
        if(pose_only){if(anchors)*anchors=prepared_anchors;return {};}
        std::optional<std::array<float,3>> head_position;
        if ((state.head_only || portrait) && state.avatar_visible) {
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
            if (state.head_only) for(std::size_t i=0;i<pose.bones.size();++i)if(i!=*head) {
                pose.bones[i].absolute_matrix.fill(0.0f);
                pose.bones[i].absolute_position.fill(0.0f);
            }
        }
        std::optional<PortraitView> view;
        if(portrait&&state.avatar_visible) {
            if(!head_position)throw odh::FormatError("scene renderer portrait requires a head anchor");
            view=portrait_view(*portrait,*head_position);
        }
        std::vector<odh::RasterTriangle> triangles;
        triangles.reserve(static_cast<std::size_t>(triangle_count)*(portrait?2U:1U));
        for (std::size_t index = 0; state.avatar_visible&&index < meshes.size(); ++index) {
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
                auto world = rotate_avatar(vertex.position, state.avatar_direction,state.avatar_radians);
                if(ground_align)world=align_avatar_normal(world,visual_normal);
                world[0] = finite_float(double(world[0]) + 3 * double(state.avatar_tile[0]));
                world[1] = finite_float(double(world[1]) + 3 * double(state.avatar_tile[2]));
                world[2] = finite_float(double(world[2]) + 3 * double(state.avatar_tile[1]));
                projected.push_back({odh::project_world(camera, world), vertex.texcoord,
                                     ground_align?align_avatar_normal(rotate_avatar(vertex.normal,state.avatar_direction,state.avatar_radians),visual_normal):
                                     rotate_avatar(vertex.normal, state.avatar_direction,state.avatar_radians)});
            }
            for (const auto& face : mesh.faces)
                triangles.push_back({{projected[face[0]], projected[face[1]], projected[face[2]]}, index,
                    state.owner_id?state.owner_id:static_cast<std::uint16_t>(index)});
        }
        if(portrait)options.perspective_correct=true;
        auto base = portrait ? odh::rasterize(portrait->viewport_size,portrait->viewport_size,triangles,textures,options)
                                   : odh::rasterize(camera.width,camera.height,triangles,textures,options);
        if(ground_align&&state.ground_normal&&state.simulation_tick){
            if(normal_tick&&*state.simulation_tick<*normal_tick)throw odh::FormatError("avatar ground presentation clock moved backwards");
            const auto frames=normal_tick?*state.simulation_tick-*normal_tick:0;
            const float retain=static_cast<float>(std::pow(0.80,static_cast<double>(frames)));auto next=visual_normal;float length=0;
            for(unsigned c=0;c<3;++c){next[c]=next[c]+((*state.ground_normal)[c]-next[c])*(1-retain);length+=next[c]*next[c];}
            length=std::sqrt(length);if(!std::isfinite(length)||length<1e-6f)throw odh::FormatError("avatar ground normal interpolation is singular");
            for(auto& v:next)v/=length;visual_normal=next;}
        if(!portrait&&state.simulation_tick)normal_tick=state.simulation_tick;
        const auto color=odh::avatar_display_color(state.display_flags,state.is_ghost);
        if(color!=std::array<std::uint8_t,4>{255,255,255,255})for(std::size_t i=0;i<base.pixels.size();++i)if(std::isfinite(base.depth[i])){
            for(unsigned c=0;c<3;++c){const auto tinted=(unsigned(base.pixels[i][c])*color[c]+127)/255;
                base.pixels[i][c]=static_cast<std::uint8_t>(portrait?(tinted*color[3]+unsigned(options.clear[c])*(255-color[3])+127)/255:tinted);}
        }
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

ScenePoseAnchors SceneRenderer::pose_anchors(const SceneState& state) {
    if(!impl_)throw odh::FormatError("scene renderer used after move");
    ScenePoseAnchors anchors;
    (void)impl_->render(state,odh::WorldProjection{},odh::RasterOptions{},nullptr,&anchors,true);
    return anchors;
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
odh::CompositeFrame SceneRenderer::render_body_portrait(const SceneState& state, HeadPortraitCamera camera) {
    if (!impl_) throw odh::FormatError("scene renderer used after move");
    auto fullBody = state;
    fullBody.head_only = false;
    return impl_->render(fullBody, odh::WorldProjection{}, odh::RasterOptions{}, &camera);
}
std::uint64_t SceneRenderer::cache_samples() const { return impl_ ? impl_->sample_count : 0; }
std::size_t SceneRenderer::cache_clips() const { return impl_ ? impl_->clips.size() : 0; }
std::uint64_t SceneRenderer::vertices() const { return impl_ ? impl_->vertex_count : 0; }
std::uint64_t SceneRenderer::triangles() const { return impl_ ? impl_->triangle_count : 0; }
std::uint64_t SceneRenderer::texture_bytes() const { return impl_ ? impl_->texture_size : 0; }
std::uint64_t SceneRenderer::sprite_bytes() const { return impl_ ? impl_->sprite_size : 0; }
