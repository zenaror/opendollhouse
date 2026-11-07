#include "neighborhood_storage.hpp"

#include <stdexcept>

namespace odh_host {

NeighborhoodStorage::NeighborhoodStorage(unsigned number) : number_(number) {
    if(number<1 || number>8) throw std::invalid_argument("neighborhood number must be between 1 and 8");
}

std::string NeighborhoodStorage::user_data_name() const {
    return number_==1 ? "UserData" : "UserData"+std::to_string(number_);
}

NeighborhoodStorage NeighborhoodStorage::from_user_data(const std::filesystem::path& directory) {
    auto path=directory.lexically_normal();
    if(path.filename().empty()) path=path.parent_path();
    const auto name=path.filename().string();
    if(name=="UserData") return {};
    if(name.size()==9 && name.compare(0,8,"UserData")==0 && name.back()>='2' && name.back()<='8')
        return NeighborhoodStorage(static_cast<unsigned>(name.back()-'0'));
    throw std::invalid_argument("expected installed neighborhood UserData or UserData2 through UserData8");
}

bool NeighborhoodStorage::matches_user_data(const std::filesystem::path& directory) const {
    auto path=directory.lexically_normal();
    if(path.filename().empty()) path=path.parent_path();
    return path.filename()==user_data_name();
}

void NeighborhoodStorage::verify_neighborhood_source(const std::filesystem::path& neighborhood_iff) const {
    const auto directory=neighborhood_iff.parent_path();
    // Older callers provide an explicitly selected standalone IFF for town 1.
    // A named installation folder must always agree with the selected town.
    if((number_!=1 || directory.filename().string().compare(0,8,"UserData")==0) && !matches_user_data(directory))
        throw std::invalid_argument("neighborhood source directory differs from selected storage");
}

std::filesystem::path NeighborhoodStorage::qualify(const std::filesystem::path& base) const {
    if(number_==1) return base;
    const auto path=base/user_data_name();
    const auto status=std::filesystem::symlink_status(path);
    if(std::filesystem::exists(status) && !std::filesystem::is_directory(status))
        throw std::runtime_error("neighborhood storage must be a directory, not a link or file");
    return path;
}

} // namespace odh_host
