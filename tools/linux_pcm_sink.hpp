#pragma once
#include "linux_audio.hpp"
#include "odh/pcm_transport.hpp"

namespace odh_host {
// One shared Linux device for a PcmTransport. The transport has exclusive queue
// access and must be destroyed before this sink. Construction leaves it paused.
// Per-note owners use the transport; they never close the shared device.
class LinuxPcmSink final : public odh::PcmSink {
public:
    explicit LinuxPcmSink(std::uint32_t rate=44100,AudioLimits limits={});
    odh::PcmSinkFormat format() const override;
    std::size_t queue(const std::int16_t*,std::size_t) override;
    std::size_t queued_frames() const override;
    void pause() override;
    void start() override;
    void clear() override;
    std::string driver() const {return output_.driver();}
    std::string runtime_version() const {return output_.runtime_version();}
private:
    LinuxAudio output_;
};
}
