#include "odh/template_person.hpp"

#include "odh/animation.hpp"
#include "odh/formats.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <sstream>

namespace odh {
namespace {
constexpr std::uint32_t kPersonGuid = 0x7FD96B54;
constexpr std::uint16_t kPersonObjd = 128;
constexpr std::uint16_t kBodyStrings = 200;
constexpr std::uint16_t kCatalogStrings = 2000;
constexpr std::uint64_t kHeaderSize = 64;
constexpr std::uint64_t kChunkHeaderSize = 76;
constexpr std::uint64_t kGuidOffset = 28;
// OBJD136/138 use a 32-bit version followed by 16-bit fields. The
// VMOBJDVariable numbering includes both version words: CatalogStringsID41
// is byte82; byte80 is the distinct FunctionFlags/BuyModeType40 field.
constexpr std::uint64_t kCatalogStringsOffset = 82;

struct ChunkData {
    std::string type;
    std::uint16_t id = 0;
    std::uint16_t flags = 0;
    std::string label;
    std::vector<std::uint8_t> payload;
};

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t p) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= static_cast<std::uint32_t>(b.at(p + i)) << (8 * i);
    return result;
}
void put_le16(std::vector<std::uint8_t>& b, std::size_t p, std::uint16_t value) {
    b.at(p) = static_cast<std::uint8_t>(value);
    b.at(p + 1) = static_cast<std::uint8_t>(value >> 8);
}
void put_le32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) b.at(p + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
void put_be32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) b.at(p + i) = static_cast<std::uint8_t>(value >> (8 * (3 - i)));
}
void put_be16(std::vector<std::uint8_t>& b, std::size_t p, std::uint16_t value) {
    b.at(p) = static_cast<std::uint8_t>(value >> 8);
    b.at(p + 1) = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> read_bytes(const View& view) {
    if (view.size() > 16 * 1024 * 1024) throw FormatError("template chunk exceeds configured byte limit");
    std::vector<std::uint8_t> out(static_cast<std::size_t>(view.size()));
    view.read(0, out.data(), out.size());
    return out;
}

void validate_string_bytes(const std::string& value, const char* field) {
    if (value.find('\0') != std::string::npos) throw FormatError(std::string(field) + " contains an embedded NUL");
}

std::vector<std::uint8_t> encode_str3(const std::vector<StringEntry>& entries,
                                     const std::vector<std::uint8_t>& padding) {
    if (entries.size() > std::numeric_limits<std::uint16_t>::max())
        throw FormatError("TS1 string table has too many entries");
    std::vector<std::uint8_t> out;
    out.reserve(4 + padding.size());
    out.push_back(0xFD); out.push_back(0xFF); // signed -3
    out.push_back(static_cast<std::uint8_t>(entries.size()));
    out.push_back(static_cast<std::uint8_t>(entries.size() >> 8));
    for (const auto& entry : entries) {
        validate_string_bytes(entry.value, "TS1 string value");
        validate_string_bytes(entry.comment, "TS1 string comment");
        out.push_back(entry.language);
        out.insert(out.end(), entry.value.begin(), entry.value.end()); out.push_back(0);
        out.insert(out.end(), entry.comment.begin(), entry.comment.end()); out.push_back(0);
    }
    out.insert(out.end(), padding.begin(), padding.end());
    return out;
}

std::vector<std::uint8_t> patch_body_strings(const ChunkData& chunk,
                                             const std::vector<TemplateBodyString>& replacements,bool authenticated_source) {
    std::string raw(reinterpret_cast<const char*>(chunk.payload.data()), chunk.payload.size());
    std::istringstream stream(raw, std::ios::in | std::ios::binary);
    const auto table = read_strings(View(stream, chunk.payload.size()));
    if (table.state != StringTableState::decoded || table.format != -3)
        throw FormatError("TemplatePerson STR# 200 is not a decoded TS1 -3 table");
    if (table.remaining_bytes != table.entries.size())
        throw FormatError("TemplatePerson STR# 200 has unexpected trailing extent");
    std::vector<std::uint8_t> padding(static_cast<std::size_t>(table.remaining_bytes), 0);
    if (!padding.empty()) {
        std::istringstream input(raw, std::ios::in | std::ios::binary);
        View view(input, raw.size());
        const auto start = view.size() - table.remaining_bytes;
        view.read(start, padding.data(), padding.size());
        if (!std::all_of(padding.begin(), padding.end(), [](std::uint8_t v) { return v == 0xA3; }))
            throw FormatError("TemplatePerson STR# 200 has unrecognized padding");
    }
    auto entries = table.entries;
    for (const auto& replacement : replacements) {
        if(replacement.index>=entries.size()){
            // Original templates contain27 human /34 pet default-language
            // strings. Authenticated Make19 writes the wardrobe tail through
            // index34; preserve old fields/comments/padding and add only that
            // bounded missing suffix. Legacy CAS requests retain strict bounds.
            if(!authenticated_source||entries.size()<27||replacement.index>=35||
               !std::all_of(entries.begin(),entries.end(),[](const auto& e){return e.language==1;}))
                throw FormatError("body-string index is outside authenticated template extension");
            while(entries.size()<=replacement.index){entries.push_back({1,"",""});padding.push_back(0xA3);}
        }
        validate_string_bytes(replacement.bytes, "body-string replacement");
        if (entries[replacement.index].language != 1)
            throw FormatError("TemplatePerson body-string index is not in the English source set");
        entries[replacement.index].value = replacement.bytes;
    }
    return encode_str3(entries, padding);
}

std::vector<std::uint8_t> make_catalog_strings(const std::string& name,
                                               const std::string& biography,
                                               std::uint8_t language) {
    validate_string_bytes(name, "character name");
    validate_string_bytes(biography, "character biography");
    return encode_str3({StringEntry{language, name, ""}, StringEntry{language, biography, ""}},
                       {0xA3, 0xA3});
}

std::vector<ChunkData> parse_chunks(const std::vector<std::uint8_t>& bytes) {
    std::string raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(raw, std::ios::in | std::ios::binary);
    View view(stream, bytes.size());
    std::vector<ChunkData> chunks;
    iff_chunks(view, [&](const Chunk& chunk) {
        chunks.push_back({chunk.type, chunk.id, chunk.flags, chunk.label, read_bytes(chunk.payload)});
    });
    return chunks;
}
} // namespace

std::vector<std::uint8_t> clone_template_person(const std::vector<std::uint8_t>& template_iff,
                                                const TemplatePersonRequest& request) {
    const auto source_guid=request.source?request.source->guid:kPersonGuid;
    const auto source_objd=request.source?request.source->definition_chunk_id:kPersonObjd;
    const auto source_body=request.source?request.source->body_strings_id:kBodyStrings;
    if(!source_guid||request.guid==0||request.guid==source_guid)throw FormatError("new person GUID is invalid or unchanged");
    if (request.language == 0 || request.language > 20) throw FormatError("TS1 language code must be 1..20");
    if (template_iff.size() < kHeaderSize || template_iff.size() > request.maximum_iff_bytes)
        throw FormatError("template IFF is outside configured size bounds");
    if (request.object_label.size() > 63 || request.object_label.find('\0') != std::string::npos)
        throw FormatError("person IFF label is invalid");
    if (request.maximum_iff_bytes > 64 * 1024 * 1024)
        throw FormatError("configured template IFF limit is too large");
    std::set<std::uint16_t> body_indices;
    for (const auto& item : request.body_strings)
        if (!body_indices.insert(item.index).second) throw FormatError("duplicate body-string replacement index");

    auto chunks = parse_chunks(template_iff);
    ChunkData* person = nullptr;
    ChunkData* body = nullptr;
    ChunkData* catalog = nullptr;
    for (auto& chunk : chunks) {
        if (chunk.type == "OBJD" && chunk.id == source_objd) {
            if (person) throw FormatError("ambiguous TemplatePerson OBJD 128");
            person = &chunk;
        }
        if (chunk.type == "STR#" && chunk.id == source_body) {
            if (body) throw FormatError("ambiguous TemplatePerson STR# 200");
            body = &chunk;
        }
        if (chunk.type == "CTSS" && chunk.id == kCatalogStrings) {
            if (catalog) throw FormatError("ambiguous TemplatePerson CTSS 2000");
            catalog = &chunk;
        }
    }
    if (!person || person->payload.size() < kCatalogStringsOffset + 2)
        throw FormatError("TemplatePerson OBJD 128 is missing or too short");
    const auto version=le32(person->payload,0);
    if(version!=136&&version!=138)
        throw FormatError("TemplatePerson OBJD metadata version is unsupported");
    if (le32(person->payload, static_cast<std::size_t>(kGuidOffset)) != source_guid)
        throw FormatError("TemplatePerson OBJD 128 has an unexpected source GUID");
    if (!body) throw FormatError("source person BodySTR table is missing");
    if(request.source&&(person->payload.size()<40||
       static_cast<std::uint16_t>(person->payload[38]|(static_cast<unsigned>(person->payload[39])<<8))!=source_body))
        throw FormatError("source person BodyStringID differs from authenticated template table");

    put_le32(person->payload, static_cast<std::size_t>(kGuidOffset), request.guid);
    put_le16(person->payload, static_cast<std::size_t>(kCatalogStringsOffset), kCatalogStrings);
    person->label = request.object_label;
    body->payload = patch_body_strings(*body, request.body_strings,request.source.has_value());
    const auto catalog_payload = make_catalog_strings(request.name, request.biography, request.language);
    if (catalog) catalog->payload = catalog_payload;
    else chunks.push_back({"CTSS", kCatalogStrings, 0x10, "", catalog_payload});

    std::uint64_t total = kHeaderSize;
    for (const auto& chunk : chunks) {
        const auto size = kChunkHeaderSize + chunk.payload.size();
        if (size > request.maximum_iff_bytes - total || size > std::numeric_limits<std::uint32_t>::max())
            throw FormatError("generated character IFF exceeds configured size limit");
        total += size;
    }
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(total));
    output.insert(output.end(), template_iff.begin(), template_iff.begin() + 60);
    output.insert(output.end(), 4, 0); // No resource map is emitted by this bounded writer.
    for (const auto& chunk : chunks) {
        output.insert(output.end(), chunk.type.begin(), chunk.type.end());
        const auto start = output.size(); output.resize(start + 8);
        put_be32(output, start, static_cast<std::uint32_t>(kChunkHeaderSize + chunk.payload.size()));
        put_be16(output, start + 4, chunk.id); put_be16(output, start + 6, chunk.flags);
        const auto label_start = output.size(); output.resize(label_start + 64, 0);
        std::copy(chunk.label.begin(), chunk.label.end(), output.begin() + static_cast<std::ptrdiff_t>(label_start));
        output.insert(output.end(), chunk.payload.begin(), chunk.payload.end());
    }
    return output;
}
} // namespace odh
