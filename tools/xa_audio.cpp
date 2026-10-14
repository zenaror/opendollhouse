#include "odh/xa_audio.hpp"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
namespace {
void word(std::ostream& out,std::uint32_t value,unsigned bytes){
    for(unsigned i=0;i<bytes;++i)out.put(static_cast<char>((value>>(i*8))&255));
}
}
int main(int argc,char** argv){try{
    if(argc!=3)throw odh::FormatError("usage: odh-xa-audio INPUT_XA OUTPUT_WAV");
    const std::filesystem::path input=argv[1],output=argv[2];
    if(input.empty()||output.empty()||std::filesystem::weakly_canonical(input)==std::filesystem::weakly_canonical(output)||
       (std::filesystem::exists(output)&&std::filesystem::equivalent(input,output)))
        throw odh::FormatError("XA output aliases input or has an empty path");
    std::ifstream stream(input,std::ios::binary);
    if(!stream)throw odh::FormatError("cannot open XA input");
    const auto decoded=odh::read_xa(odh::View(stream,std::filesystem::file_size(input)));
    if(decoded.samples.size()>(std::numeric_limits<std::uint32_t>::max()-36)/2)
        throw odh::FormatError("XA PCM exceeds RIFF output size");
    const auto bytes=static_cast<std::uint32_t>(decoded.samples.size()*2);
    std::ofstream out(output,std::ios::binary|std::ios::trunc);
    if(!out)throw odh::FormatError("cannot open XA WAV output");
    out.write("RIFF",4);word(out,bytes+36,4);out.write("WAVEfmt ",8);word(out,16,4);
    word(out,1,2);word(out,decoded.channels,2);word(out,decoded.sample_rate,4);
    word(out,decoded.sample_rate*decoded.channels*2,4);word(out,decoded.channels*2,2);word(out,16,2);
    out.write("data",4);word(out,bytes,4);
    for(const auto sample:decoded.samples)word(out,static_cast<std::uint16_t>(sample),2);
    out.close();if(!out)throw odh::FormatError("cannot finish XA WAV output");
    std::cout<<"{\"evidence\":\"XA_PCM_decode_only\",\"sample_rate\":"<<decoded.sample_rate
        <<",\"channels\":"<<decoded.channels<<",\"frames\":"<<decoded.samples.size()/decoded.channels
        <<",\"pcm_bytes\":"<<bytes<<",\"remaining_bytes\":"<<decoded.remaining_bytes<<"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"odh-xa-audio: "<<e.what()<<'\n';return 2;}}
