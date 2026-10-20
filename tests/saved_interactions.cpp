#include "saved_interactions.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t v,unsigned n,bool big=false){for(unsigned i=0;i<n;++i)b+=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string chunk(const std::string& type,unsigned id,const std::string& payload){
    std::string b(76,'\0');b.replace(0,4,type);for(unsigned i=0;i<4;++i)b[4+i]=static_cast<char>((76+payload.size())>>((3-i)*8));
    b[8]=static_cast<char>(id>>8);b[9]=static_cast<char>(id);return b+payload;
}
std::string iff(const std::string& payload){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');return b+payload;}
std::string ttab(){std::string b;put(b,1,2);put(b,8,2);put(b,0x1001,2);put(b,0x1002,2);put(b,0,4);put(b,1,4);put(b,0,4);put(b,4,4);put(b,0x3f800000,4);put(b,50,4);put(b,0xffffffff,4);return b;}
std::string labels(){std::string b;put(b,0,2);put(b,1,2);b+=char(3);b+="Sit";return b;}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!f)throw std::runtime_error("fixture write failed");}
}
int main(){try{
    const auto root=std::filesystem::temp_directory_path()/"odh-saved-interactions-test";
    std::filesystem::create_directories(root);const auto path=root/"object.iff";
    auto data=iff(chunk("TTAB",128,ttab())+chunk("TTAs",128,labels()));write(path,data);
    odh::ObjectDefinition object{};object.guid=123;object.tree_table_id=128;
    const auto result=saved_object_interactions({path.string(),"",7,object});
    check(result.state==SavedObjectInteractions::State::found&&result.table&&result.table->interactions.size()==1,"owner TTAB discovered");
    check(result.source==path.string()&&result.table_id==128&&result.asset.empty(),"exact owner provenance retained");
    check(result.labels.state==odh::StringTableState::decoded&&result.row_labels.size()==1&&result.row_labels[0]=="Sit","TTAs label mapped by explicit language/index");
    data=iff(chunk("TTAB",128,ttab()));write(path,data);
    const auto no_labels=saved_object_interactions({path.string(),"",7,object});
    check(no_labels.state==SavedObjectInteractions::State::found&&!no_labels.row_labels[0],"missing TTAs leaves decoded interaction unlabeled");
    data=iff(chunk("TTAB",128,ttab())+chunk("TTAB",128,ttab()));write(path,data);
    bool duplicate=false;try{(void)saved_object_interactions({path.string(),"",7,object});}catch(const odh::FormatError&){duplicate=true;}
    check(duplicate,"duplicate owner TTAB is rejected");
    object.tree_table_id=0;check(saved_object_interactions({path.string(),"",7,object}).state==SavedObjectInteractions::State::no_tree_table_id,"zero table id distinguished");
    object.tree_table_id=128;write(path,iff(""));check(saved_object_interactions({path.string(),"",7,object}).state==SavedObjectInteractions::State::missing_ttab,"absent TTAB distinguished");
    std::filesystem::remove_all(root);std::cout<<checks<<" saved interaction discovery checks passed (synthetic IFF only)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
