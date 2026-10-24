#include "odh/mesh.hpp"
#include "read_cursor.hpp"

#include <charconv>
#include <sstream>

namespace odh {
namespace {
template<class T>
std::uint32_t bounded(detail::Cursor& c,std::uint32_t count,
                      std::uint32_t& budget,std::uint64_t stride,
                      std::uint64_t fixed=0) {
    if(count>budget || c.remaining()<fixed ||
       count>(c.remaining()-fixed)/stride ||
       count>std::vector<T>{}.max_size())
        throw FormatError("mesh count exceeds payload or configured allocation budget");
    budget-=count;
    return count;
}
template<class T>
std::uint32_t signed_count(detail::Cursor& c,std::uint32_t& budget,
                           std::uint64_t stride,std::uint64_t fixed=0) {
    const auto count=c.i32();
    if(count<0)throw FormatError("negative mesh count");
    return bounded<T>(c,static_cast<std::uint32_t>(count),budget,stride,fixed);
}
void transform(detail::Cursor& c,MeshVertex& vertex) {
    for(auto& bits:vertex.position_bits)bits=c.u32();
    for(auto& bits:vertex.normal_bits)bits=c.u32();
}

class CmxCursor {
public:
    explicit CmxCursor(const std::string& input) {
        std::istringstream lines(input);
        std::string line;
        while (std::getline(lines,line)) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            lines_.push_back(std::move(line));
        }
        if (!lines_.empty() && lines_[0].size()>=3 &&
            static_cast<unsigned char>(lines_[0][0])==0xEF &&
            static_cast<unsigned char>(lines_[0][1])==0xBB &&
            static_cast<unsigned char>(lines_[0][2])==0xBF)
            lines_[0].erase(0,3);
        while (line_ < lines_.size() && lines_[line_].rfind("version ",0)!=0) ++line_;
        if (line_ == lines_.size()) throw FormatError("CMX version declaration is missing");
        ++line_;
    }

    std::string text() {
        if (line_ >= lines_.size()) throw FormatError("truncated CMX text field");
        return lines_[line_++];
    }

    std::int32_t i32() {
        const auto value = number();
        std::int32_t parsed=0;
        const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
        if (result.ec!=std::errc{}||result.ptr!=value.data()+value.size())
            throw FormatError("invalid CMX signed number");
        return parsed;
    }

    std::uint32_t u32() {
        const auto value = number();
        std::uint32_t parsed=0;
        const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
        if (result.ec!=std::errc{}||result.ptr!=value.data()+value.size())
            throw FormatError("invalid CMX unsigned number");
        return parsed;
    }

    void require_end() {
        if (token_ < tokens_.size()) throw FormatError("unexpected CMX numeric data after animation section");
        for (;line_<lines_.size();++line_)
            if (lines_[line_].find_first_not_of(" \t")!=std::string::npos)
                throw FormatError("unexpected CMX text after animation section");
    }

private:
    std::string number() {
        while (token_ >= tokens_.size()) {
            if (line_ >= lines_.size()) throw FormatError("truncated CMX numeric field");
            std::istringstream row(lines_[line_++]);
            tokens_.clear(); token_=0;
            std::string value;
            while (row>>value) tokens_.push_back(std::move(value));
            if (tokens_.empty()) throw FormatError("empty CMX numeric line");
        }
        return tokens_[token_++];
    }

    std::vector<std::string> lines_;
    std::vector<std::string> tokens_;
    std::size_t line_=0,token_=0;
};
}

Mesh read_bmf(const View& view,MeshLimits limits) {
    detail::Cursor c(view,limits.text_bytes);
    Mesh result{};
    result.skin_name=c.pascal();result.texture_name=c.pascal();
    const auto bones=signed_count<std::string>(c,limits.bones,1);
    result.bones.reserve(bones);
    for(std::uint32_t i=0;i<bones;++i)result.bones.push_back(c.pascal());
    const auto faces=signed_count<std::array<std::int32_t,3>>(c,limits.faces,12);
    result.faces.resize(faces);
    for(auto& face:result.faces)for(auto& index:face)index=c.i32();
    const auto bindings=signed_count<MeshBoneBinding>(c,limits.bindings,20);
    result.bindings.reserve(bindings);
    for(std::uint32_t i=0;i<bindings;++i)result.bindings.push_back(
        {c.i32(),c.i32(),c.i32(),c.i32(),c.i32()});
    // Each real vertex needs both its UV pair and its later transform. Check
    // both regions before allocating, including the two intervening fields.
    const auto real=signed_count<MeshVertex>(c,limits.vertices,32,8);
    result.real_vertices.resize(real);
    for(auto& vertex:result.real_vertices)
        for(auto& bits:vertex.texcoord_bits)bits=c.u32();
    // Blend entries and transforms are separate arrays on disk. The fixed
    // extent accounts for vertex_count2 and all previously declared real data.
    const auto blend=signed_count<MeshBlend>(c,limits.blends,32,
                                            4+std::uint64_t{real}*24);
    if(blend>std::vector<MeshVertex>{}.max_size())
        throw FormatError("blend vertex allocation budget exceeded");
    result.blend_data.reserve(blend);result.blend_vertices.resize(blend);
    for(std::uint32_t i=0;i<blend;++i)result.blend_data.push_back({c.i32(),c.i32()});
    result.vertex_count2=c.i32();
    for(auto& vertex:result.real_vertices)transform(c,vertex);
    for(auto& vertex:result.blend_vertices)transform(c,vertex);
    result.remaining_bytes=c.remaining();
    return result;
}

AppearanceCollection read_bcf_appearances(const View& view,MeshLimits limits) {
    detail::Cursor c(view,limits.text_bytes);
    if(c.u32()!=0)throw FormatError("BCF skeleton section outside appearance-only profile");
    AppearanceCollection result{};
    const auto count=signed_count<Appearance>(c,limits.appearances,13,4);
    result.appearances.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        Appearance appearance{};
        appearance.name=c.pascal();appearance.type=c.i32();appearance.zero=c.i32();
        const auto raw_bindings=c.u32();
        const auto bindings=bounded<AppearanceBinding>(c,raw_bindings,
                                                       limits.appearance_bindings,10);
        appearance.bindings.reserve(bindings);
        for(std::uint32_t j=0;j<bindings;++j) {
            AppearanceBinding binding{};
            binding.bone=c.pascal();binding.mesh_name=c.pascal();
            binding.censor_flags=c.i32();binding.zero=c.i32();
            appearance.bindings.push_back(std::move(binding));
        }
        result.appearances.push_back(std::move(appearance));
    }
    if(c.i32()!=0)throw FormatError("BCF animation section outside appearance-only profile");
    result.remaining_bytes=c.remaining();
    return result;
}

AppearanceCollection read_cmx_appearances(const View& view,MeshLimits limits) {
    if (view.size()>limits.text_bytes || view.size()>std::string{}.max_size())
        throw FormatError("CMX exceeds configured text limit");
    std::string bytes(static_cast<std::size_t>(view.size()),'\0');
    if (!bytes.empty()) view.read(0,bytes.data(),bytes.size());
    CmxCursor cursor(bytes);
    if (cursor.i32()!=0) throw FormatError("CMX skeleton section outside CAS appearance-only profile");
    const auto count=cursor.i32();
    if (count<0 || static_cast<std::uint32_t>(count)>limits.appearances)
        throw FormatError("CMX appearance count exceeds configured budget");
    AppearanceCollection result;
    result.appearances.reserve(static_cast<std::size_t>(count));
    std::uint32_t binding_budget=limits.appearance_bindings;
    std::size_t text_budget=limits.text_bytes;
    auto take_text=[&]() {
        auto value=cursor.text();
        if (value.size()>text_budget) throw FormatError("CMX text exceeds cumulative byte budget");
        text_budget-=value.size();
        return value;
    };
    for (std::int32_t i=0;i<count;++i) {
        Appearance appearance{};
        appearance.name=take_text();
        appearance.type=cursor.i32();
        appearance.zero=cursor.i32();
        const auto bindings=cursor.u32();
        if (bindings>binding_budget) throw FormatError("CMX binding count exceeds configured budget");
        binding_budget-=bindings;
        appearance.bindings.reserve(bindings);
        for (std::uint32_t j=0;j<bindings;++j) {
            AppearanceBinding binding{};
            binding.bone=take_text();
            binding.mesh_name=take_text();
            binding.censor_flags=cursor.i32();
            binding.zero=cursor.i32();
            appearance.bindings.push_back(std::move(binding));
        }
        result.appearances.push_back(std::move(appearance));
    }
    if (cursor.i32()!=0) throw FormatError("CMX animation section outside CAS appearance-only profile");
    cursor.require_end();
    result.remaining_bytes=0;
    return result;
}
}
