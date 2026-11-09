#include "odh/animation.hpp"
#include "odh/formats.hpp"
#include "odh/template_person.hpp"
#include "odh/object_definition.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void add16(std::vector<std::uint8_t>& out, std::uint16_t v) { out.push_back(v); out.push_back(v >> 8); }
void add16be(std::vector<std::uint8_t>& out, std::uint16_t v) { out.push_back(v >> 8); out.push_back(v); }
void add32le(std::vector<std::uint8_t>& out, std::uint32_t v) { for (unsigned i=0;i<4;++i) out.push_back(v >> (8*i)); }
void add32be(std::vector<std::uint8_t>& out, std::uint32_t v) { for (unsigned i=0;i<4;++i) out.push_back(v >> (8*(3-i))); }
void patch32le(std::vector<std::uint8_t>& out, std::size_t p, std::uint32_t v) { for(unsigned i=0;i<4;++i)out.at(p+i)=v>>(8*i); }
void patch16le(std::vector<std::uint8_t>& out, std::size_t p, std::uint16_t v) { out.at(p)=v;out.at(p+1)=v>>8; }
std::vector<std::uint8_t> strings(const std::vector<std::string>& values) {
    std::vector<std::uint8_t> out{0xFD,0xFF}; add16(out,static_cast<std::uint16_t>(values.size()));
    for(const auto& value:values){out.push_back(1);out.insert(out.end(),value.begin(),value.end());out.push_back(0);out.push_back(0);}
    out.insert(out.end(),values.size(),0xA3); return out;
}
void chunk(std::vector<std::uint8_t>& out,const std::string& type,std::uint16_t id,
           std::uint16_t flags,const std::string& label,const std::vector<std::uint8_t>& payload) {
    out.insert(out.end(),type.begin(),type.end()); add32be(out,static_cast<std::uint32_t>(76+payload.size()));
    add16be(out,id);add16be(out,flags);std::vector<std::uint8_t> lab(64,0);
    std::copy(label.begin(),label.end(),lab.begin());out.insert(out.end(),lab.begin(),lab.end());
    out.insert(out.end(),payload.begin(),payload.end());
}
std::vector<std::uint8_t> template_iff(bool duplicate_body=false,std::uint32_t version=138) {
    const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    std::vector<std::uint8_t> out(signature.begin(),signature.end());out.resize(64,0);
    std::vector<std::uint8_t> objd(216,0);patch32le(objd,0,version);patch32le(objd,28,0x7FD96B54);
    patch16le(objd,80,0xA55A);patch16le(objd,82,0x1234);patch16le(objd,84,0x55);
    chunk(out,"OBJD",128,0x10,"TemplatePerson",objd);
    const std::vector<std::string> body={"skin","body-old","head-old"};
    chunk(out,"STR#",200,0x10,"bodystring",strings(body));
    const std::vector<std::uint8_t> unknown={0xDE,0xAD,0xBE,0xEF,0x00,0x7F};
    chunk(out,"ZZZZ",0xBEEF,0x4321,"unknown data",unknown);
    if(duplicate_body)chunk(out,"STR#",200,0x20,"duplicate",strings(body));
    return out;
}
struct Seen { std::string type,label;std::uint16_t id=0,flags=0;std::vector<std::uint8_t> payload; };
std::vector<Seen> parse(const std::vector<std::uint8_t>& bytes) {
    const std::string raw(bytes.begin(),bytes.end());std::istringstream in(raw,std::ios::in|std::ios::binary);
    odh::View view(in,bytes.size());std::vector<Seen> result;
    odh::iff_chunks(view,[&](const odh::Chunk& c){std::vector<std::uint8_t> p(c.payload.size());c.payload.read(0,p.data(),p.size());result.push_back({c.type,c.label,c.id,c.flags,std::move(p)});});
    return result;
}
const Seen& find(const std::vector<Seen>& chunks,const char* type,std::uint16_t id) {
    const auto it=std::find_if(chunks.begin(),chunks.end(),[&](const Seen& c){return c.type==type&&c.id==id;});
    if(it==chunks.end())throw std::runtime_error("expected output chunk is missing");return *it;
}
std::vector<std::int16_t> definition_words(const Seen& objd){
    const std::string raw(objd.payload.begin(),objd.payload.end());std::istringstream input(raw,std::ios::in|std::ios::binary);
    return odh::read_object_definition_words(odh::View(input,objd.payload.size()));
}
void catalog_roundtrip(const std::vector<std::uint8_t>& original,const std::vector<std::uint8_t>& cloned,
        const odh::TemplatePersonRequest& request){
    const auto source=parse(original),generated=parse(cloned);const auto& source_objd=find(source,"OBJD",128);
    const auto& generated_objd=find(generated,"OBJD",128);const auto source_words=definition_words(source_objd);
    const auto generated_words=definition_words(generated_objd);
    need(generated_words.size()>42&&source_words.size()==generated_words.size(),"real OBJD reader did not decode supported profile");
    need(generated_words[41]==2000,"real OBJD CatalogStringsID does not reference CTSS2000");
    need(generated_words[40]==source_words[40]&&generated_words[42]==source_words[42],
        "catalog write changed adjacent FunctionFlags or Global metadata");
    for(std::size_t i=2;i<source_words.size();++i){
        if(i==14||i==15||i==41)continue; // GUID and the intended CatalogStringsID.
        need(source_words[i]==generated_words[i],"catalog clone modified an unrelated decoded OBJD field");
    }
    const auto& catalog=find(generated,"CTSS",static_cast<std::uint16_t>(generated_words[41]));
    const std::string raw(catalog.payload.begin(),catalog.payload.end());std::istringstream input(raw,std::ios::in|std::ios::binary);
    const auto table=odh::read_strings(odh::View(input,catalog.payload.size()));
    const auto* name=odh::string_in_language(table,request.language,0);
    const auto* bio=odh::string_in_language(table,request.language,1);
    need(name&&bio&&name->value==request.name&&bio->value==request.biography,
        "real OBJD catalog lookup did not round-trip character name and biography");
}
void rejects(const std::vector<std::uint8_t>& input,odh::TemplatePersonRequest request,const char* label) {
    try { (void)odh::clone_template_person(input,request); } catch(const odh::FormatError&) { return; }
    throw std::runtime_error(std::string("expected rejection: ")+label);
}
}
int main(int argc,char** argv) {
    try {
        const auto input=template_iff();
        odh::TemplatePersonRequest request{0x12345678,"user00001 - Sim",1,"Name", "Bio",
            {{1,"body-new"},{2,"head-new"}}};
        const auto output=odh::clone_template_person(input,request);
        const auto chunks=parse(output);
        const auto& objd=find(chunks,"OBJD",128);
        need(objd.label==request.object_label,"OBJD label changed");
        need(objd.payload.size()==216,"OBJD payload extent changed");
        need(objd.payload[28]==0x78&&objd.payload[29]==0x56&&objd.payload[30]==0x34&&objd.payload[31]==0x12,"new GUID not written");
        catalog_roundtrip(input,output,request);
        const auto input136=template_iff(false,136);
        catalog_roundtrip(input136,odh::clone_template_person(input136,request),request);
        rejects(template_iff(false,139),request,"unsupported OBJD layout");
        const auto& unknown=find(chunks,"ZZZZ",0xBEEF);
        need(unknown.flags==0x4321&&unknown.label=="unknown data"&&unknown.payload==std::vector<std::uint8_t>({0xDE,0xAD,0xBE,0xEF,0,0x7F}),"unknown chunk changed");
        const auto& body=find(chunks,"STR#",200);
        std::string body_bytes(body.payload.begin(),body.payload.end());std::istringstream body_in(body_bytes,std::ios::in|std::ios::binary);
        const auto table=odh::read_strings(odh::View(body_in,body.payload.size()));
        need(table.entries.size()==3&&table.entries[0].value=="skin"&&table.entries[1].value=="body-new"&&table.entries[2].value=="head-new","body string replacements incorrect");
        need(table.remaining_bytes==3,"body string padding extent changed");
        const auto& catalog=find(chunks,"CTSS",2000);
        std::string cat_bytes(catalog.payload.begin(),catalog.payload.end());std::istringstream cat_in(cat_bytes,std::ios::in|std::ios::binary);
        const auto cat=odh::read_strings(odh::View(cat_in,catalog.payload.size()));
        need(cat.entries.size()==2&&cat.entries[0].value=="Name"&&cat.entries[1].value=="Bio","name and biography missing");
        need(chunks.size()==4,"unexpected chunk count");
        rejects(template_iff(true),request,"duplicate body table");
        auto bad=request;bad.body_strings.push_back({1,"duplicate"});rejects(input,bad,"duplicate replacement index");
        bad=request;bad.name=std::string("bad\0name",8);rejects(input,bad,"embedded NUL");
        bad=request;bad.guid=0x7FD96B54;rejects(input,bad,"template GUID reuse");
        for(const auto species_guid:{0x4a70df92U,0x7bea0977U}){
            auto pieces=parse(input);std::vector<std::uint8_t> pet(input.begin(),input.begin()+64);
            for(auto& piece:pieces){
                if(piece.type=="OBJD"){piece.id=130;patch32le(piece.payload,28,species_guid);patch16le(piece.payload,38,204);}
                if(piece.type=="STR#")piece.id=204;
                chunk(pet,piece.type,piece.id,piece.flags,piece.label,piece.payload);
            }
            auto selected=request;selected.source=odh::TemplatePersonSource{species_guid,130,204};
            const auto cloned=parse(odh::clone_template_person(pet,selected));
            const auto words=definition_words(find(cloned,"OBJD",130));
            need(static_cast<std::uint32_t>(static_cast<std::uint16_t>(words[14]))+(static_cast<std::uint32_t>(static_cast<std::uint16_t>(words[15]))<<16)==selected.guid&&words[19]==204&&words[41]==2000,"explicit species template preserves its authenticated descriptor while publishing new GUID/catalog");
            need(find(cloned,"ZZZZ",0xBEEF).payload==find(pieces,"ZZZZ",0xBEEF).payload,"species clone preserves unknown authored chunks");
            auto mismatched=selected;mismatched.source->body_strings_id=200;rejects(pet,mismatched,"species BodyStringID mismatch");
            mismatched=selected;mismatched.source->guid^=1;rejects(pet,mismatched,"species source GUID mismatch");
        }
        {
            auto pieces=parse(input);std::vector<std::uint8_t> source27(input.begin(),input.begin()+64);
            std::vector<std::string> fields(27);for(std::size_t i=0;i<fields.size();++i)fields[i]="preserved"+std::to_string(i);
            for(auto& piece:pieces){if(piece.type=="OBJD")patch16le(piece.payload,38,200);if(piece.type=="STR#")piece.payload=strings(fields);chunk(source27,piece.type,piece.id,piece.flags,piece.label,piece.payload);}
            auto extended=request;extended.source=odh::TemplatePersonSource{0x7fd96b54U,128,200};extended.body_strings.clear();
            for(std::uint16_t i=27;i<35;++i)extended.body_strings.push_back({i,"wardrobe"+std::to_string(i)});
            const auto parsed=parse(odh::clone_template_person(source27,extended));const auto& bytes=find(parsed,"STR#",200).payload;
            std::string raw(bytes.begin(),bytes.end());std::istringstream stream(raw,std::ios::binary);const auto expanded=odh::read_strings(odh::View(stream,raw.size()));
            need(expanded.entries.size()==35&&expanded.remaining_bytes==35,"actual human template27 grows exactly to35 with per-entry padding");
            for(std::size_t i=0;i<27;++i)need(expanded.entries[i].value==fields[i]&&expanded.entries[i].language==1&&expanded.entries[i].comment.empty(),"bounded suffix expansion preserves every earlier source field");
            for(std::size_t i=27;i<35;++i)need(expanded.entries[i].value=="wardrobe"+std::to_string(i),"new source wardrobe suffix is explicitly written");
            auto invalid=extended;invalid.body_strings.push_back({35,"outside"});rejects(source27,invalid,"bounded wardrobe suffix rejects index35");
            invalid=extended;invalid.source.reset();rejects(source27,invalid,"legacy template caller cannot acquire suffix extension implicitly");
        }
        if(argc>1) {
            const std::filesystem::path path(argv[1]);std::ifstream far(path,std::ios::binary);
            if(!far)throw std::runtime_error("cannot open supplied Objects.far");
            const odh::View far_view(far,std::filesystem::file_size(path));
            const auto entries=odh::far1_index(far_view);const auto entry=std::find_if(entries.begin(),entries.end(),[](const odh::FarEntry& e){return e.name==R"(People\TemplatePerson.iff)";});
            if(entry==entries.end())throw std::runtime_error("TemplatePerson.iff not found in supplied FAR");
            const auto source=far_view.subview(entry->offset,entry->length);std::vector<std::uint8_t> real_template(static_cast<std::size_t>(source.size()));source.read(0,real_template.data(),real_template.size());
            auto real_request=request;real_request.guid=0x12345679;real_request.object_label="user00002 - Check";
            const auto generated=odh::clone_template_person(real_template,real_request);
            catalog_roundtrip(real_template,generated,real_request);
            const auto real_chunks=parse(generated);const auto& real_body=find(real_chunks,"STR#",200);
            std::string payload(real_body.payload.begin(),real_body.payload.end());std::istringstream payload_in(payload,std::ios::in|std::ios::binary);
            const auto real_strings=odh::read_strings(odh::View(payload_in,real_body.payload.size()));
            need(real_strings.entries[1].value=="body-new"&&real_strings.entries[2].value=="head-new","real TemplatePerson body fields were not transformed");
        }
        std::cout<<"template-person clone checks passed\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
