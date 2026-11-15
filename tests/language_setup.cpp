// SPDX-License-Identifier: MPL-2.0
#include "language_picker.hpp"
#include "linux_language_setup.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unistd.h>
static void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
static std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){
    std::filesystem::path sandbox;
    try{
        require(argc==2,"supply a diagnostic output directory");
        std::string pattern=(std::filesystem::path(argv[1])/"language-test-XXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
        require(mkdtemp(name.data()),"cannot create isolated settings directory");sandbox=name.data();
        require(setenv("XDG_CONFIG_HOME",sandbox.c_str(),1)==0,"cannot isolate XDG config");
        using namespace odh_host;
        require(parse_game_language("en-US")==1&&parse_game_language("pt-BR")==14&&parse_game_language("14")==14,"EN/PT CLI and saved codes disagree");
        for(std::size_t i=0;i<game_languages.size();++i)
            require(parse_game_language(game_languages[i].tag)==i+1&&parse_game_language(std::to_string(i+1))==i+1,"selectable code not accepted by CLI");
        require(!parse_game_language("0")&&!parse_game_language("21")&&!parse_game_language("14bad")&&!parse_game_language(""),"invalid locale accepted");
        require(game_language_or_english(0)==1&&game_language_or_english(21)==1&&game_language_or_english(14)==14,"fallback must be English without overriding valid PT");
        LanguagePicker picker(0);require(picker.language()==1,"first-run fallback selection");
        require(!picker.click(0,56+13*25,800)&&!picker.click(30,56+13*25+24,800),"outside/gap hit selected a language");
        require(picker.click(30,56+13*25,800)&&picker.language()==14,"PT pointer selection disagrees with displayed row");
        picker.move(7);require(picker.language()==1,"keyboard wrap");
        LinuxLanguageSetup setup;setup.load();require(setup.readable&&setup.needs_choice(),"missing config must offer first run");
        std::filesystem::path backup;std::string error;
        require(setup.confirm(14,backup,error)&&backup.empty(),"first-run confirmation failed");
        setup.load();require(!setup.needs_choice()&&setup.config.language_code==14,"confirmed PT was not persisted");
        setup.config.data_root="/player/João/Sims";setup.config.neighborhood_number=5;
        setup.config.preferences.lighting=false;setup.config.preferences.auto_center=false;setup.config.preferences.edge_scrolling=false;
        require(save_linux_user_config(setup.config,&error),"fixture config save");
        require(setup.confirm(1,backup,error),"EN confirmation failed");
        setup.load();require(setup.config.data_root=="/player/João/Sims"&&setup.config.neighborhood_number==5&&!setup.config.preferences.lighting&&!setup.config.preferences.auto_center&&!setup.config.preferences.edge_scrolling,"language change erased unrelated preferences");
        const std::string legacy="language=1\ndata_root_hex=\nneighborhood=5\nlighting=0\n";
        {std::ofstream out(linux_user_config_path());out<<legacy;}
        setup.load();require(setup.readable&&setup.needs_choice()&&setup.config.language_code==1&&read(linux_user_config_path())==legacy,
            "legacy default must request one explicit choice without rewriting settings");
        require(setup.confirm(14,backup,error)&&backup.empty(),"legacy preference confirmation failed");
        setup.load();require(!setup.needs_choice()&&setup.config.language_confirmed&&setup.config.neighborhood_number==5&&!setup.config.preferences.lighting,
            "confirmation must persist once while preserving unrelated settings");
        const std::string invalid="language=oops\ndata_root_hex=unreadable\nplayer-note=João\n";
        {std::ofstream out(linux_user_config_path(),std::ios::binary);out<<invalid;}
        setup.load();require(!setup.readable&&setup.needs_choice()&&read(linux_user_config_path())==invalid,"reading invalid config mutated it");
        require(setup.confirm(14,backup,error)&&!backup.empty()&&read(backup)==invalid,"explicit recovery did not preserve exact original bytes");
        setup.load();require(setup.readable&&setup.config.language_code==14,"recovered preference unreadable");
        {std::ofstream out(linux_user_config_path());out<<invalid;}
        require(!setup.confirm(1,backup,error)&&read(linux_user_config_path())==invalid,"config changed after prompt was overwritten without warning");
        std::filesystem::remove_all(sandbox);
        std::cout<<"Language setup: EN/PT, code convergence, fallback, pointer/keyboard, persisted fields and invalid-config recovery passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
