#include "session_resource_manifest.hpp"

#include "odh/sha256.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#if defined(ODH_HAVE_OPENSSL_SHA256) && !defined(ODH_RESOURCE_SHA256_FORCE_PORTABLE)
#include <openssl/evp.h>
#endif

namespace odh_host {
struct ResourceSha256::State {
#if defined(ODH_HAVE_OPENSSL_SHA256) && !defined(ODH_RESOURCE_SHA256_FORCE_PORTABLE)
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest{EVP_MD_CTX_new(),EVP_MD_CTX_free};
    State() {
        if (!digest || EVP_DigestInit_ex(digest.get(),EVP_sha256(),nullptr)!=1)
            throw odh::FormatError("cannot initialize SHA-256 for game resource fingerprinting");
    }
#else
    odh::Sha256 digest;
#endif
    std::uint64_t total_bytes=0;
    bool finished=false;
};
ResourceSha256::ResourceSha256() : state_(std::make_unique<State>()) {}
ResourceSha256::~ResourceSha256() = default;
void ResourceSha256::update(const void* data,std::size_t size) {
    auto& state=*state_;
    if(state.finished)throw odh::FormatError("SHA-256 state is already finalized");
    if(size && !data)throw odh::FormatError("SHA-256 input is null");
    if(size>std::numeric_limits<std::uint64_t>::max()-state.total_bytes)
        throw odh::FormatError("SHA-256 input length exceeds limit");
#if defined(ODH_HAVE_OPENSSL_SHA256) && !defined(ODH_RESOURCE_SHA256_FORCE_PORTABLE)
    if(size && EVP_DigestUpdate(state.digest.get(),data,size)!=1)
        throw odh::FormatError("cannot update SHA-256 for game resource fingerprinting");
#else
    state.digest.update(data,size);
#endif
    state.total_bytes+=static_cast<std::uint64_t>(size);
}
std::array<std::uint8_t,32> ResourceSha256::finish() {
    auto& state=*state_;
    if(state.finished)throw odh::FormatError("SHA-256 state is already finalized");
    if(state.total_bytes>std::numeric_limits<std::uint64_t>::max()/8u)
        throw odh::FormatError("SHA-256 bit length exceeds limit");
#if defined(ODH_HAVE_OPENSSL_SHA256) && !defined(ODH_RESOURCE_SHA256_FORCE_PORTABLE)
    std::array<std::uint8_t,32> result{};unsigned int count=0;
    if(EVP_DigestFinal_ex(state.digest.get(),result.data(),&count)!=1 || count!=result.size())
        throw odh::FormatError("cannot finish SHA-256 for game resource fingerprinting");
#else
    const auto result=state.digest.finish();
#endif
    state.finished=true;
    return result;
}
namespace {
constexpr std::size_t maximum_files = 4096;
constexpr std::size_t maximum_path_bytes = 4096;
constexpr std::size_t read_block_bytes = 1024 * 1024;

struct Resource {
    std::string relative_path;
    std::filesystem::path resolved_path;
    std::uint64_t size = 0;
};

bool is_within(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto root_part = root.begin();
    auto path_part = path.begin();
    for (; root_part != root.end(); ++root_part, ++path_part)
        if (path_part == path.end() || *root_part != *path_part) return false;
    return path_part != path.end();
}

void append_u32(odh::Sha256& digest, std::uint32_t value) {
    std::array<std::uint8_t,4> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> (8u * i));
    digest.update(bytes.data(), bytes.size());
}

void append_u64(odh::Sha256& digest, std::uint64_t value) {
    std::array<std::uint8_t,8> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> (8u * i));
    digest.update(bytes.data(), bytes.size());
}

std::array<std::uint8_t,32> hash_file(const Resource& resource) {
    std::ifstream input(resource.resolved_path, std::ios::binary);
    if (!input) throw odh::FormatError("cannot open game resource for fingerprinting");
    ResourceSha256 digest;
    std::array<char,read_block_bytes> buffer{};
    std::uint64_t bytes_read = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count < 0) throw odh::FormatError("cannot read game resource for fingerprinting");
        if (count != 0) {
            const auto length = static_cast<std::uint64_t>(count);
            if (length > std::numeric_limits<std::uint64_t>::max() - bytes_read)
                throw odh::FormatError("game resource size exceeds fingerprint limit");
            bytes_read += length;
            digest.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (!input.eof()) throw odh::FormatError("cannot read game resource for fingerprinting");
    if (bytes_read != resource.size)
        throw odh::FormatError("game resource changed while fingerprinting");
    std::error_code error;
    const auto size_after = std::filesystem::file_size(resource.resolved_path, error);
    if (error || size_after != resource.size)
        throw odh::FormatError("game resource changed while fingerprinting");
    return digest.finish();
}
}

odh::SessionResourceIdentity identify_session_resources(
        const std::filesystem::path& game_root,
        const std::vector<std::filesystem::path>& resources,
        std::uint8_t language_code, std::uint8_t expansion_mask,
        std::uint64_t maximum_total_bytes) {
    odh::SessionResourceIdentity identity;
    identity.language_code = language_code;
    identity.expansion_mask = expansion_mask;
    if (language_code < 1 || language_code > 20)
        throw odh::FormatError("session resource identity has an unsupported language");
    if ((expansion_mask & 0x80u) != 0)
        throw odh::FormatError("session resource identity has unsupported expansion flags");
    if (resources.empty() || resources.size() > maximum_files)
        throw odh::FormatError("game resource count is empty or exceeds fingerprint limit");

    std::error_code error;
    const auto root = std::filesystem::canonical(game_root, error);
    if (error || !std::filesystem::is_directory(root))
        throw odh::FormatError("game root is not an accessible directory for fingerprinting");

    std::vector<Resource> ordered;
    ordered.reserve(resources.size());
    std::uint64_t total_bytes = 0;
    for (const auto& supplied : resources) {
        const auto supplied_path = supplied.is_absolute() ? supplied : root / supplied;
        const auto resolved = std::filesystem::canonical(supplied_path, error);
        if (error || !is_within(root, resolved) || !std::filesystem::is_regular_file(resolved))
            throw odh::FormatError("game resource is missing, not regular, or outside the installation");
        const auto relative = resolved.lexically_relative(root).generic_u8string();
        if (relative.empty() || relative.size() > maximum_path_bytes)
            throw odh::FormatError("game resource path is empty or exceeds fingerprint limit");
        const auto size = std::filesystem::file_size(resolved, error);
        if (error || size > maximum_total_bytes || size > maximum_total_bytes - total_bytes)
            throw odh::FormatError("game resources exceed fingerprint byte limit");
        total_bytes += size;
        ordered.push_back({relative, resolved, size});
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.relative_path < right.relative_path;
    });
    for (std::size_t i = 1; i < ordered.size(); ++i)
        if (ordered[i - 1].relative_path == ordered[i].relative_path)
            throw odh::FormatError("duplicate game resource path in fingerprint manifest");

    odh::Sha256 manifest;
    static constexpr char domain[] = "ODH-RESOURCE-MANIFEST-V1\0";
    manifest.update(domain, sizeof(domain) - 1);
    append_u32(manifest, static_cast<std::uint32_t>(ordered.size()));
    for (const auto& resource : ordered) {
        const auto file_digest = hash_file(resource);
        append_u32(manifest, static_cast<std::uint32_t>(resource.relative_path.size()));
        manifest.update(resource.relative_path.data(), resource.relative_path.size());
        append_u64(manifest, resource.size);
        manifest.update(file_digest.data(), file_digest.size());
    }
    identity.resource_fingerprint = manifest.finish();
    return identity;
}

} // namespace odh_host
