// SPDX-License-Identifier: MPL-2.0
// Pinned FreeSO 7e76c196 CARR / TS1JobProvider; no TSO outfit table.
#pragma once
#include "odh/formats.hpp"
#include <map>
#include <utility>
namespace odh {
struct CareerLimits {
    std::uint64_t payload_bytes=4U*1024U*1024U;
    std::uint64_t string_bytes=256U*1024U;
    std::uint32_t string_length=4096,levels=1024,decoded_fields=65536,careers=4096;
};
struct CareerLevel {
    std::array<std::int32_t,10> minimum_required{};
    std::array<std::int32_t,7> motive_delta{};
    std::int32_t salary=0,start_hour=0,end_hour=0,car_type=0;
    // IoBuffer.ReadNullTerminatedString maps bytes directly to characters;
    // preserve those bytes rather than inventing a locale/UTF8 conversion.
    std::string job_name,male_uniform_mesh,female_uniform_mesh,uniform_skin,unknown;
    std::array<std::optional<std::uint8_t>,5> string_padding{};
};
struct Career {
    std::uint32_t padding=0,version=0,magic=0;
    std::uint8_t compression=0;
    std::string name;
    std::optional<std::uint8_t> name_padding;
    std::vector<CareerLevel> levels;
    // Known schema ends here; suffix bytes/bits are not interpreted or lost.
    std::uint64_t consumed_bits=0,remaining_bits=0;
    std::vector<std::uint8_t> suffix;
};
Career read_carr(const View&,const CareerLimits& = {});
enum class CareerLookupStatus { found,missing_career,invalid_level };
struct CareerLookup {
    CareerLookupStatus status=CareerLookupStatus::missing_career;
    const CareerLevel* level=nullptr;
};
class CareerCatalog {
public:
    // Chunk IDs come from the one exact Work.iff selected by the host. No
    // archive search, sorting-based job renumbering or duplicate precedence.
    explicit CareerCatalog(std::vector<std::pair<std::uint16_t,Career>>,const CareerLimits& = {});
    CareerLookup lookup(std::int16_t job_type,std::int32_t level) const noexcept;
    const Career* find(std::uint16_t id) const noexcept;
    // Source TS1JobProvider/CARR.GetJobData: a missing career returns zero;
    // an existing career validates its level before even the level-count or
    // constant-zero fields. Returns the signed low16 of the source int value.
    std::optional<std::int16_t> job_data(std::uint16_t job,std::int16_t level,
        std::int16_t field) const noexcept;
    // Source first CARR chunk in original archive order whose unsigned ID exceeds current.
    std::int16_t next(std::int16_t current) const noexcept;
    const std::map<std::uint16_t,Career>& careers() const noexcept {return careers_;}
private:
    std::map<std::uint16_t,Career> careers_;
    std::vector<std::uint16_t> enumeration_order_;
};
} // namespace odh
