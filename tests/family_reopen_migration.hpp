// Test-only, read-only verifier. Included after the inspector's bounded helpers.
namespace family_reopen {
void require(bool ok,const char* why){if(!ok)throw odh::FormatError(why);}
std::vector<std::uint8_t> bytes(const fs::path& p,std::uintmax_t limit=128u*1024u*1024u){
    const auto size=fs::file_size(p);require(size<=limit,"migration input exceeds bound");
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));std::ifstream f(p,std::ios::binary);
    require(bool(f)&&(!size||bool(f.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(size)))),"migration read failed");return out;
}
std::string digest(const std::vector<std::uint8_t>& v){return hex_digest(odh::sha256(v.data(),v.size()));}
const odh::SessionArchiveSection& section(const odh::SessionArchive& a,const std::string& name){
    const auto p=std::find_if(a.sections.begin(),a.sections.end(),[&](const auto& s){return std::string(s.id.begin(),s.id.end())==name;});
    require(p!=a.sections.end(),"migration required section absent");return *p;
}
fs::path contained_file(const fs::path& root,const fs::path& relative){
    require(!relative.empty()&&!relative.is_absolute(),"migration locator must be relative");
    for(const auto& part:relative)require(part!=".."&&part!=".","migration locator traversal");
    const auto raw=root/relative,real=fs::canonical(raw),base=fs::canonical(root);const auto rel=real.lexically_relative(base);
    require(!rel.empty()&&*rel.begin()!=".."&&!fs::is_symlink(raw)&&fs::is_regular_file(real),"migration source escaped trusted root");return real;
}
odh::OriginalWardrobeSource source(const fs::path& game,const odh::NativePersonalIdentity& identity){
    const auto logical=fs::path(identity.character_asset);const auto parent=logical.parent_path().generic_string();
    require(parent=="PlayerData/Characters"||parent=="UserData/Characters","migration supports only authenticated household characters");
    const auto root=parent=="PlayerData/Characters"?odh_host::linux_player_character_directory():game/"UserData/Characters";
    const auto path=contained_file(root,logical.filename());const auto raw=bytes(path,16u*1024u*1024u);
    require(odh::sha256(raw.data(),raw.size())==identity.character_fingerprint,"migration character fingerprint changed");
    odh::OriginalWardrobeSource out;std::optional<odh::ObjdStartup> startup;
    std::istringstream input(std::string(raw.begin(),raw.end()),std::ios::in|std::ios::binary);
    const odh::View view(input,raw.size());
    {
        odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type=="OBJD"&&chunk.id==identity.definition_chunk_id){
            require(!startup&&odh::read_object_definition(chunk.payload).guid==identity.guid,"migration OBJD identity mismatch");startup=odh::read_objd_startup(chunk.payload);}});
        require(startup&&startup->body_strings,"migration source BodySTR absent");out.body_str_id=startup->body_strings;
        std::optional<odh::StringTable> table;odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type=="STR#"&&chunk.id==out.body_str_id){require(!table,"migration BodySTR ambiguous");table=odh::read_strings(chunk.payload);}});
        require(table&&table->state==odh::StringTableState::decoded,"migration BodySTR undecodable");std::size_t total=0;
        for(std::size_t i=0;i<257;++i){const auto* e=odh::string_in_ts1_language_set(*table,1,i);if(!e)break;
            require(i<256&&e->value.size()<=4096&&e->value.size()<=65536-total,"migration BodySTR exceeds bound");total+=e->value.size();out.strings.push_back(e->value);}
    }return out;
}
std::vector<std::string> animation_sources(const fs::path& game){
    std::vector<std::string> out;const auto add=[&](const fs::path& p){out.push_back(contained_file(game,p).string());};
    add("GameData/Animation/Animation.far");add("ExpansionShared/ExpansionShared.far");
    for(unsigned n=1;n<=7;++n){const auto folder=n==1?std::string("ExpansionPack"):"ExpansionPack"+std::to_string(n);const auto relative=fs::path(folder)/(folder+".far");if(fs::exists(game/relative))add(relative);}
    std::vector<fs::path> downloads;const auto dir=game/"Downloads";
    if(fs::is_directory(dir))for(const auto& entry:fs::recursive_directory_iterator(dir)){
        if(entry.is_symlink())continue;if(!entry.is_regular_file())continue;
        auto ext=entry.path().extension().string();for(auto& c:ext)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        if(ext==".far"){require(downloads.size()<128,"migration Downloads FAR bound");downloads.push_back(contained_file(game,entry.path().lexically_relative(game)));}}
    std::sort(downloads.begin(),downloads.end());for(const auto& p:downloads)out.push_back(p.string());require(out.size()<=128,"migration animation provider bound");return out;
}
std::optional<odh::WardrobeCatalogAsset> current_asset(const fs::path& game,const odh::WardrobeCatalogAsset& receipt){
    const auto path=contained_file(game,receipt.source);auto out=receipt;
    const auto hash=[&](const auto& view){require(view.size()<=16u*1024u*1024u,"migration selected asset exceeds bound");std::vector<std::uint8_t> raw(static_cast<std::size_t>(view.size()));if(!raw.empty())view.read(0,raw.data(),raw.size());out.fingerprint=odh::sha256(raw.data(),raw.size());};
    if(receipt.asset.empty())with_iff_file(path.string(),hash);else with_far_entry(path.string(),receipt.asset,hash);return out;
}
int inspect(int argc,char** argv){
    const auto game=fs::canonical(argv[2]);const auto oldbytes=bytes(argv[3]),newbytes=bytes(argv[4]);
    const auto before=odh::decode_session_bundle(oldbytes),after=odh::decode_session_bundle(newbytes);
    const auto a=odh::decode_session_archive(oldbytes),b=odh::decode_session_archive(newbytes);
    require(!before.personal_state&&after.personal_state&&before.family_lease&&before.simulation,"migration requires legacy family NPER absence and SIMS");
    require(a.sections.size()+1==b.sections.size(),"migration added/removed unexpected sections");
    std::vector<std::string> oldorder,neworder;for(const auto& s:a.sections)oldorder.emplace_back(s.id.begin(),s.id.end());
    for(const auto& s:b.sections)if(std::string(s.id.begin(),s.id.end())!="NPER")neworder.emplace_back(s.id.begin(),s.id.end());
    require(oldorder==neworder,"migration reordered original sections");
    const auto neighborhood=game/"UserData/Neighborhood.iff";const auto nid=odh_host::linux_neighborhood_identity(neighborhood);
    require(nid==before.family_lease->neighborhood_identity,"migration neighborhood mismatch");
    odh_host::FamilySessionJournal journal(source_families(neighborhood),nid,odh_host::read_linux_family_overlay_for_neighborhood(neighborhood),before.host.house_number,before.host.family_chunk_id);
    const auto head=journal.checkpoint_path();require(head&&fs::equivalent(*head,argv[4]),"migration after archive is not authenticated current head");journal.validate_restore(newbytes);
    int cursor=5;std::optional<odh::SessionBundle> reference;std::string reference_sha;
    if(cursor<argc&&std::string(argv[cursor])=="--personal-reference"){
        require(cursor+2<argc,"migration reference requires path and SHA");const auto raw=bytes(argv[cursor+1]);reference_sha=digest(raw);require(reference_sha==argv[cursor+2],"migration reference SHA mismatch");reference=odh::decode_session_bundle(raw);cursor+=3;
        require(reference->personal_state&&odh::same_session_household_roster(reference->household_roster,before.household_roster),"migration reference household differs");
        const auto refarchive=odh::decode_session_archive(raw);for(const auto* name:{"META","HHRD","FAML"})require(section(refarchive,name).data==section(a,name).data,"migration reference source domain differs");
    }
    std::set<std::int16_t> admitted;
    for(;cursor<argc;++cursor){unsigned value=0;const std::string text=argv[cursor];const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&value>0&&value<=32767&&admitted.insert(static_cast<std::int16_t>(value)).second,"migration admitted NID invalid/duplicate");}
    auto expected=before;for(const auto id:admitted){require(std::none_of(expected.relationships.sources.begin(),expected.relationships.sources.end(),[&](const auto& row){return row.matrix==odh::RelationshipMatrix::neighbor&&row.id==id;}),"migration NID was already present");expected.relationships.sources.push_back({odh::RelationshipMatrix::neighbor,id,0,{}});}
    std::sort(expected.relationships.sources.begin(),expected.relationships.sources.end(),[](const auto& x,const auto& y){return std::tie(x.matrix,x.id,x.generation)<std::tie(y.matrix,y.id,y.generation);});
    const auto canonical=odh::decode_session_archive(odh::encode_session_bundle(expected));
    std::vector<std::string> changed;
    for(const auto& old:a.sections){const auto name=std::string(old.id.begin(),old.id.end());const auto& actual=section(b,name);require(old.required==actual.required,"migration changed section flags");
        if(name=="SIMS"||name=="RELA")require(actual.data==section(canonical,name).data,"migration typed SIMS/RELA differs from exact canonical expectation");else require(actual.data==old.data,"migration changed unrelated source section");
        if(actual.data!=old.data)changed.push_back(name);}
    const auto& sim=section(a,"SIMS");require(sim.required&&sim.data.size()>=2&&sim.data[0]==1&&sim.data[1]==0,"migration only supports exact SIMS v1 source");
    odh::NativePersonalSnapshot personal;personal.version=3;personal.neighborhood_identity=nid;
    std::optional<odh_host::NativeWardrobeResources> resources;if(!reference)resources.emplace(game,animation_sources(game));
    std::set<std::int16_t> eligible;
    for(const auto& entity:before.world.entities){if(entity.ui_ghost||entity.dead||entity.person_data.size()<=31||entity.person_data[31]<=0)continue;
        const auto member=std::find_if(before.household_roster.begin(),before.household_roster.end(),[&](const auto& row){return row.id==entity.id&&row.generation==entity.generation;});
        require(member!=before.household_roster.end()&&member->neighbor_id==entity.person_data[31]&&eligible.insert(member->neighbor_id).second,"migration only proves exact preexisting household wardrobes");
        const odh::NativePersonalIdentity identity{member->neighbor_id,member->guid,member->object_definition_id,member->character_asset,member->character_fingerprint};const auto rawsource=source(game,identity);
        odh::OriginalWardrobeProjection wardrobe;
        if(reference){const auto& previous=*reference->personal_state;require(previous.version==3&&!previous.catalog&&previous.records.empty()&&previous.neighborhood_identity==nid,"migration reference personal domain differs");
            const auto found=std::find_if(previous.wardrobes.begin(),previous.wardrobes.end(),[&](const auto& row){return row.identity.neighbor_id==identity.neighbor_id;});require(found!=previous.wardrobes.end()&&found->revision==1&&odh::same_native_personal_identity(found->identity,identity),"migration reference wardrobe identity differs");wardrobe=found->wardrobe;
            odh::validate_original_wardrobe_source(wardrobe,rawsource);odh::validate_original_wardrobe_assets(wardrobe,[&](const auto& receipt){return current_asset(game,receipt);});
        }else wardrobe=odh::prepare_original_wardrobe_upgrade(rawsource,resources->catalog());
        personal.wardrobes.push_back({identity,1,wardrobe});
    }
    require(!personal.wardrobes.empty(),"migration expected wardrobe set empty");std::sort(personal.wardrobes.begin(),personal.wardrobes.end(),[](const auto& x,const auto& y){return x.identity.neighbor_id<y.identity.neighbor_id;});
    if(reference)require(reference->personal_state->wardrobes.size()==personal.wardrobes.size(),"migration reference has extra wardrobes");
    const auto expected_personal=odh::encode_native_personal_snapshot(personal);const auto& actual_personal=section(b,"NPER");require(actual_personal.required&&actual_personal.data==expected_personal,"migration NPER differs from exact shared-core expectation");
    std::cout<<"{\"schema\":\"family-reopen-migration-v1\",\"verified\":true,\"before_sha256\":"<<json_string(digest(oldbytes))<<",\"after_sha256\":"<<json_string(digest(newbytes))<<",\"nper_sha256\":"<<json_string(digest(expected_personal))<<",\"fresh_catalog_reconstruction\":"<<(reference?"false":"true")<<",\"reference_sha256\":"<<json_string(reference_sha)<<",\"added_sections\":[\"NPER\"],\"changed_sections\":[";
    bool comma=false;for(const auto& name:changed){if(comma)std::cout<<',';comma=true;std::cout<<json_string(name);}std::cout<<"],\"wardrobe_nids\":[";comma=false;for(const auto id:eligible){if(comma)std::cout<<',';comma=true;std::cout<<id;}std::cout<<"]}\n";return 0;
}
} // namespace family_reopen
