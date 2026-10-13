#include "odh/bitmap.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    if(argc!=3&&argc!=4){std::cerr<<"Usage: odh-bitmap FAR BMP_ENTRY [RGBA_OUTPUT]\n";return 2;}
    try {
        std::optional<odh::BitmapImage> image;
        with_far_entry(argv[1],argv[2],[&](const auto& view){image=odh::read_bmp(view);});
        if(argc==4){
            static_assert(sizeof(std::array<std::uint8_t,4>)==4);
            std::ofstream output(argv[3],std::ios::binary|std::ios::trunc);
            if(!output)throw odh::FormatError("cannot open RGBA output");
            output.write(reinterpret_cast<const char*>(image->pixels.data()),
                         static_cast<std::streamsize>(image->pixels.size()*4));
            output.close();if(!output)throw odh::FormatError("cannot write RGBA output");
        }
        std::cout<<"{\"evidence\":\"ODH_BMP_opaque_RGBA_decode\",\"archive\":"<<json_bytes(argv[1])
            <<",\"entry\":"<<json_bytes(argv[2])<<",\"width\":"<<image->width<<",\"height\":"<<image->height
            <<",\"bit_count\":"<<image->bit_count<<",\"original_top_down\":"<<(image->top_down?"true":"false")
            <<",\"decoded_bytes\":"<<image->pixels.size()*4<<",\"remaining_bytes\":"<<image->remaining_bytes<<"}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"odh-bitmap: "<<error.what()<<'\n';return 2;}
}
