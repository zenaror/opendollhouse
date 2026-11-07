#pragma once

#include <filesystem>
#include <string>

namespace odh_host {

// The installed directory selects the save namespace. Content hashes still
// authenticate the commercial data and can legitimately match across towns.
class NeighborhoodStorage {
public:
    NeighborhoodStorage() = default;
    explicit NeighborhoodStorage(unsigned number);
    unsigned number() const { return number_; }
    std::string user_data_name() const;
    static NeighborhoodStorage from_user_data(const std::filesystem::path& directory);
    bool matches_user_data(const std::filesystem::path& directory) const;
    void verify_neighborhood_source(const std::filesystem::path& neighborhood_iff) const;
    std::filesystem::path qualify(const std::filesystem::path& base) const;
private:
    unsigned number_ = 1;
};

} // namespace odh_host
