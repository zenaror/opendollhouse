#include "odh/bootstrap.hpp"

namespace odh {
std::optional<std::int16_t> Ts1BootstrapGlobals::read(std::int16_t index) const {
    for(std::size_t n=0;n<indices.size();++n)if(indices[n]==index)return values_[n];
    return std::nullopt;
}
bool Ts1BootstrapGlobals::write(std::int16_t index,std::int16_t value) {
    for(std::size_t n=0;n<indices.size();++n)if(indices[n]==index){values_[n]=value;return true;}
    return false;
}
}
