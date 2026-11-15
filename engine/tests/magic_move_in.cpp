/* SPDX-License-Identifier: MPL-2.0 */
// Residential Magic Town purchase through the live model and its neighbourhood
// writer. Synthetic resources and a private temporary profile only.
#include "app/front/FrontStrings.h"
#include "app/LotSession.h"
#include "files/LoadProgress.h"
#include "files/SafeFile.h"
#include <algorithm>
#include "app/front/NhoodModel.h"
#include "content/ContentManager.h"
#include "content/TS1CharacterFiles.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1NeighbourhoodWriter.h"
#include "files/formats/iff/IffRawFile.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::Content::TS1;
using namespace simapp::front;
using Result = NhoodModel::MoveResult;
constexpr uint32_t coin = 0x99e81bec;

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "could not read fixture");
    return {std::istreambuf_iterator<char>(input), {}};
}

static InventoryItem item(int type, uint32_t guid, uint16_t count) {
    InventoryItem i;
    i.Type = type;
    i.GUID = guid;
    i.Count = count;
    return i;
}

static void writeStrings(const fs::path& root, std::string outerPrice) {
    std::vector<std::string> table(39);
    table[29] = "quote %s %s %s %s";
    table[31] = "yes %s %s %s %s";
    table[32] = "1500";
    table[33] = std::move(outerPrice);
    table[36] = "coins %s %s %s %s %s %s";
    table[37] = "both %s %s %s %s %s %s";
    table[38] = "cash %s %s %s %s %s %s";
    IffRawFile file;
    file.Set("STR#", 134, RawIff::MakeSTRFormat3(table));
    require(file.Save((root / "UIText.iff").string()), "write prices");
    FrontStrings::Get().Load(root.string());
}

static void writeFamily(IffRawFile& file, uint16_t id, std::vector<uint32_t> members) {
    FAMI f;
    f.FamilyNumber = id;
    f.FamilyGUIDs = std::move(members);
    f.Budget = 1400;
    f.NetWorth = 37;
    st::Writer w;
    require(f.Write(nullptr, w), "encode FAMI");
    file.Set("FAMI", id, w.ToArray());
}

static void writeHouse(const fs::path& root, int id, int flag, int architecture) {
    // Plain legacy SIMI with 32 shorts. Exercise the reader at the map boundary.
    st::Writer w(st::Endian::Little);
    w.WriteUInt32(0);
    w.WriteUInt32(0x3e);
    w.WriteCString("IMIS", 4);
    for (int i = 0; i < 32; ++i) w.WriteInt16(i == 22 ? flag : 0);
    w.WriteInt16(0);
    for (int i = 0; i < 5; ++i) w.WriteInt32(0);
    w.WriteInt32(500);
    w.WriteInt32(0);
    w.WriteInt32(architecture);
    for (int i = 0; i < 6; ++i) w.WriteInt32(0);
    IffRawFile house;
    house.Set("SIMI", 1, w.ToArray());
    require(house.Save((root / "Houses" / ("House" + std::to_string(id) + ".iff")).string()), "write SIMI");
}

static void checkPrivateProfile(const fs::path& root, NhoodModel& model, TS1NeighborhoodProvider& provider) {
    const auto install = root / "install";
    const auto saves = root / "saves";
    const auto active = saves / "UserData";
    const auto outside = install / "UserData2";
    fs::create_directories(outside / "Houses");
    fs::create_directories(outside / "Characters");
    fs::copy_file(active / "Neighborhood.iff", outside / "Neighborhood.iff.bak");
    fs::copy_file(active / "Houses/House90.iff", outside / "Houses/House90.iff");
    fs::copy_file(active / "Houses/House90.iff", outside / "Houses/House90.iff.new");
    fs::copy_file(active / "Houses/House90.iff", outside / "Characters/User00001.iff.bak");
    fs::create_directories(install / "TemplateUserData");
    fs::copy_file(active / "Neighborhood.iff", install / "TemplateUserData/Neighborhood.iff");
    const auto neighborhood = bytes(outside / "Neighborhood.iff.bak");
    const auto house = bytes(outside / "Houses/House90.iff");
    auto checkSentinels = [&] {
        require(!fs::exists(outside / "Neighborhood.iff") && bytes(outside / "Neighborhood.iff.bak") == neighborhood,
                "source neighbourhood and backup changed");
        require(bytes(outside / "Houses/House90.iff") == house && bytes(outside / "Houses/House90.iff.new") == house,
                "source house or pending image changed");
        require(!fs::exists(outside / "Characters/User00001.iff") &&
                bytes(outside / "Characters/User00001.iff.bak") == house, "source character backup changed");
    };
    fs::create_directory_symlink(outside, saves / "UserData2");
    for (const auto& entry : std::map<std::string, std::string>{{"UserData3","Houses"},{"UserData4","Characters"}}) {
        auto slot = saves / entry.first;
        fs::create_directories(slot);
        fs::copy_file(active / "Neighborhood.iff", slot / "Neighborhood.iff");
        fs::create_directory_symlink(outside / entry.second, slot / entry.second);
    }
    UserDataSetup setup;
    setup.Configure(install.string(), saves.string());
    require(!IsPrivateNeighbourhood(install.string(), root.string(), active.string()), "overlapping root was accepted");
    auto available = setup.Available();
    const auto broken = setup.Broken();
    std::string error;
    for (int i : {1,2,3}) {
        require(std::find(available.begin(), available.end(), i) == available.end() &&
                std::find(broken.begin(), broken.end(), i) == broken.end() && !setup.CanRestore(i),
                "outside slot was offered by the map");
        require(!model.Switch(i, setup, error) && model.UserDir == active.string() && provider.UserPath == active.string(),
                "switch adopted an external directory");
        checkSentinels();
    }
    // Revalidate after adoption too. The selected house can still be read,
    // while the writer and recovery must respect the current physical paths.
    fs::rename(active / "Houses", active / "original-houses");
    fs::create_directory_symlink(outside / "Houses", active / "Houses");
    provider.Neighborhood->InventoryByID.at(2)[0].Count = 600;
    const auto before = bytes(active / "Neighborhood.iff");
    const auto writes = FSO::Files::SafeFile::Writes;
    simapp::LotSession read;
    require(read.OpenHouse(90, false, error) && read.LoadedHouseResource(), "read-only lookup failed");
    std::unique_ptr<simapp::LotSession> purchased;
    require(model.PurchaseForEntry(1, 90, true, purchased, error) == Result::SaveFailed && !purchased &&
            provider.GetFamily(1)->HouseNumber == 0 && provider.GetFamily(1)->Budget == 1400 &&
            provider.GetMagicoinsForFamily(*provider.GetFamily(1)) == 1000 &&
            FSO::Files::SafeFile::Writes == writes && bytes(active / "Neighborhood.iff") == before,
            "purchase reached an unsafe destination or changed the in-memory family");
    checkSentinels();
    fs::remove(active / "Houses");
    fs::rename(active / "original-houses", active / "Houses");
    // A directory alias that resolves into private storage remains usable.
    const auto inside = saves / "archive/slot";
    fs::create_directories(inside);
    fs::copy_file(active / "Neighborhood.iff", inside / "Neighborhood.iff.bak");
    fs::create_directory_symlink(inside, saves / "UserData5");
    available = setup.Available();
    require(std::find(available.begin(), available.end(), 4) != available.end() &&
            fs::exists(inside / "Neighborhood.iff") && !fs::exists(inside / "Neighborhood.iff.bak"),
            "private alias was not recovered");
    require(model.Switch(4, setup, error) && provider.GetFamily(1), "private alias was not adopted");
    checkSentinels();
}

int main(int argc, char** argv) {
    auto root = fs::temp_directory_path() / ("odh-magic-home-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); }
    } cleanup{root};
    try {
        const auto saves = root / "saves";
        const auto profile = saves / "UserData";
        fs::create_directories(root / "install");
        const auto path = profile / "Neighborhood.iff";
        fs::create_directories(profile / "Houses");
        writeStrings(root, "1000");
        writeHouse(profile, 90, 1, 0);
        writeHouse(profile, 91, 1, 0);
        writeHouse(profile, 92, 0, 100);
        writeHouse(profile, 93, 1, 0);

        IffRawFile hood;
        writeFamily(hood, 1, {111, 222});
        writeFamily(hood, 2, {333});
        writeFamily(hood, 3, {444});
        NBRS residents;
        for (auto identity : std::map<int, uint32_t>{{9,111},{2,222},{5,333},{6,444}}) {
            auto n = std::make_unique<Neighbour>();
            n->NeighbourID = identity.first;
            n->GUID = identity.second;
            n->Name = "Member" + std::to_string(identity.first);
            residents.NeighbourByID[n->NeighbourID] = n.get();
            residents.Entries.push_back(std::move(n));
        }
        st::Writer nbrs;
        require(residents.Write(nullptr, nbrs), "encode NBRS");
        hood.Set("NBRS", 1, nbrs.ToArray());
        std::vector<uint8_t> ngbh(48);
        ngbh[4] = 0x49;
        ngbh[8] = 'H'; ngbh[9] = 'B'; ngbh[10] = 'G'; ngbh[11] = 'N';
        hood.Set("NGBH", 1, ngbh);
        hood.Set("TEST", 7, {4, 2});
        require(hood.Save(path.string()), "write neighbourhood");

        auto* content = FSO::Content::Content::Get();
        content->Neighborhood = std::make_unique<TS1NeighborhoodProvider>(content);
        auto& p = *content->Neighborhood;
        content->SimsCompleteDir = (root / "install").string();
        p.UserDataRoot = saves.string();
        p.InitSpecific(0);
        p.Neighborhood = p.MainResource->Get<NGBH>(1);
        auto* f = p.GetFamily(1);
        auto* other = p.GetFamily(2);
        require(p.Neighborhood && f && other, "decode neighbourhood");
        auto& inv = p.Neighborhood->InventoryByID;
        inv[9] = {item(1, coin, 5000), item(7, coin, 400), item(7, 55, 6)};
        inv[2] = {item(7, coin, 599)};
        inv[5] = {item(7, coin, 3000)};
        p.GameState.ActiveFamily = other;
        p.Neighborhood->NeighborhoodData[2] = 7;
        f->RuntimeSubset = {222};
        p.ZoningDictionary = {{8, 0}, {90, 0}, {91, 0}, {92, 0}, {93, 1}};
        p.FamilyForHouse[90] = nullptr;
        NhoodModel model;
        model.UserDir = profile.string();
        model.RefreshFamilies();
        std::map<int, std::pair<int,int>> points{{90,{}}, {91,{}}, {92,{}}, {93,{}}};
        model.LoadArea(Area::MagicTown, &points);
        LotInfo ordinary;
        ordinary.House = 8;
        ordinary.Value = 500;
        ordinary.Exists = true;
        model.Lots.push_back(ordinary);
        if (argc == 2 && std::string(argv[1]) == "--private-profile") {
            checkPrivateProfile(root, model, p);
            std::cout << "Private profile admission, recovery and purchase guard passed\n";
            return 0;
        }
        require(model.Lot(90) && model.Lot(90)->MagicHouseFlag == 1 && model.Lot(90)->EmptyLot, "architecture zero house");
        require(model.Lot(92) && model.Lot(92)->MagicHouseFlag == 0 && !model.Lot(92)->EmptyLot, "flag zero land");
        require(model.CanMoveIn(1, 93) == Result::Community, "community admission");
        require(model.Family(1)->MagiCoins == 999 && model.Family(2)->MagiCoins == 3000, "candidate totals");
        require(model.CanMoveIn(1, 90) == Result::NotEnoughMagiCoins, "999 cannot buy an outer house");
        require(model.CanMoveIn(1, 8) == Result::Ok, "ordinary house independently affordable");
        require(model.CanMoveIn(1, 92) == Result::Ok, "flag zero admits land with insufficient coins");
        require(model.Rollover(*model.Lot(92), model.Family(1)).find("coins ") == 0, "land still quotes coins");
        std::string error;
        std::unique_ptr<simapp::LotSession> session;
        const auto templateDir = root / "install/TemplateMagictown";
        fs::create_directories(templateDir);
        const auto templateHouse = templateDir / "House94.iff";
        fs::copy_file(profile / "Houses/House90.iff", templateHouse);
        { std::ofstream untouched(templateHouse.string() + ".new"); untouched << "template sidecar"; }
        p.TemplateDirs = {templateDir.string()};
        simapp::LotSession templateRead;
        require(templateRead.OpenHouse(94, false, error) && templateRead.LoadedHousePath == templateHouse.string() &&
                bytes(templateHouse.string() + ".new") == "template sidecar", "template fallback is read-only");
        const auto private95 = profile / "Houses/House95.iff";
        fs::copy_file(templateHouse, private95.string() + ".new");
        simapp::LotSession privateRead;
        require(privateRead.OpenHouse(95, false, error) && privateRead.LoadedHousePath == private95.string() &&
                !fs::exists(private95.string() + ".new"), "recover private house only");
        const auto quoteBytes = bytes(path);
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::NotEnoughMagiCoins, "confirmed refused purchase");
        require(model.PurchaseForEntry(1, 92, false, session, error) == Result::Cancelled, "cancel land");
        require(!model.Dirty && !session && f->HouseNumber == 0 && bytes(path) == quoteBytes,
                "refusal and cancellation must leave purchase unchanged");
        inv[2][0].Count = 600;
        require(model.CanMoveIn(1, 90) == Result::Ok, "exact 1000 for outer house");
        writeStrings(root, "1001");
        require(model.CanMoveIn(1, 90) == Result::NotEnoughMagiCoins, "price must come from UIText");
        writeStrings(root, "1000");
        inv[2][0].Count = 1099;
        require(model.CanMoveIn(1, 91) == Result::NotEnoughMagiCoins, "1499 cannot buy middle house");
        inv[2][0].Count = 1100;
        require(model.CanMoveIn(1, 91) == Result::Ok, "exact 1500 for middle house");
        f->Budget = 499;
        model.RefreshFamilies();
        require(model.CanMoveIn(1, 91) == Result::CantAfford && model.CanMoveIn(1, 8) == Result::CantAfford,
                "simoleons are independently required");
        require(model.Rollover(*model.Lot(91), model.Family(1)).find("cash ") == 0, "cash-only refusal rollover");

        f->Budget = 1400;
        inv[2][0].Count = 700;
        model.RefreshFamilies();
        require(TS1NeighbourhoodWriter::Save(&p, path.string(), {}, &error), "save fixture starting state");
        const auto before = bytes(path);
        const auto* cached = model.Family(1);
        model.Dirty = true;
        auto checkUnchanged = [&]() {
            require(model.Dirty && !session && model.Family(1) == cached && cached->House == 0, "preserve selection/cache/dirty");
            require(f->Budget == 1400 && f->NetWorth == 37 && f->HouseNumber == 0, "preserve FAMI");
            require(p.FamilyForHouse.count(90) && !p.GetFamilyForHouse(90) && model.Lot(90)->Family == -1, "preserve resident map");
            require(inv.at(9)[1].Count == 400 && inv.at(2)[0].Count == 700 && bytes(path) == before, "preserve both wallets and disk");
        };
        const auto house = profile / "Houses/House90.iff";
        fs::rename(house, root / "parked-house");
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::Missing, "reject absent file");
        { std::ofstream invalid(house); invalid << "damaged house"; }
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::Missing, "reject invalid house IFF");
        fs::remove(house);
        fs::rename(root / "parked-house", house);
        checkUnchanged();
        writeHouse(profile, 90, 1, 100);
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::Missing && model.Lot(90)->Value == 570,
                "re-quote a changed SIMI without charging");
        writeHouse(profile, 90, 1, 0);
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::Missing && model.Lot(90)->Value == 500,
                "restore initial quote");
        checkUnchanged();
        fs::create_directory(path.string() + ".new");
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::SaveFailed && !error.empty(),
                "reject failed publication after opening real house");
        require(model.PurchaseForEntry(3, 8, true, session, error) == Result::SaveFailed &&
                p.GetFamily(3)->HouseNumber == 0 && p.GetFamily(3)->Budget == 1400, "ordinary write failure rolls back");
        fs::remove(path.string() + ".new");
        checkUnchanged();
        // No progress callback or portrait reconstruction may turn a published
        // purchase into an apparent refusal. The normal Save uses this event.
        FSO::Files::LoadProgress::ProgressSink = [](const std::string& step, size_t done, size_t total) {
            if (step == "save: neighbourhood" && done == total) throw std::runtime_error("presentation after write");
        };
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::Ok && session, "publish magic purchase");
        require(!model.Dirty && cached->House == 90 && cached->MagiCoins == 100 && f->Budget == 900, "committed map and currency");
        require(inv.at(9).size() == 2 && inv.at(9)[0].Type == 1 && inv.at(9)[0].Count == 5000 &&
                inv.at(9)[1].GUID == 55 && inv.at(2)[0].Count == 100 && inv.at(5)[0].Count == 3000,
                "spend members in FAMI order and erase only exhausted currency");
        require(model.PurchaseForEntry(1, 90, true, session, error) == Result::Occupied, "duplicate confirmation");
        IffFile reopened(path.string());
        auto* owner = reopened.Get<FAMI>(1);
        auto* wallet = reopened.Get<NGBH>(1);
        require(owner && owner->HouseNumber == 90 && owner->Budget == 900 && owner->NetWorth == 500, "reopened ownership");
        require(wallet && wallet->InventoryByID.at(9).size() == 2 && wallet->InventoryByID.at(2)[0].Count == 100 &&
                wallet->InventoryByID.at(5)[0].Count == 3000, "reopened wallet");
        IffRawFile raw;
        require(raw.Load(path.string()) && raw.Find("TEST", 7) &&
                raw.Find("TEST", 7)->Data == std::vector<uint8_t>({4,2}), "preserve opaque chunk");

        // Remove the file after publication. The real activation must still
        // reach the missing HOUS chunk of the retained IFF, without reopening.
        auto* retained = session->LoadedHouseResource();
        require(retained && session->LoadedHousePath == house.string(), "retain purchased IFF");
        fs::rename(house, root / "parked-house");
        error.clear();
        require(!session->ActivateOpenedHouse(100, 100, 0, error) && error.find("HOUS") != std::string::npos &&
                session->LoadedHouseResource() == retained, "later activation fails on retained house content");
        session.reset(); // App releases VM/entity resources before reloading the provider.
        fs::rename(root / "parked-house", house);
        FSO::Files::LoadProgress::ProgressSink = {};
        UserDataSetup setup;
        setup.Configure(content->SimsCompleteDir, saves.string());
        require(model.Switch(0, setup, error), "reopen persisted purchase through map's normal discard");
        f = p.GetFamily(1);
        other = p.GetFamily(2);
        require(f && f->HouseNumber == 90 && f->Budget == 900 && f->NetWorth == 500 &&
                p.GetMagicoinsForFamily(*f) == 100 && f->FamilyGUIDs == std::vector<uint32_t>({111,222}) &&
                p.GetFamilyForHouse(90) == f, "activation failure preserved acquired home, members and wallets");
        p.ZoningDictionary = {{8, 0}, {90, 0}, {91, 0}, {92, 0}, {93, 1}};
        model.LoadArea(Area::MagicTown, &points);
        model.Lots.push_back(ordinary);
        p.Neighborhood->InventoryByID.at(5)[0].Count = 0;
        require(model.PurchaseForEntry(2, 92, true, session, error) == Result::Ok && session &&
                other->Budget == 830 && p.GetMagicoinsForFamily(*other) == 0, "land spends only cash");
        session.reset();
        require(model.PurchaseForEntry(3, 8, true, session, error) == Result::Ok && !session &&
                p.GetFamily(3)->HouseNumber == 8 && p.GetFamily(3)->Budget == 900, "ordinary purchase before normal load");
        IffFile common(path.string());
        require(common.Get<FAMI>(3)->HouseNumber == 8 && common.Get<FAMI>(3)->Budget == 900, "ordinary home persisted");
        std::cout << "Magic residential purchase and persistence passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
