/* SPDX-License-Identifier: MPL-2.0 */
#include "app/front/UserDataSetup.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <map>
using simapp::front::UserDataSetup;
namespace fs = std::filesystem;
static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void Put(const fs::path& p, const std::string& value) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << value; }
static std::string Read(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static std::map<std::string,std::string> Snapshot(const fs::path& root) {
    std::map<std::string,std::string> r;
    if (fs::exists(root)) for (const auto& e : fs::recursive_directory_iterator(root)) if (e.is_regular_file()) r[e.path().lexically_relative(root).string()] = Read(e.path());
    return r;
}
static void Run(UserDataSetup& setup) { while(setup.Busy()) setup.Step(1u << 20); }
int main() {
    auto root = fs::temp_directory_path() / ("odh-save-import-policy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path Root; ~Cleanup() { std::error_code ec; fs::remove_all(Root,ec); } } cleanup{root};
    try {
        const auto install = root/"install";
        Put(install/"UserData/Neighborhood.iff", "existing family records; deleted template family absent");
        Put(install/"UserData/Houses/House05.iff", "modified house");
        Put(install/"UserData/Houses/NeighborhoodDesc.iff", "saved description");
        Put(install/"UserData/LotLocations.iff", "custom location");
        Put(install/"UserData/PhotoAlbum/nested/album.txt", "family photo continuity");
        Put(install/"UserData/Characters/User00001.iff", "saved sim");
        Put(install/"TemplateUserData/Neighborhood.iff", "new seed");
        Put(install/"TemplateUserData/Houses/House05.iff", "default house");
        Put(install/"TemplateCommunity/House56.iff", "expansion house");
        Put(install/"TemplateNPCs/User00002.iff", "default npc");
        for (const auto& file : {"LotZoning.iff", "LotLocations.iff", "StreetNames.iff"}) {
            Put(install/"TemplateCommunity"/file,"earlier expansion metadata");
            Put(install/"TemplateMagicTown"/file,"latest expansion metadata");
        }
        UserDataSetup imported; imported.Configure(install.string(), (root/"imported").string()); imported.PlanAll(); Run(imported);
        Require(Read(root/"imported/UserData/Neighborhood.iff")==Read(install/"UserData/Neighborhood.iff"), "import changed family records");
        Require(Read(root/"imported/UserData/Houses/House05.iff")=="modified house", "import replaced modified house");
        Require(!fs::exists(root/"imported/UserData/Houses/House56.iff"), "import resurrected removed house");
        Require(!fs::exists(root/"imported/UserData/Characters/User00002.iff"), "import resurrected removed character");
        Require(Read(root/"imported/UserData/Houses/NeighborhoodDesc.iff")=="saved description", "import lost saved description");
        Require(Read(root/"imported/UserData/LotLocations.iff")=="custom location", "import lost metadata");
        Require(Read(root/"imported/UserData/PhotoAlbum/nested/album.txt")=="family photo continuity", "import lost nested save data");
        Require(!imported.TemplateFamiliesPending(0), "import authorized automatic template families");
        std::cout << "PASS imported existing neighbourhood preserves edits and deletions\n";
        for (const auto& marker : {"", "sims save area: copied from the install (UserData + expansion pack templates).\n"}) {
            auto saves = root/(marker[0] ? "legacy" : "unmarked");
            Put(saves/"UserData/Neighborhood.iff", "player deleted families");
            Put(saves/"UserData/Houses/House56.iff", "expansion house"); // even exact template equality proves no permission
            if (marker[0]) Put(saves/"UserData/sims_setup.txt", marker);
            auto before = Snapshot(saves);
            UserDataSetup existing; existing.Configure(install.string(),saves.string()); existing.PlanAll(); Run(existing); existing.PlanNew(0); Run(existing);
            Require(Snapshot(saves)==before, "existing/legacy destination mutated");
            Require(!existing.TemplateFamiliesPending(0), "legacy marker authorized recreating deleted families");
        }
        std::cout << "PASS unmarked and legacy native saves remain exact\n";
        UserDataSetup fresh; fresh.Configure(install.string(),(root/"fresh").string()); fresh.PlanNew(0); Run(fresh);
        Require(Read(root/"fresh/UserData/Neighborhood.iff")=="new seed", "explicit new used existing install save");
        Require(fs::exists(root/"fresh/UserData/Houses/House56.iff"), "new neighbourhood lost expansion lots");
        Require(fs::exists(root/"fresh/UserData/Characters/User00002.iff"), "new neighbourhood lost npc templates");
        for (const auto& file : {"LotZoning.iff", "LotLocations.iff", "StreetNames.iff"})
            Require(Read(root/"fresh/UserData"/file)=="latest expansion metadata", "new metadata precedence regressed");
        Require(fresh.TemplateFamiliesPending(0), "new neighbourhood did not authorize family templates");
        Require(fresh.CompleteTemplateFamilies(0), "could not consume new bootstrap authorization");
        Require(!fresh.TemplateFamiliesPending(0), "completed bootstrap still authorized recreation");
        fs::remove(root/"fresh/UserData/fam_templates.txt");
        Require(!fresh.TemplateFamiliesPending(0), "missing old family marker reauthorized templates");
        std::cout << "PASS explicit new installs defaults and consumes authorization\n";
        // Interrupt after creating the durable intent and one tiny copy step.
        UserDataSetup interrupted; interrupted.Configure(install.string(),(root/"resume").string()); interrupted.PlanNew(1); interrupted.Step(1);
        UserDataSetup resumed; resumed.Configure(install.string(),(root/"resume").string()); resumed.PlanAll(); Run(resumed);
        Require(resumed.TemplateFamiliesPending(1), "explicit new copy did not resume after interruption");
        Require(Read(root/"resume/UserData2/Neighborhood.iff")=="new seed", "resumed new lost seed");
        UserDataSetup metadataPartial; metadataPartial.Configure(install.string(),(root/"metadata-resume").string()); metadataPartial.PlanNew(3);
        for(int i=0;i<30 && Read(root/"metadata-resume/UserData4/LotLocations.iff")!="earlier expansion metadata";++i) metadataPartial.Step(1);
        Require(Read(root/"metadata-resume/UserData4/LotLocations.iff")=="earlier expansion metadata", "could not stop at earlier metadata");
        UserDataSetup metadataResumed; metadataResumed.Configure(install.string(),(root/"metadata-resume").string()); metadataResumed.PlanAll(); Run(metadataResumed);
        for (const auto& file : {"LotZoning.iff", "LotLocations.iff", "StreetNames.iff"})
            Require(Read(root/"metadata-resume/UserData4"/file)=="latest expansion metadata", "resumed metadata precedence regressed");
        std::cout << "PASS explicit interrupted bootstrap resumes with metadata precedence\n";
        // A failed copy cannot publish completion; a later invocation retries it.
        UserDataSetup failed; failed.Configure(install.string(),(root/"failed").string()); failed.PlanNew(2);
        fs::remove(install/"TemplateUserData/Neighborhood.iff"); Run(failed);
        Require(!fs::exists(root/"failed/UserData3/sims_setup.txt"), "failed copy marked complete");
        Require(failed.PreparationPending(2), "failed copy lost incomplete status");
        Require(failed.Available().empty(), "failed partial copy offered for gameplay");
        Put(install/"TemplateUserData/Neighborhood.iff", "new seed");
        UserDataSetup retried; retried.Configure(install.string(),(root/"failed").string()); retried.PlanAll(); Run(retried);
        Require(retried.TemplateFamiliesPending(2), "failed copy not recoverable");
        std::cout << "PASS failed copy stays pending and retry completes\n";
        Put(root/"broken/UserData/Houses/House05.iff", "preserve me");
        UserDataSetup broken; broken.Configure(install.string(), (root/"broken").string());
        Require(broken.Available().empty() && broken.Broken() == std::vector<int>{0}, "unmarked missing save not offered recovery");
        broken.PlanNew(0); Run(broken);
        Require(Read(root/"broken/UserData/Houses/House05.iff")=="preserve me", "new overwrote incomplete existing save");
        std::cout << "PASS unmarked missing save offers recovery and retains houses\n";
        std::string error, log;
        Require(imported.RestoreFromInstall(0,&error,&log), "explicit restore failed");
        Require(fs::exists(root/"imported/UserData.damaged/Neighborhood.iff"), "restore discarded prior save");
        Require(!imported.TemplateFamiliesPending(0), "restored existing install save authorized template families");
        std::cout << "PASS explicit restore preserves displaced data and imported identity\n";
        Put(install/"UserData/PhotoAlbum/locked/record.txt", "preserved after listing retry");
        const auto locked = install/"UserData/PhotoAlbum/locked";
        struct Permissions { fs::path Path; ~Permissions() { std::error_code ec; fs::permissions(Path,fs::perms::owner_all,ec); } } permissions{locked};
        fs::permissions(locked,fs::perms::none);
        UserDataSetup listingFailed; listingFailed.Configure(install.string(),(root/"listing-failed").string()); listingFailed.PlanAll(); Run(listingFailed);
        Require(!fs::exists(root/"listing-failed/UserData/sims_setup.txt"), "failed subtree listing published completion");
        const auto beforeFailedRestore = Snapshot(root/"imported/UserData");
        Require(!imported.RestoreFromInstall(0,&error), "partial restore reported success");
        Require(!error.empty() && imported.PreparationPending(0), "partial restore was not reported as pending");
        Require(imported.Available().empty(), "partial restore appeared available");
        Require(Snapshot(root/"imported/UserData.damaged2")==beforeFailedRestore, "partial restore lost the displaced save");
        fs::permissions(locked,fs::perms::owner_all);
        UserDataSetup listingRetry; listingRetry.Configure(install.string(),(root/"listing-failed").string()); listingRetry.PlanAll(); Run(listingRetry);
        Require(Read(root/"listing-failed/UserData/PhotoAlbum/locked/record.txt")=="preserved after listing retry", "listing retry lost nested data");
        UserDataSetup restoreRetry; restoreRetry.Configure(install.string(),(root/"imported").string()); restoreRetry.PlanAll(); Run(restoreRetry);
        Require(restoreRetry.Available()==std::vector<int>{0}, "completed restore was not available after restart");
        Require(Read(root/"imported/UserData/PhotoAlbum/locked/record.txt")=="preserved after listing retry", "restored save lost nested data");
        std::cout << "PASS failed subtree listing remains recoverable\n";
        const auto crowded = root/"crowded";
        Put(crowded/"UserData/Neighborhood.iff", "current save");
        for (int slot = 1; slot <= 100; ++slot)
            fs::create_directories(crowded/("UserData.damaged" + (slot == 1 ? "" : std::to_string(slot))));
        UserDataSetup exhausted; exhausted.Configure(install.string(), crowded.string());
        Require(!exhausted.RestoreFromInstall(0,&error), "restore reused an occupied backup name");
        Require(Read(crowded/"UserData/Neighborhood.iff")=="current save", "restore changed save without a backup slot");
        Require(fs::is_empty(crowded/"UserData.damaged100"), "restore changed an existing backup directory");
        std::cout << "PASS restore requires a free backup name\n";
#ifndef _WIN32
        const auto fallback = root/"fallback";
        Put(fallback/"UserData/Neighborhood.iff", "current index");
        Put(fallback/"UserData/Neighborhood.iff.damaged", "earlier backup");
        Put(fallback/"UserData/Houses/House05.iff", "keep the house");
        Permissions fallbackPermissions{fallback};
        fs::permissions(fallback, fs::perms::owner_read | fs::perms::owner_exec);
        UserDataSetup indexOnly; indexOnly.Configure(install.string(), fallback.string());
        Require(indexOnly.RestoreFromInstall(0,&error), "index-only restore failed");
        Require(Read(fallback/"UserData/Neighborhood.iff.damaged")=="earlier backup", "index-only restore replaced an older backup");
        Require(Read(fallback/"UserData/Neighborhood.iff.damaged2")=="current index", "index-only restore did not keep the displaced index");
        Require(Read(fallback/"UserData/Houses/House05.iff")=="keep the house", "index-only restore replaced a house");
        std::cout << "PASS index-only restore preserves previous backups\n";
#endif
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
