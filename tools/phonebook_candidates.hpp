// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace odh_host {
struct PhonebookNeighbor {
    std::int16_t id=0;
    std::uint32_t guid=0;
    bool person_data_available=false;
    std::int16_t family=0,gender=0;
};
struct PhonebookSnapshot {
    bool caller_current=false,relationships_current=false;
    // Preserve the authority's enumeration order.
    // This helper never sorts contacts or adds a reciprocity/score filter.
    std::vector<std::int32_t> relationship_keys;
    std::vector<PhonebookNeighbor> neighbors;
};
struct PhonebookContact {std::string label;std::int32_t value=0;};
struct PhonebookSelection {bool ready=false;std::vector<PhonebookContact> choices;std::string diagnostic;};
using PhonebookName=std::function<std::optional<std::string>(std::int16_t,std::uint32_t)>;
PhonebookSelection select_phonebook_choices(const PhonebookSnapshot&,const PhonebookName&,
    std::size_t max_choices=4096,std::size_t max_label_bytes=1048576);
// Alex TS1PhoneBook: code1/positive offered NID, or code0/text-1 for Cancel.
struct PhonebookResponse {std::uint8_t code=0;std::string text;};
std::optional<PhonebookResponse> phonebook_response(const PhonebookSelection&,std::optional<std::size_t> selected);
std::optional<PhonebookResponse> phonebook_button_response(const PhonebookSelection&,std::size_t button,std::optional<std::size_t> selected);
}
