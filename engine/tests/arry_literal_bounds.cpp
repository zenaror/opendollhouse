// SPDX-License-Identifier: MPL-2.0
#include "files/formats/iff/chunks/ARRY.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using FSO::Files::Formats::IFF::Chunks::ARRY;
static void Need(bool pass,const char* message) { if(!pass) throw std::runtime_error(message); }
static void Word(std::vector<uint8_t>& bytes,uint16_t value) {
    bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value>>8));
}
static void Dword(std::vector<uint8_t>& bytes,uint32_t value) {
    Word(bytes,uint16_t(value)); Word(bytes,uint16_t(value>>16));
}
static std::vector<uint8_t> Header(int width,int height,int stride=1) {
    std::vector<uint8_t> bytes;
    for(uint32_t word : {0u,uint32_t(width),uint32_t(height),uint32_t(stride),0u}) Dword(bytes,word);
    return bytes;
}
static std::vector<uint8_t> Read(const std::vector<uint8_t>& encoded,int64_t* consumed=nullptr) {
    st::Reader stream(encoded);
    ARRY array; array.Read(nullptr,stream);
    Need(stream.Position()<=stream.Length(),"ARRY reader advanced past the encoded payload");
    if(consumed) *consumed=stream.Position();
    return array.Data;
}
int main() { try {
    // Both small and large even literals end at EOF without an extra opcode.
    // The final sample is data, rather than a sentinel or a format-specific G.
    for(int length : {2,6,258,4096}) {
        auto bytes=Header(length,1); Word(bytes,uint16_t(length));
        std::vector<uint8_t> expected;
        for(int i=0;i<length;++i) expected.push_back(uint8_t(i*37+19));
        bytes.insert(bytes.end(),expected.begin(),expected.end());
        Need(Read(bytes)==expected,"final literal byte was omitted");
    }
    // Odd runs consume their alignment byte before another literal opcode.
    {
        auto bytes=Header(4,1); Word(bytes,3);bytes.insert(bytes.end(),{11,22,33,0x99});
        Word(bytes,1);bytes.insert(bytes.end(),{44,0});
        Need(Read(bytes)==std::vector<uint8_t>({11,22,33,44}),"odd literal padding changed the next run");
    }
    // Truncated literal payloads retain available data and leave the remaining
    // initialized output untouched. They must neither overread nor invent bytes.
    for(int available=0;available<6;++available) {
        auto bytes=Header(6,1);Word(bytes,6);
        std::vector<uint8_t> expected(6,0);
        for(int i=0;i<available;++i) {bytes.push_back(uint8_t(i+31));expected[size_t(i)]=uint8_t(i+31);}
        Need(Read(bytes)==expected,"truncated literal did not preserve exactly its available prefix");
    }
    {
        auto bytes=Header(1,1);Word(bytes,1);bytes.push_back(0xa5);
        Need(Read(bytes)==std::vector<uint8_t>({0xa5}),"EOF after odd literal lost its data or read missing padding");
    }
    // Declared literal overrun stops before writing outside the decoded grid.
    {
        auto bytes=Header(2,1);Word(bytes,4);bytes.insert(bytes.end(),{7,8,9,10});
        int64_t consumed=0;
        Need(Read(bytes,&consumed)==std::vector<uint8_t>({7,8}),"literal overrun changed the bounded output prefix");
        Need(consumed==24,"literal overrun consumed a byte before checking output bounds");
    }
    // The literal following a fill uses the same byte bound, while its existing
    // decoded-grid wrap behavior remains intact.
    {
        auto bytes=Header(6,1);Word(bytes,0x8002);bytes.insert(bytes.end(),{0x7f,0});
        Word(bytes,4);bytes.insert(bytes.end(),{1,2,3,4});
        Need(Read(bytes)==std::vector<uint8_t>({0x7f,0x7f,1,2,3,4}),"fill-following literal changed");
        bytes.pop_back();
        Need(Read(bytes)==std::vector<uint8_t>({0x7f,0x7f,1,2,3,0}),"truncated fill-following literal overread");
    }
    {
        auto bytes=Header(4,1);Word(bytes,0x8002);bytes.insert(bytes.end(),{9,0});
        Word(bytes,4);bytes.insert(bytes.end(),{1,2,3,4});
        Need(Read(bytes)==std::vector<uint8_t>({3,4,1,2}),"fill-following literal wrap changed");
    }
    {
        auto bytes=Header(4,1);Word(bytes,0x8001);bytes.insert(bytes.end(),{9,0});
        Word(bytes,3);bytes.insert(bytes.end(),{1,2,3});
        Need(Read(bytes)==std::vector<uint8_t>({9,1,2,3}),"odd fill-following EOF literal overread padding");
    }
    // A trailing one-byte opcode fragment remains unconsumed: opcode lookahead
    // still requires two bytes. This change is local to literal payload bytes.
    {
        auto bytes=Header(2,1);Word(bytes,2);bytes.insert(bytes.end(),{1,2,0x77});
        int64_t consumed=0;
        Need(Read(bytes,&consumed)==std::vector<uint8_t>({1,2}) && consumed==24,
            "literal fix changed two-byte opcode lookahead");
    }
    {
        auto bytes=Header(0,1);Word(bytes,1);bytes.push_back(7);
        int64_t consumed=0;
        Need(Read(bytes,&consumed).empty() && consumed==22,"empty output grid accepted a literal write");
    }
    // Preserve the terminal fill fix, including a single available pad byte.
    {
        auto bytes=Header(4,1);Word(bytes,0x8004);bytes.push_back(0xfe);
        Need(Read(bytes)==std::vector<uint8_t>(4,0xfe),"terminal fill lost its final pad value");
    }
    std::cout<<"ARRY literal bounds passed\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; } }
