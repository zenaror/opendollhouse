#include "linux_pcm_sink.hpp"

namespace odh_host {
LinuxPcmSink::LinuxPcmSink(std::uint32_t rate,AudioLimits limits):output_({rate,2},limits){}
odh::PcmSinkFormat LinuxPcmSink::format() const {
    const auto f=output_.format();return {f.sample_rate,f.channels};
}
std::size_t LinuxPcmSink::queue(const std::int16_t* samples,std::size_t frames){return output_.queue(samples,frames);}
std::size_t LinuxPcmSink::queued_frames() const {return output_.queued_frames();}
void LinuxPcmSink::pause(){output_.pause();}
void LinuxPcmSink::start(){output_.start();}
void LinuxPcmSink::clear(){output_.clear();}
}
