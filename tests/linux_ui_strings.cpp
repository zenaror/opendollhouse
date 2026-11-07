#include "linux_ui_strings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void le16(std::string& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value & 0xff));
    bytes.push_back(static_cast<char>(value >> 8));
}
void be16(std::string& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value >> 8));
    bytes.push_back(static_cast<char>(value & 0xff));
}
void be32(std::string& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<char>((value >> shift) & 0xff));
}
std::string string_table(std::initializer_list<std::pair<std::uint8_t, std::string>> entries) {
    std::string result;
    le16(result, 0xfffd); // STR# format -3
    le16(result, static_cast<std::uint16_t>(entries.size()));
    for (const auto& entry : entries) {
        result.push_back(static_cast<char>(entry.first));
        result += entry.second;
        result.push_back('\0');
        result.push_back('\0'); // comment
    }
    return result;
}
void chunk(std::string& iff, const char* type, std::uint16_t id,
           const std::string& payload) {
    iff.append(type, 4);
    be32(iff, static_cast<std::uint32_t>(76 + payload.size()));
    be16(iff, id);
    be16(iff, 0);
    iff.append(64, '\0');
    iff += payload;
}
void write_iff(const std::filesystem::path& path,
               const std::vector<std::pair<std::uint16_t, std::string>>& tables) {
    constexpr char header[] = "IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1";
    std::string iff(header, sizeof(header) - 1);
    iff.resize(60, '\0');
    iff.append(4, '\0');
    for (const auto& table : tables) chunk(iff, "STR#", table.first, table.second);
    std::ofstream out(path, std::ios::binary);
    out.write(iff.data(), static_cast<std::streamsize>(iff.size()));
    if (!out) throw std::runtime_error("could not write synthetic UIText.iff");
}
struct TempTree {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("odh-ui-strings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempTree() { std::filesystem::create_directories(root / "GameData"); }
    ~TempTree() { std::error_code ec; std::filesystem::remove_all(root, ec); }
};
}

int main() {
    try {
        TempTree temp;
        const auto path = temp.root / "GameData/UIText.iff";
        write_iff(path, {
            {128, string_table({{14, "Cancelar"}, {1, "Cancel"}})},
            {132, string_table({{1, "Purchase House?"}})},
            {134, string_table({{14, "Casa %s"}})},
            {145, string_table({{14, "Vizinhança"}})},
            {152, string_table({{14, "OK"}, {14, "Cancelar"}, {14, "Sim"}, {14, "N\xe3o"},
                                {1, "OK"}, {1, "Cancel"}, {1, "Yes"}, {1, "No"}})},
            {150, string_table({{1, "Caf\xe9"}})},
            {153, string_table({{14, "Sair"}, {14, "Salvar antes de sair?"}})},
            {160, string_table({{14, "Conforto: %d"}, {1, "Comfort: %d"}})},
        });
        odh_host::GameUiStrings strings(temp.root);
        require(strings.get(160,0,14,"missing")=="Conforto: %d" &&
                strings.get(160,0,2,"missing")=="Comfort: %d",
                "catalog rating labels are selected and retain format markers across language fallback");
        require(strings.get(128, 0, 14, "fallback") == "Cancelar",
                "requested language is selected");
        require(strings.get(128, 0, 2, "fallback") == "Cancel",
                "English fallback is preserved");
        require(strings.get(152, 1, 14, "fallback") == "Cancelar" &&
                strings.get(152, 2, 14, "fallback") == "Sim" &&
                strings.get(152, 3, 14, "fallback") == "Não",
                "original default dialog buttons use the requested language");
        require(strings.get(150, 0, 15, "fallback") == "Café",
                "English fallback is decoded using its own encoding, not Japanese");
        require(strings.get(999, 0, 15, "Não disponível") == "Não disponível",
                "caller fallback is already UTF-8 even for an Asian language");
        require(strings.get(134, 0, 14, "fallback") == "Casa %s",
                "lot-selection text table is loaded and localized");
        require(strings.get(153,1,14,"fallback")=="Salvar antes de sair?",
                "save/exit dialog text is loaded from original table153");
        write_iff(temp.root / "GameData/Live.iff", {
            {132, string_table({{14, "Rela\347\365es"}, {1, "Relationships"}})},
            {136, string_table({{1, "Job"}})},
            {133, string_table({{14, "Casa"}})},
            {135, string_table({{14, "Tamanho"}})},
            {138, string_table({{14, "Pequeno"}})},
            {140, string_table({{14, "Interesses"}})},
            {142, string_table({{14, "Descri\347\343o"}, {1, "Description"}})},
            {128, string_table({{14, "unrelated"}})},
        });
        odh_host::GameUiStrings live(temp.root, odh_host::GameUiStringResource::live_panels);
        require(live.get(132,0,14,"fallback")=="Relações" &&
                strings.get(132,0,14,"fallback")=="Purchase House?",
                "same table ID remains scoped to the selected original resource");
        require(live.get(136,0,14,"fallback")=="Job" && live.get(128,0,14,"missing")=="missing",
                "live panels preserve English fallback and exclude unrelated menu tables");
        require(live.get(140,0,14,"fallback")=="Interesses"&&live.get(142,0,14,"fallback")=="Descrição",
                "interest captions and descriptions load from Live rather than UIText");
        require(live.get(133,0,14,"fallback")=="Casa" && live.get(135,0,14,"fallback")=="Tamanho" &&
                live.get(138,0,14,"fallback")=="Pequeno", "house tables remain scoped to original Live resource");
        std::filesystem::rename(path, path.string() + ".moved");
        require(strings.get(132, 0, 14, "fallback") == "Purchase House?",
                "other menu tables are reused from the initial IFF read");
        require(strings.get(999, 0, 14, "Fallback") == "Fallback",
                "missing table uses caller fallback");

        TempTree missing;
        odh_host::GameUiStrings absent(missing.root);
        require(absent.get(128, 0, 14, "Fallback") == "Fallback",
                "missing UIText.iff uses caller fallback");

        TempTree duplicate;
        write_iff(duplicate.root / "GameData/UIText.iff", {
            {128, string_table({{1, "one"}})},
            {128, string_table({{1, "two"}})},
        });
        bool rejected = false;
        try {
            odh_host::GameUiStrings bad(duplicate.root);
            (void)bad.get(128, 0, 1, "fallback");
        } catch (const odh::FormatError&) { rejected = true; }
        require(rejected, "ambiguous requested table is rejected");

        TempTree outside;
        write_iff(outside.root / "GameData/UIText.iff", {
            {128, string_table({{1, "outside"}})},
        });
        TempTree escaped;
        std::filesystem::create_symlink(outside.root / "GameData/UIText.iff",
            escaped.root / "GameData/UIText.iff");
        rejected = false;
        try {
            odh_host::GameUiStrings bad(escaped.root);
            (void)bad.get(128, 0, 1, "fallback");
        } catch (const odh::FormatError&) { rejected = true; }
        require(rejected, "UIText.iff symlink outside the installation is rejected");

        std::cout << checks << " Linux UI string catalog checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Linux UI string catalog test failed: " << error.what() << '\n';
        return 1;
    }
}
