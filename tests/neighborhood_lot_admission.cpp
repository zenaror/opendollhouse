#include "neighborhood_lot_admission.hpp"
#include <iostream>
#include <stdexcept>
int main(){try{
    using U=odh_host::NeighborhoodLotUse;
    for(const auto house:{21,31,40,49,58,93,98})
        if(odh_host::neighborhood_lot_use(house,{})!=U::community)throw std::runtime_error("missing source row rejected community lot");
    for(const auto house:{81,89})
        if(odh_host::neighborhood_lot_use(house,{})!=U::studio)throw std::runtime_error("studio default missing");
    for(const auto house:{58,81,90,91,92}){
        if(odh_host::neighborhood_lot_use(house,0)!=U::residential)throw std::runtime_error("explicit residential override lost");
        if(odh_host::neighborhood_lot_use(house,1)!=U::community)throw std::runtime_error("explicit community override lost");
    }
    for(const auto house:{0,99}){
        bool rejected=false;try{(void)odh_host::neighborhood_lot_use(house,{});}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("invalid map selector accepted");
    }
    bool rejected=false;try{(void)odh_host::neighborhood_lot_use(58,7);}catch(const std::invalid_argument&){rejected=true;}
    if(!rejected)throw std::runtime_error("invalid explicit zone accepted");
    std::cout<<"neighborhood lot admission source cases passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
