#pragma once
#include "saved_appearance.hpp"
#include <filesystem>
namespace odh_host {
// The renderer's selected mapping is the authority, including filename aliases
// and FAR/loose precedence. This wrapper introduces no second asset census.
class NativeWardrobeResources {
public:
 NativeWardrobeResources(const std::filesystem::path& root,const std::vector<std::string>& sources){
  std::vector<std::string> loose;
  for(const auto* relative:{"ExpansionShared/SkinsBuy","Downloads"}){
   const auto path=root/relative;std::error_code error;
   if(std::filesystem::is_directory(path,error)&&!error)loose.push_back(path.string());
  }
  const auto skins=root/"GameData/Skins";std::error_code error;
  const auto selected_skins=std::filesystem::is_directory(skins,error)&&!error?skins.string():std::string{};
  catalog_=SavedAppearanceResources::wardrobe_catalog(root,(root/"GameData/Animation/Animation.far").string(),
      (root/"GameData/Textures/Textures.far").string(),selected_skins,sources,loose);
 }
 const std::vector<odh::WardrobeCatalogAsset>& catalog() const noexcept{return catalog_;}
 std::optional<odh::WardrobeCatalogAsset> resolve(const odh::WardrobeCatalogAsset& receipt) const {
  std::optional<odh::WardrobeCatalogAsset> found;
  for(const auto& asset:catalog_)if(asset.kind==receipt.kind&&asset.name==receipt.name&&asset.source==receipt.source&&asset.asset==receipt.asset){
   if(found)throw odh::FormatError("wardrobe selected asset is ambiguous in admitted provider");found=asset;
  }
  return found; // Core checks content, ignores current ordinal and retains the accepted one.
 }
private:
 std::vector<odh::WardrobeCatalogAsset> catalog_;
};
} // namespace odh_host
