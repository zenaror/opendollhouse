# Source inventory follows the pinned Sims3DS Makefile.port engine and app groups.
set(ODH_ENGINE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/sims3ds/src")
set(ODH_ENGINE_SOURCES
  sims3ds/src/files/FAR1/FAR1Archive.cpp
  sims3ds/src/files/FAR3/Decompresser.cpp
  sims3ds/src/files/FAR3/FAR3Archive.cpp
  sims3ds/src/files/FilePool.cpp
  sims3ds/src/files/HIT/EVT.cpp
  sims3ds/src/files/HIT/FSC.cpp
  sims3ds/src/files/HIT/HITFile.cpp
  sims3ds/src/files/HIT/HSM.cpp
  sims3ds/src/files/HIT/Hitlist.cpp
  sims3ds/src/files/HIT/Hot.cpp
  sims3ds/src/files/HIT/Patch.cpp
  sims3ds/src/files/HIT/TLO.cpp
  sims3ds/src/files/HIT/Track.cpp
  sims3ds/src/files/Hash.cpp
  sims3ds/src/files/SafeFile.cpp
  sims3ds/src/files/Tuning.cpp
  sims3ds/src/files/UTK/UTKFile2.cpp
  sims3ds/src/files/XA/XAFile.cpp
  sims3ds/src/files/formats/iff/IffFieldEncode.cpp
  sims3ds/src/files/formats/iff/IffFieldEncodeWriter.cpp
  sims3ds/src/files/formats/iff/IffFile.cpp
  sims3ds/src/files/formats/iff/IffRawFile.cpp
  sims3ds/src/files/formats/iff/PIFFRegistry.cpp
  sims3ds/src/files/formats/iff/chunks/ARRY.cpp
  sims3ds/src/files/formats/iff/chunks/ARRYWriter.cpp
  sims3ds/src/files/formats/iff/chunks/BCON.cpp
  sims3ds/src/files/formats/iff/chunks/BHAV.cpp
  sims3ds/src/files/formats/iff/chunks/BMP.cpp
  sims3ds/src/files/formats/iff/chunks/CARR.cpp
  sims3ds/src/files/formats/iff/chunks/DGRP.cpp
  sims3ds/src/files/formats/iff/chunks/FAMI.cpp
  sims3ds/src/files/formats/iff/chunks/FWAV.cpp
  sims3ds/src/files/formats/iff/chunks/GLOB.cpp
  sims3ds/src/files/formats/iff/chunks/HOUS.cpp
  sims3ds/src/files/formats/iff/chunks/NBRS.cpp
  sims3ds/src/files/formats/iff/chunks/NGBH.cpp
  sims3ds/src/files/formats/iff/chunks/OBJD.cpp
  sims3ds/src/files/formats/iff/chunks/OBJM.cpp
  sims3ds/src/files/formats/iff/chunks/OBJMWriter.cpp
  sims3ds/src/files/formats/iff/chunks/OBJT.cpp
  sims3ds/src/files/formats/iff/chunks/OBJf.cpp
  sims3ds/src/files/formats/iff/chunks/PALT.cpp
  sims3ds/src/files/formats/iff/chunks/PIFF.cpp
  sims3ds/src/files/formats/iff/chunks/SIMI.cpp
  sims3ds/src/files/formats/iff/chunks/SLOT.cpp
  sims3ds/src/files/formats/iff/chunks/SPR.cpp
  sims3ds/src/files/formats/iff/chunks/SPR2.cpp
  sims3ds/src/files/formats/iff/chunks/SPR2FrameEncoder.cpp
  sims3ds/src/files/formats/iff/chunks/STR.cpp
  sims3ds/src/files/formats/iff/chunks/THMB.cpp
  sims3ds/src/files/formats/iff/chunks/TPRP.cpp
  sims3ds/src/files/formats/iff/chunks/TTAB.cpp
  sims3ds/src/files/formats/iff/chunks/TTAT.cpp
  sims3ds/src/files/formats/iff/chunks/WALm.cpp
  sims3ds/src/files/formats/otf/OTFFile.cpp
  sims3ds/src/simantics/VM.cpp
  sims3ds/src/simantics/VMAI.cpp
  sims3ds/src/simantics/VMArchitecture.cpp
  sims3ds/src/simantics/VMContext.cpp
  sims3ds/src/simantics/VMEnvironmentGeometry.cpp
  sims3ds/src/simantics/VMRoofGeometry.cpp
  sims3ds/src/simantics/engine/Routing/VMRectRouter.cpp
  sims3ds/src/simantics/engine/TSOGlobalLink/VMTSOGlobalLinkStub.cpp
  sims3ds/src/simantics/engine/VMActionCallback.cpp
  sims3ds/src/simantics/engine/VMAmbientSound.cpp
  sims3ds/src/simantics/engine/VMChangeActionString.cpp
  sims3ds/src/simantics/engine/VMDialogHandler.cpp
  sims3ds/src/simantics/engine/VMMemory.cpp
  sims3ds/src/simantics/engine/VMMotiveDecay.cpp
  sims3ds/src/simantics/engine/VMQueuedAction.cpp
  sims3ds/src/simantics/engine/VMRoutingFrame.cpp
  sims3ds/src/simantics/engine/VMSimanticsException.cpp
  sims3ds/src/simantics/engine/VMSlotParser.cpp
  sims3ds/src/simantics/engine/VMStackFrame.cpp
  sims3ds/src/simantics/engine/VMSuitProvider.cpp
  sims3ds/src/simantics/engine/VMTS1MotiveDecay.cpp
  sims3ds/src/simantics/engine/VMTS1Neighbourhood.cpp
  sims3ds/src/simantics/engine/VMTS1Outfits.cpp
  sims3ds/src/simantics/engine/VMTS1Travel.cpp
  sims3ds/src/simantics/engine/VMThread.cpp
  sims3ds/src/simantics/engine/translator/VMTranslator.cpp
  sims3ds/src/simantics/entities/VMAvatar.cpp
  sims3ds/src/simantics/entities/VMAvatarMotiveDecay.cpp
  sims3ds/src/simantics/entities/VMEntity.cpp
  sims3ds/src/simantics/entities/VMGameObject.cpp
  sims3ds/src/simantics/entities/VMMultitileGroup.cpp
  sims3ds/src/simantics/model/TSOPlatform/VMDefaultValidator.cpp
  sims3ds/src/simantics/model/TSOPlatform/VMTSOLotState.cpp
  sims3ds/src/simantics/model/TSOPlatform/VMTSOObjectState.cpp
  sims3ds/src/simantics/model/VMArchitectureTerrain.cpp
  sims3ds/src/simantics/model/VMBuildableAreaInfo.cpp
  sims3ds/src/simantics/model/VMMotiveChange.cpp
  sims3ds/src/simantics/model/VMObjectQueries.cpp
  sims3ds/src/simantics/model/VMRoomMap.cpp
  sims3ds/src/simantics/model/VMTS1Appearance.cpp
  sims3ds/src/simantics/model/VMTuningCache.cpp
  sims3ds/src/simantics/primitives/VMAnimateSim.cpp
  sims3ds/src/simantics/primitives/VMBreakPoint.cpp
  sims3ds/src/simantics/primitives/VMChangeSuitOrAccessory.cpp
  sims3ds/src/simantics/primitives/VMCreateObjectInstance.cpp
  sims3ds/src/simantics/primitives/VMDialogGlobalStrings.cpp
  sims3ds/src/simantics/primitives/VMDialogPrivateStrings.cpp
  sims3ds/src/simantics/primitives/VMDialogSemiGlobalStrings.cpp
  sims3ds/src/simantics/primitives/VMDrop.cpp
  sims3ds/src/simantics/primitives/VMDropOnto.cpp
  sims3ds/src/simantics/primitives/VMExpression.cpp
  sims3ds/src/simantics/primitives/VMFindBestAction.cpp
  sims3ds/src/simantics/primitives/VMFindBestObjectForFunction.cpp
  sims3ds/src/simantics/primitives/VMFindLocationFor.cpp
  sims3ds/src/simantics/primitives/VMGenericTS1Call.cpp
  sims3ds/src/simantics/primitives/VMGenericTSOCall.cpp
  sims3ds/src/simantics/primitives/VMGetDirectionTo.cpp
  sims3ds/src/simantics/primitives/VMGetDistanceTo.cpp
  sims3ds/src/simantics/primitives/VMGetTerrainInfo.cpp
  sims3ds/src/simantics/primitives/VMGosubFoundAction.cpp
  sims3ds/src/simantics/primitives/VMGotoRelativePosition.cpp
  sims3ds/src/simantics/primitives/VMGotoRoutingSlot.cpp
  sims3ds/src/simantics/primitives/VMGrab.cpp
  sims3ds/src/simantics/primitives/VMIdleForInput.cpp
  sims3ds/src/simantics/primitives/VMInventoryOperations.cpp
  sims3ds/src/simantics/primitives/VMInvokePlugin.cpp
  sims3ds/src/simantics/primitives/VMLookTowards.cpp
  sims3ds/src/simantics/primitives/VMNotifyOutOfIdle.cpp
  sims3ds/src/simantics/primitives/VMOnlineJobsCall.cpp
  sims3ds/src/simantics/primitives/VMPlaySound.cpp
  sims3ds/src/simantics/primitives/VMPrimitiveTable.cpp
  sims3ds/src/simantics/primitives/VMPushInteraction.cpp
  sims3ds/src/simantics/primitives/VMRandomNumber.cpp
  sims3ds/src/simantics/primitives/VMReach.cpp
  sims3ds/src/simantics/primitives/VMRefresh.cpp
  sims3ds/src/simantics/primitives/VMRelationship.cpp
  sims3ds/src/simantics/primitives/VMRemoveObjectInstance.cpp
  sims3ds/src/simantics/primitives/VMRunFunctionalTree.cpp
  sims3ds/src/simantics/primitives/VMRunTreeByName.cpp
  sims3ds/src/simantics/primitives/VMSetBalloonHeadline.cpp
  sims3ds/src/simantics/primitives/VMSetMotiveChange.cpp
  sims3ds/src/simantics/primitives/VMSetToNext.cpp
  sims3ds/src/simantics/primitives/VMSleep.cpp
  sims3ds/src/simantics/primitives/VMSnap.cpp
  sims3ds/src/simantics/primitives/VMSpecialEffect.cpp
  sims3ds/src/simantics/primitives/VMStopAllSounds.cpp
  sims3ds/src/simantics/primitives/VMSysLog.cpp
  sims3ds/src/simantics/primitives/VMTS1Budget.cpp
  sims3ds/src/simantics/primitives/VMTS1InventoryOperations.cpp
  sims3ds/src/simantics/primitives/VMTS1MakeNewCharacter.cpp
  sims3ds/src/simantics/primitives/VMTS1MiscPrimitives.cpp
  sims3ds/src/simantics/primitives/VMTS1Primitives.cpp
  sims3ds/src/simantics/primitives/VMTestObjectType.cpp
  sims3ds/src/simantics/primitives/VMTestSimInteractingWith.cpp
  sims3ds/src/simantics/primitives/VMTransferFunds.cpp
  sims3ds/src/simantics/utils/VMArchitectureTools.cpp
  sims3ds/src/simantics/utils/VMTS1FamilyLoader.cpp
  sims3ds/src/simantics/utils/VMTS1LotSaver.cpp
  sims3ds/src/simantics/utils/VMTS1InteractionCheckpoint.cpp
  sims3ds/src/simantics/utils/VMWorldActivator.cpp
  sims3ds/src/content/Audio.cpp
  sims3ds/src/content/ContentManager.cpp
  sims3ds/src/content/TS1CharacterFiles.cpp
  sims3ds/src/content/TS1Clothing.cpp
  sims3ds/src/content/TS1ExpansionContent.cpp
  sims3ds/src/content/TS1FamilyImport.cpp
  sims3ds/src/content/TS1JobProvider.cpp
  sims3ds/src/content/TS1NeighbourProvider.cpp
  sims3ds/src/content/TS1NeighbourhoodWriter.cpp
  sims3ds/src/content/TS1SimDesc.cpp
  sims3ds/src/content/TS1SkinCatalog.cpp
  sims3ds/src/content/WorldFloorProvider.cpp
  sims3ds/src/content/WorldRoofProvider.cpp
  sims3ds/src/content/WorldWallProvider.cpp
  sims3ds/src/world/World.cpp
  sims3ds/src/world/World2D.cpp
  sims3ds/src/world/WorldState.cpp
  sims3ds/src/world/components/FloorComponent.cpp
  sims3ds/src/world/components/ObjectComponent.cpp
  sims3ds/src/world/components/RoofComponent.cpp
  sims3ds/src/world/components/TerrainComponent.cpp
  sims3ds/src/world/components/WallComponent.cpp
  sims3ds/src/world/model/Blueprint.cpp
  sims3ds/src/world/utils/DGRPRenderer.cpp
  sims3ds/src/world/utils/TextureGenerator.cpp
  sims3ds/src/world/utils/TriangleRaster.cpp
  sims3ds/src/world/utils/_2DWorldBatch.cpp
  sims3ds/src/vitaboy/Animation.cpp
  sims3ds/src/vitaboy/Animator.cpp
  sims3ds/src/vitaboy/Appearance.cpp
  sims3ds/src/vitaboy/Avatar.cpp
  sims3ds/src/vitaboy/AvatarRenderer.cpp
  sims3ds/src/vitaboy/BCF.cpp
  sims3ds/src/vitaboy/BCFReadProxy.cpp
  sims3ds/src/vitaboy/CFP.cpp
  sims3ds/src/vitaboy/HandGroup.cpp
  sims3ds/src/vitaboy/Mesh.cpp
  sims3ds/src/vitaboy/Outfit.cpp
  sims3ds/src/vitaboy/SimAvatar.cpp
  sims3ds/src/vitaboy/Skeleton.cpp
  sims3ds/src/vitaboy/Texture.cpp
  sims3ds/src/vitaboy/content/TS1AvatarProvider.cpp
  sims3ds/src/common/TS1/TS1Curve.cpp
  sims3ds/src/common/TS1/TS1RoofGeometry.cpp
  sims3ds/src/common/TS1/TS1LotPictures.cpp
  sims3ds/src/hit/AmbiencePlayer.cpp
  sims3ds/src/hit/FSCPlayer.cpp
  sims3ds/src/hit/HITInterpreter.cpp
  sims3ds/src/hit/HITTVOn.cpp
  sims3ds/src/hit/HITThread.cpp
  sims3ds/src/hit/HITVM.cpp
  sims3ds/src/audio/AudioBackend.cpp
  sims3ds/src/audio/AudioMixer.cpp
  sims3ds/src/audio/GameAudio.cpp
  sims3ds/src/audio/Mp3Stream.cpp
  sims3ds/src/audio/WavFile.cpp
  sims3ds/src/app/App.cpp
  sims3ds/src/app/Canvas.cpp
  sims3ds/src/app/DesktopHUD.cpp
  sims3ds/src/app/Font.cpp
  sims3ds/src/app/LotScreen.cpp
  sims3ds/src/app/LotSession.cpp
  sims3ds/src/app/PieMenu.cpp
  sims3ds/src/app/UIStrings.cpp
  sims3ds/src/app/WorldPicking.cpp
  sims3ds/src/app/build/UIBuildMode.cpp
  sims3ds/src/app/build/UIFloorPainter.cpp
  sims3ds/src/app/build/UITerrainTools.cpp
  sims3ds/src/app/build/UIWallPainter.cpp
  sims3ds/src/app/build/UIWallPlacer.cpp
  sims3ds/src/app/buy/UIBuyMode.cpp
  sims3ds/src/app/buy/UICatalog.cpp
  sims3ds/src/app/buy/UIObjectHolder.cpp
  sims3ds/src/app/cas/CASArt.cpp
  sims3ds/src/app/cas/CASFamilyWriter.cpp
  sims3ds/src/app/cas/CASScreen.cpp
  sims3ds/src/app/cas/DesktopCAS.cpp
  sims3ds/src/app/cas/CASSimPreview.cpp
  sims3ds/src/app/cas/CASTextInput.cpp
  sims3ds/src/app/front/AppFlow.cpp
  sims3ds/src/app/front/CASBridge.cpp
  sims3ds/src/app/front/FamilyPicker.cpp
  sims3ds/src/app/front/FrontAudio.cpp
  sims3ds/src/app/front/FrontEnd.cpp
  sims3ds/src/app/front/FrontStrings.cpp
  sims3ds/src/app/front/FrontUI.cpp
  sims3ds/src/app/front/GameOptions.cpp
  sims3ds/src/app/front/IntroScreen.cpp
  sims3ds/src/app/front/LotSave.cpp
  sims3ds/src/app/front/NeighbourhoodScreen.cpp
  sims3ds/src/app/front/NhoodModel.cpp
  sims3ds/src/app/front/ResGraphics.cpp
  sims3ds/src/app/front/SaveOverlay.cpp
  sims3ds/src/app/front/SimDialogView.cpp
  sims3ds/src/app/front/TS1Font.cpp
  sims3ds/src/app/front/UserDataSetup.cpp
  sims3ds/src/app/live/UIHeadlineRenderer.cpp
  sims3ds/src/app/live/UILiveMode.cpp
  sims3ds/src/app/live/UIMotiveDisplay.cpp
  sims3ds/src/app/ui/GameText.cpp
  sims3ds/src/app/ui/UICheatConsole.cpp
  sims3ds/src/app/ui/UIGraphics.cpp
  sims3ds/src/app/ui/UIOptionsPanel.cpp
  sims3ds/src/app/ui/UIUCP.cpp
)

add_subdirectory(original-ui)
find_package(ZLIB REQUIRED)
add_library(odh_engine STATIC ${ODH_ENGINE_SOURCES})
target_include_directories(odh_engine PUBLIC "${ODH_ENGINE_SOURCE_ROOT}")
# Reuse portable presentation services without an old VM dependency.
target_sources(odh_engine PRIVATE sims3ds/src/app/ui/OriginalPCUI.cpp
  sims3ds/src/world/utils/TS1LightMap.cpp
  sims3ds/src/world/utils/TS1TerrainRaster.cpp
  sims3ds/src/app/ui/DesktopHousePanel.cpp
  sims3ds/src/app/ui/DesktopOptionsPanel.cpp
  sims3ds/src/app/ui/DesktopLivePanels.cpp
  sims3ds/src/app/ui/DesktopExpansionPanels.cpp
  sims3ds/src/app/ui/DesktopExpansionSelection.cpp
  sims3ds/src/app/ui/DesktopExpansionPanelsRender.cpp
  sims3ds/src/app/ui/OriginalTextBackend.cpp
  sims3ds/src/app/ui/OriginalPhonebook.cpp
  sims3ds/src/app/ui/OriginalTextEncoding.cpp
  sims3ds/src/app/ui/OriginalSelectedCrystal.cpp)
target_include_directories(odh_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_compile_features(odh_engine PUBLIC cxx_std_17)
target_compile_definitions(odh_engine PUBLIC ST_FILES_REAL)
target_link_libraries(odh_engine PUBLIC ZLIB::ZLIB odh_original_ui)
target_sources(odh_engine PRIVATE sims3ds/src/content/TS1CareerDialog.cpp)
set_target_properties(odh_engine PROPERTIES CXX_EXTENSIONS OFF)

# Distributions of the application must carry the incorporated source notices.
install(FILES sims3ds/LICENSE sims3ds/CREDITS.md
  DESTINATION share/licenses/OpenDollhouse/Sims3DS)
install(DIRECTORY sims3ds/LICENSES/
  DESTINATION share/licenses/OpenDollhouse/Sims3DS)
install(FILES sims3ds/src/third_party/minimp3/LICENSE
  DESTINATION share/licenses/OpenDollhouse/minimp3)

if(ODH_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "ODH_SANITIZERS requires GCC or Clang")
  endif()
  # PUBLIC instruments the desktop consumer and propagates the runtime link
  # options from this static library to the final executable.
  target_compile_options(odh_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(odh_engine PUBLIC -fsanitize=address,undefined)
endif()

# Prepare portable game indexes on a computer before copying them to a card.
if(NOT CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$")
  add_executable(odh-prepare-cache EXCLUDE_FROM_ALL sims3ds/src/tools/simcache.cpp)
  target_link_libraries(odh-prepare-cache PRIVATE odh_engine)
endif()

option(ODH_BUILD_DESKTOP "Build the single-screen desktop application" ON)
if(ODH_BUILD_DESKTOP)
  add_subdirectory(platform/desktop)
endif()

if(BUILD_TESTING)
  add_executable(odh-handheld-lot-scene tests/handheld_lot_scene.cpp)
  target_link_libraries(odh-handheld-lot-scene PRIVATE odh_engine)
  add_test(NAME handheld-lot-scene COMMAND odh-handheld-lot-scene)

  add_executable(odh-lot-positional-sound tests/lot_positional_sound.cpp)
  target_link_libraries(odh-lot-positional-sound PRIVATE odh_engine)
  add_test(NAME lot-positional-sound COMMAND odh-lot-positional-sound)

  add_executable(odh-handheld-language-contract tests/handheld_language.cpp)
  target_include_directories(odh-handheld-language-contract PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
  target_link_libraries(odh-handheld-language-contract PRIVATE odh_engine)
  add_test(NAME handheld-language-contract COMMAND odh-handheld-language-contract "${CMAKE_CURRENT_BINARY_DIR}")
  add_executable(odh-portable-formats-contract ../tests/formats.cpp)
  target_link_libraries(odh-portable-formats-contract PRIVATE odh_original_ui)
  add_test(NAME portable-formats-contract COMMAND odh-portable-formats-contract)
  add_executable(odh-portable-resource-io-contract ../tests/resource_io.cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(odh-portable-resource-io-contract PRIVATE odh_original_ui Threads::Threads)
  add_test(NAME portable-resource-io-contract COMMAND odh-portable-resource-io-contract)
  add_executable(odh-portable-ui-bitmap-exact ../tests/ui_bitmap_exact.cpp)
  target_link_libraries(odh-portable-ui-bitmap-exact PRIVATE odh_original_ui)
  add_test(NAME portable-ui-bitmap-exact COMMAND odh-portable-ui-bitmap-exact)
  add_executable(odh-portable-pie-presentation-exact ../tests/original_pie_presentation_exact.cpp
    sims3ds/src/app/Canvas.cpp)
  target_include_directories(odh-portable-pie-presentation-exact PRIVATE sims3ds/src)
  target_link_libraries(odh-portable-pie-presentation-exact PRIVATE odh_original_ui)
  add_test(NAME portable-pie-presentation-exact COMMAND odh-portable-pie-presentation-exact)
  add_executable(odh-portable-original-presentation-exact ../tests/original_presentation.cpp)
  target_compile_definitions(odh-portable-original-presentation-exact PRIVATE ODH_PRESENTATION_GAME_FONT_BACKEND=1)
  target_link_libraries(odh-portable-original-presentation-exact PRIVATE odh_engine)
  add_test(NAME portable-original-presentation-exact COMMAND odh-portable-original-presentation-exact)
  # Asset-backed flow: callers supply disposable save/cache roots and game data.
  # Kept out of CTest/default builds because commercial fixtures are not shipped.
  add_executable(odh-classic-continuation-flow EXCLUDE_FROM_ALL tests/classic_continuation_flow.cpp)
  target_include_directories(odh-classic-continuation-flow PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_link_libraries(odh-classic-continuation-flow PRIVATE odh_engine)
  add_executable(odh-dialog-checkpoint-flow EXCLUDE_FROM_ALL tests/dialog_checkpoint_flow.cpp)
  target_include_directories(odh-dialog-checkpoint-flow PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_link_libraries(odh-dialog-checkpoint-flow PRIVATE odh_engine)
  add_executable(odh-nbrs-wire-contract tests/nbrs_wire_contract.cpp)
  target_link_libraries(odh-nbrs-wire-contract PRIVATE odh_engine)
  add_test(NAME nbrs-wire-contract COMMAND odh-nbrs-wire-contract)
  add_executable(odh-ts1-offlot-presence-contract tests/ts1_offlot_presence_contract.cpp)
  target_link_libraries(odh-ts1-offlot-presence-contract PRIVATE odh_engine)
  add_test(NAME ts1-offlot-presence-contract COMMAND odh-ts1-offlot-presence-contract)
  add_executable(odh-self-snap-translation-contract tests/self_snap_translation_contract.cpp)
  target_link_libraries(odh-self-snap-translation-contract PRIVATE odh_engine)
  add_test(NAME self-snap-translation-contract COMMAND odh-self-snap-translation-contract)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(contract odh-classic-continuation-flow odh-dialog-checkpoint-flow odh-nbrs-wire-contract odh-ts1-offlot-presence-contract odh-self-snap-translation-contract)
      target_compile_options(${contract} PRIVATE -g0)
      target_link_options(${contract} PRIVATE -Wl,--strip-debug)
    endforeach()
  endif()
  add_executable(odh-neighborhood-hover-contract ../tests/neighborhood_hover_contract.cpp)
  target_link_libraries(odh-neighborhood-hover-contract PRIVATE odh_original_ui)
  add_test(NAME neighborhood-hover-contract COMMAND odh-neighborhood-hover-contract)
  add_executable(odh-neighborhood-recovery-contract ../tests/neighborhood_recovery_contract.cpp)
  target_link_libraries(odh-neighborhood-recovery-contract PRIVATE odh_original_ui)
  add_test(NAME neighborhood-recovery-contract COMMAND odh-neighborhood-recovery-contract)
  add_executable(odh-original-win-button tests/original_win_button.cpp)
  target_link_libraries(odh-original-win-button PRIVATE odh_original_ui)
  add_test(NAME original-win-button COMMAND odh-original-win-button)
  add_executable(odh-original-needs-panel ../tests/linux_motive_panel.cpp)
  target_link_libraries(odh-original-needs-panel PRIVATE odh_original_ui)
  add_test(NAME original-needs-panel COMMAND odh-original-needs-panel)
  add_executable(odh-original-live-help ../tests/original_live_help.cpp)
  target_link_libraries(odh-original-live-help PRIVATE odh_original_ui)
  add_test(NAME original-live-help COMMAND odh-original-live-help)
  add_executable(odh-original-queue-presentation tests/original_queue_presentation.cpp)
  target_link_libraries(odh-original-queue-presentation PRIVATE odh_original_ui)
  add_test(NAME original-queue-presentation COMMAND odh-original-queue-presentation)
  add_executable(odh-original-money-headline tests/original_money_headline.cpp)
  target_link_libraries(odh-original-money-headline PRIVATE odh_engine)
  add_test(NAME original-money-headline COMMAND odh-original-money-headline)
  add_executable(odh-original-clock-format tests/original_clock_format.cpp)
  target_compile_features(odh-original-clock-format PRIVATE cxx_std_17)
  target_include_directories(odh-original-clock-format PRIVATE sims3ds/src)
  add_test(NAME original-clock-format COMMAND odh-original-clock-format)
  add_executable(odh-arry-literal-bounds tests/arry_literal_bounds.cpp
    sims3ds/src/files/formats/iff/chunks/ARRY.cpp)
  target_compile_features(odh-arry-literal-bounds PRIVATE cxx_std_17)
  target_include_directories(odh-arry-literal-bounds PRIVATE sims3ds/src)
  add_test(NAME arry-literal-bounds COMMAND odh-arry-literal-bounds)
  add_executable(odh-room-map-updates tests/room_map_updates.cpp)
  target_link_libraries(odh-room-map-updates PRIVATE odh_engine)
  add_test(NAME room-map-updates COMMAND odh-room-map-updates)
  add_executable(odh-object-room-lighting tests/object_room_lighting.cpp)
  target_link_libraries(odh-object-room-lighting PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-object-room-lighting PRIVATE -g0)
    target_link_options(odh-object-room-lighting PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME object-room-lighting COMMAND odh-object-room-lighting)
  add_executable(odh-cache-light-composition tests/cache_light_composition.cpp)
  target_link_libraries(odh-cache-light-composition PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-cache-light-composition PRIVATE -g0)
    target_link_options(odh-cache-light-composition PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME cache-light-composition COMMAND odh-cache-light-composition)
  add_executable(odh-visual-lighting tests/visual_lighting.cpp)
  target_link_libraries(odh-visual-lighting PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-visual-lighting PRIVATE -g0)
    target_link_options(odh-visual-lighting PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME visual-lighting COMMAND odh-visual-lighting)
  foreach(lighting_test spatial-lighting spatial-light-render desktop-family-friends terrain-lighting terrain-raster roof-lighting roof-geometry roof-sampling lot-picture-placement lot-picture-generation)
    string(REPLACE "-" "_" lighting_source "${lighting_test}")
    add_executable(odh-${lighting_test} tests/${lighting_source}.cpp)
    target_link_libraries(odh-${lighting_test} PRIVATE odh_engine)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_compile_options(odh-${lighting_test} PRIVATE -g0)
      target_link_options(odh-${lighting_test} PRIVATE -Wl,--strip-debug)
    endif()
    add_test(NAME ${lighting_test} COMMAND odh-${lighting_test})
  endforeach()
  add_executable(odh-lot-rezone-transaction tests/lot_rezone_transaction.cpp)
  target_link_libraries(odh-lot-rezone-transaction PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-lot-rezone-transaction PRIVATE -g0)
    target_link_options(odh-lot-rezone-transaction PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME lot-rezone-transaction COMMAND odh-lot-rezone-transaction)
  add_executable(odh-original-settings-contract ../tests/original_settings_view.cpp)
  target_link_libraries(odh-original-settings-contract PRIVATE odh_original_ui)
  add_test(NAME original-settings-contract COMMAND odh-original-settings-contract)
  add_executable(odh-desktop-options-bindings tests/desktop_options_bindings.cpp)
  target_link_libraries(odh-desktop-options-bindings PRIVATE odh_engine)
  add_test(NAME desktop-options-bindings COMMAND odh-desktop-options-bindings)
  add_executable(odh-house-panel-metrics-contract ../tests/house_panel_metrics.cpp)
  target_link_libraries(odh-house-panel-metrics-contract PRIVATE odh_original_ui)
  add_test(NAME house-panel-metrics-contract COMMAND odh-house-panel-metrics-contract)
  # Explicit resource-free matrix: pass a new disposable directory as argv[1].
  # It is deliberately not an automatic asset-backed gameplay or Save test.
  add_executable(odh-ts1-resource-patch-matrix EXCLUDE_FROM_ALL tests/ts1_resource_patch_matrix.cpp)
  target_link_libraries(odh-ts1-resource-patch-matrix PRIVATE odh_engine)
  add_executable(odh-original-crystal-projection tests/original_crystal_projection.cpp)
  target_link_libraries(odh-original-crystal-projection PRIVATE odh_engine)
  add_test(NAME original-crystal-projection COMMAND odh-original-crystal-projection)
  add_executable(odh-cas-text-encoding-contract tests/cas_text_encoding_contract.cpp)
  target_link_libraries(odh-cas-text-encoding-contract PRIVATE odh_engine)
  add_test(NAME cas-text-encoding-contract COMMAND odh-cas-text-encoding-contract)
  add_executable(odh-suit-host-contract tests/suit_host_contract.cpp)
  target_link_libraries(odh-suit-host-contract PRIVATE odh_engine)
  add_test(NAME suit-host-contract COMMAND odh-suit-host-contract)
  add_executable(odh-cas-person-data-contract tests/cas_person_data_contract.cpp)
  target_link_libraries(odh-cas-person-data-contract PRIVATE odh_engine)
  add_test(NAME cas-person-data-contract COMMAND odh-cas-person-data-contract)
  add_executable(odh-dialog-personal-text-contract tests/dialog_personal_text_contract.cpp)
  target_link_libraries(odh-dialog-personal-text-contract PRIVATE odh_engine)
  add_test(NAME dialog-personal-text-contract COMMAND odh-dialog-personal-text-contract ${CMAKE_CURRENT_BINARY_DIR}/dialog-personal-text-fixtures)
  add_executable(odh-game-font-text-backend tests/game_font_text_backend.cpp)
  target_link_libraries(odh-game-font-text-backend PRIVATE odh_engine)
  add_test(NAME game-font-text-backend COMMAND odh-game-font-text-backend)
  add_executable(odh-save-import-policy-contract tests/save_import_policy_contract.cpp)
  target_link_libraries(odh-save-import-policy-contract PRIVATE odh_engine)
  add_test(NAME save-import-policy-contract COMMAND odh-save-import-policy-contract)
  add_executable(odh-family-import-recovery tests/family_import_recovery.cpp)
  target_link_libraries(odh-family-import-recovery PRIVATE odh_engine)
  add_test(NAME family-import-recovery COMMAND odh-family-import-recovery)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(contract odh-save-import-policy-contract odh-family-import-recovery)
      target_compile_options(${contract} PRIVATE -g0)
      target_link_options(${contract} PRIVATE -Wl,--strip-debug)
    endforeach()
  endif()
  add_executable(odh-neighbourhood-template-reads tests/neighbourhood_template_reads.cpp)
  target_link_libraries(odh-neighbourhood-template-reads PRIVATE odh_engine)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-neighbourhood-template-reads PRIVATE -g0)
    target_link_options(odh-neighbourhood-template-reads PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME neighbourhood-template-reads COMMAND odh-neighbourhood-template-reads)
  add_executable(odh-expansion-lot-map-contract tests/expansion_lot_map_contract.cpp)
  target_link_libraries(odh-expansion-lot-map-contract PRIVATE odh_engine)
  add_test(NAME expansion-lot-map-contract COMMAND odh-expansion-lot-map-contract)
  add_executable(odh-ts1-event-camera-focus-contract tests/ts1_event_camera_focus_contract.cpp)
  target_link_libraries(odh-ts1-event-camera-focus-contract PRIVATE odh_engine)
  add_test(NAME ts1-event-camera-focus-contract COMMAND odh-ts1-event-camera-focus-contract)
  add_executable(odh-ts1-inventory-target-contract tests/ts1_inventory_target_contract.cpp)
  target_link_libraries(odh-ts1-inventory-target-contract PRIVATE odh_engine)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-ts1-inventory-target-contract PRIVATE -g0)
    target_link_options(odh-ts1-inventory-target-contract PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME ts1-inventory-target-contract COMMAND odh-ts1-inventory-target-contract)
  add_executable(odh-terrain-info-contract tests/terrain_info_contract.cpp)
  target_link_libraries(odh-terrain-info-contract PRIVATE odh_engine)
  add_test(NAME terrain-info-contract COMMAND odh-terrain-info-contract)
  add_executable(odh-travel-party-contract tests/travel_party_contract.cpp)
  target_link_libraries(odh-travel-party-contract PRIVATE odh_engine)
  add_test(NAME travel-party-contract COMMAND odh-travel-party-contract)
  add_executable(odh-desktop-expansion-contract ../tests/desktop_expansion_panels.cpp)
  target_link_libraries(odh-desktop-expansion-contract PRIVATE odh_engine)
  add_test(NAME desktop-expansion-contract COMMAND odh-desktop-expansion-contract)
  add_executable(odh-career-popup-text-contract tests/career_popup_text_contract.cpp)
  target_link_libraries(odh-career-popup-text-contract PRIVATE odh_original_ui)
  add_test(NAME career-popup-text-contract COMMAND odh-career-popup-text-contract)
  add_executable(odh-desktop-live-panel-contract ../tests/live_panel_view.cpp)
  target_link_libraries(odh-desktop-live-panel-contract PRIVATE odh_original_ui)
  add_test(NAME desktop-live-panel-contract COMMAND odh-desktop-live-panel-contract)
  add_executable(odh-desktop-ui-strings-contract ../tests/linux_ui_strings.cpp)
  target_link_libraries(odh-desktop-ui-strings-contract PRIVATE odh_original_ui)
  add_test(NAME desktop-ui-strings-contract COMMAND odh-desktop-ui-strings-contract)
  add_executable(odh-phonebook-dialog-contract tests/phonebook_dialog_contract.cpp)
  target_link_libraries(odh-phonebook-dialog-contract PRIVATE odh_engine)
  add_test(NAME phonebook-dialog-contract COMMAND odh-phonebook-dialog-contract)
  add_executable(odh-dialog-response-contract tests/dialog_response_contract.cpp)
  target_link_libraries(odh-dialog-response-contract PRIVATE odh_engine)
  add_test(NAME dialog-response-contract COMMAND odh-dialog-response-contract)
  add_executable(odh-shared-career-dialog-text-contract ../tests/career_dialog_text.cpp)
  target_link_libraries(odh-shared-career-dialog-text-contract PRIVATE odh_original_ui)
  add_test(NAME shared-career-dialog-text-contract COMMAND odh-shared-career-dialog-text-contract)
  add_executable(odh-career-dialog-contract tests/career_dialog_contract.cpp)
  target_link_libraries(odh-career-dialog-contract PRIVATE odh_engine)
  add_test(NAME career-dialog-contract COMMAND odh-career-dialog-contract)
  add_executable(odh-ts1-routing-footprint tests/ts1_routing_footprint.cpp)
  target_link_libraries(odh-ts1-routing-footprint PRIVATE odh_engine)
  add_test(NAME ts1-routing-footprint COMMAND odh-ts1-routing-footprint)
  add_executable(odh-frame-return-observer tests/frame_return_observer.cpp)
  target_link_libraries(odh-frame-return-observer PRIVATE odh_engine)
  add_test(NAME frame-return-observer COMMAND odh-frame-return-observer)
  add_executable(odh-ts1-deferred-removal tests/ts1_deferred_removal.cpp)
  target_link_libraries(odh-ts1-deferred-removal PRIVATE odh_engine)
  add_test(NAME ts1-deferred-removal COMMAND odh-ts1-deferred-removal)
  add_executable(odh-ts1-pending-removal-checkpoint tests/ts1_pending_removal_checkpoint.cpp)
  target_link_libraries(odh-ts1-pending-removal-checkpoint PRIVATE odh_engine)
  add_test(NAME ts1-pending-removal-checkpoint COMMAND odh-ts1-pending-removal-checkpoint)
  add_executable(odh-ts1-animation-clock tests/ts1_animation_clock.cpp)
  target_link_libraries(odh-ts1-animation-clock PRIVATE odh_engine)
  add_test(NAME ts1-animation-clock COMMAND odh-ts1-animation-clock)
  add_executable(odh-ts1-route-continuation tests/ts1_route_continuation.cpp)
  target_link_libraries(odh-ts1-route-continuation PRIVATE odh_engine)
  add_test(NAME ts1-route-continuation COMMAND odh-ts1-route-continuation)
  add_executable(odh-ts1-fresh-lifecycle tests/ts1_fresh_lifecycle.cpp)
  target_link_libraries(odh-ts1-fresh-lifecycle PRIVATE odh_engine)
  add_test(NAME ts1-fresh-lifecycle COMMAND odh-ts1-fresh-lifecycle)
  add_executable(odh-ts1-locomotion-contract tests/ts1_locomotion_contract.cpp)
  target_link_libraries(odh-ts1-locomotion-contract PRIVATE odh_engine)
  add_test(NAME ts1-locomotion-contract COMMAND odh-ts1-locomotion-contract)
  add_executable(odh-ts1-weighted-route-contract tests/ts1_weighted_route_contract.cpp)
  target_link_libraries(odh-ts1-weighted-route-contract PRIVATE odh_engine)
  add_test(NAME ts1-weighted-route-contract COMMAND odh-ts1-weighted-route-contract)
  add_executable(odh-ts1-relative-position-contract tests/ts1_relative_position_contract.cpp)
  target_link_libraries(odh-ts1-relative-position-contract PRIVATE odh_engine)
  add_test(NAME ts1-relative-position-contract COMMAND odh-ts1-relative-position-contract)
  add_executable(odh-ts1-height-quad-persistence tests/ts1_height_quad_persistence.cpp)
  target_link_libraries(odh-ts1-height-quad-persistence PRIVATE odh_engine)
  add_test(NAME ts1-height-quad-persistence COMMAND odh-ts1-height-quad-persistence)
  add_executable(odh-interaction-checkpoint-route-contract tests/interaction_checkpoint_route_contract.cpp)
  target_link_libraries(odh-interaction-checkpoint-route-contract PRIVATE odh_engine)
  add_test(NAME interaction-checkpoint-route-contract COMMAND odh-interaction-checkpoint-route-contract)
  add_executable(odh-motive-persistence tests/motive_persistence.cpp)
  target_compile_features(odh-motive-persistence PRIVATE cxx_std_17)
  target_include_directories(odh-motive-persistence PRIVATE sims3ds/src)
  add_test(NAME motive-persistence COMMAND odh-motive-persistence)
  add_executable(odh-objm-person-motive-order tests/objm_person_motive_order.cpp)
  target_link_libraries(odh-objm-person-motive-order PRIVATE odh_engine)
  add_test(NAME objm-person-motive-order COMMAND odh-objm-person-motive-order)
  add_executable(odh-ts1-rng-scope tests/ts1_rng_scope.cpp)
  target_link_libraries(odh-ts1-rng-scope PRIVATE odh_engine)
  add_test(NAME ts1-rng-scope COMMAND odh-ts1-rng-scope)
  add_executable(odh-ts1-global-definition tests/ts1_global_definition.cpp)
  target_compile_features(odh-ts1-global-definition PRIVATE cxx_std_17)
  target_include_directories(odh-ts1-global-definition PRIVATE sims3ds/src)
  add_test(NAME ts1-global-definition COMMAND odh-ts1-global-definition)

  add_executable(odh-simi-clock tests/simi_clock.cpp)
  target_link_libraries(odh-simi-clock PRIVATE odh_engine)
  add_test(NAME simi-clock COMMAND odh-simi-clock)
  add_executable(odh-ts1-budget-contract tests/ts1_budget_contract.cpp)
  target_link_libraries(odh-ts1-budget-contract PRIVATE odh_engine)
  add_test(NAME ts1-budget-contract COMMAND odh-ts1-budget-contract)
  add_executable(odh-ts1-daily-budget tests/ts1_daily_budget.cpp)
  target_link_libraries(odh-ts1-daily-budget PRIVATE odh_engine)
  add_test(NAME ts1-daily-budget COMMAND odh-ts1-daily-budget)
  add_executable(odh-ts1-budget-persistence tests/ts1_budget_persistence.cpp)
  target_link_libraries(odh-ts1-budget-persistence PRIVATE odh_engine)
  add_test(NAME ts1-budget-persistence COMMAND odh-ts1-budget-persistence)
  add_executable(odh-ts1-object-initial-state tests/ts1_object_initial_state.cpp)
  target_link_libraries(odh-ts1-object-initial-state PRIVATE odh_engine)
  add_test(NAME ts1-object-initial-state COMMAND odh-ts1-object-initial-state)
  add_executable(odh-ts1-object-use tests/ts1_object_use.cpp)
  target_link_libraries(odh-ts1-object-use PRIVATE odh_engine)
  add_test(NAME ts1-object-use COMMAND odh-ts1-object-use)
  add_executable(odh-original-scheduler-contract tests/original_scheduler_contract.cpp)
  target_link_libraries(odh-original-scheduler-contract PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-original-scheduler-contract PRIVATE -g0)
    target_link_options(odh-original-scheduler-contract PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME original-scheduler-contract COMMAND odh-original-scheduler-contract)
  add_executable(odh-ts1-scheduler-persistence tests/ts1_scheduler_persistence.cpp)
  target_link_libraries(odh-ts1-scheduler-persistence PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-ts1-scheduler-persistence PRIVATE -g0)
    target_link_options(odh-ts1-scheduler-persistence PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME ts1-scheduler-persistence COMMAND odh-ts1-scheduler-persistence)
  add_executable(odh-ts1-offworld-room tests/ts1_offworld_room.cpp)
  target_link_libraries(odh-ts1-offworld-room PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-ts1-offworld-room PRIVATE -g0)
    target_link_options(odh-ts1-offworld-room PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME ts1-offworld-room COMMAND odh-ts1-offworld-room)
  add_executable(odh-environment-score tests/environment_score.cpp)
  target_compile_features(odh-environment-score PRIVATE cxx_std_17)
  target_include_directories(odh-environment-score PRIVATE sims3ds/src)
  add_test(NAME environment-score COMMAND odh-environment-score)
  add_executable(odh-environment-geometry tests/environment_geometry.cpp)
  target_compile_features(odh-environment-geometry PRIVATE cxx_std_17)
  target_include_directories(odh-environment-geometry PRIVATE sims3ds/src)
  add_test(NAME environment-geometry COMMAND odh-environment-geometry)
  add_executable(odh-original-autonomy-contract tests/original_autonomy_contract.cpp)
  target_link_libraries(odh-original-autonomy-contract PRIVATE odh_engine)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-original-autonomy-contract PRIVATE -g0)
    target_link_options(odh-original-autonomy-contract PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME original-autonomy-contract COMMAND odh-original-autonomy-contract)
  add_executable(odh-original-action-queue-contract tests/original_action_queue_contract.cpp)
  target_link_libraries(odh-original-action-queue-contract PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-original-action-queue-contract PRIVATE -g0)
    target_link_options(odh-original-action-queue-contract PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME original-action-queue-contract COMMAND odh-original-action-queue-contract)
  add_executable(odh-relative-position-contract tests/relative_position_contract.cpp)
  target_link_libraries(odh-relative-position-contract PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-relative-position-contract PRIVATE -g0)
    target_link_options(odh-relative-position-contract PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME relative-position-contract COMMAND odh-relative-position-contract)
  add_test(NAME relative-position-collision-contract COMMAND odh-relative-position-contract --collision)
  add_executable(odh-check-query-isolation tests/check_query_isolation.cpp)
  target_link_libraries(odh-check-query-isolation PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-check-query-isolation PRIVATE -g0)
    target_link_options(odh-check-query-isolation PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME check-query-isolation COMMAND odh-check-query-isolation)
  add_executable(odh-saved-object-lifecycle tests/saved_object_lifecycle.cpp)
  target_link_libraries(odh-saved-object-lifecycle PRIVATE odh_engine)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-saved-object-lifecycle PRIVATE -g0)
    target_link_options(odh-saved-object-lifecycle PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME saved-object-lifecycle COMMAND odh-saved-object-lifecycle)

  add_executable(odh-thread-return-yield tests/thread_return_yield.cpp)
  target_link_libraries(odh-thread-return-yield PRIVATE odh_engine)
  add_test(NAME thread-return-yield COMMAND odh-thread-return-yield)
  add_executable(odh-neighbour-reconciliation tests/neighbour_reconciliation.cpp)
  target_link_libraries(odh-neighbour-reconciliation PRIVATE odh_engine)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-neighbour-reconciliation PRIVATE -g0)
    target_link_options(odh-neighbour-reconciliation PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME neighbour-reconciliation COMMAND odh-neighbour-reconciliation)
  add_executable(odh-social-reference-integrity tests/social_reference_integrity.cpp)
  target_link_libraries(odh-social-reference-integrity PRIVATE odh_engine)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(odh-social-reference-integrity PRIVATE -g0)
    target_link_options(odh-social-reference-integrity PRIVATE -Wl,--strip-debug)
  endif()
  add_test(NAME social-reference-integrity COMMAND odh-social-reference-integrity)
  add_executable(odh-family-admission-contract tests/family_admission_contract.cpp)
  target_link_libraries(odh-family-admission-contract PRIVATE odh_engine)
  add_test(NAME family-admission-contract COMMAND odh-family-admission-contract)
  add_executable(odh-magic-move-in tests/magic_move_in.cpp)
  target_link_libraries(odh-magic-move-in PRIVATE odh_engine)
  add_test(NAME magic-move-in COMMAND odh-magic-move-in)
  add_test(NAME magic-private-profile COMMAND odh-magic-move-in --private-profile)
  add_executable(odh-lot-pick-response-contract tests/lot_pick_response_contract.cpp)
  target_link_libraries(odh-lot-pick-response-contract PRIVATE odh_engine)
  add_test(NAME lot-pick-response-contract COMMAND odh-lot-pick-response-contract)
  add_executable(odh-app-frame-boundary-flow EXCLUDE_FROM_ALL tests/app_frame_boundary_flow.cpp)
  target_link_libraries(odh-app-frame-boundary-flow PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-app-frame-boundary-flow PRIVATE -g0)
    target_link_options(odh-app-frame-boundary-flow PRIVATE -Wl,--strip-debug)
  endif()
  add_executable(odh-avatar-slot-flow EXCLUDE_FROM_ALL tests/avatar_slot_flow.cpp)
  target_link_libraries(odh-avatar-slot-flow PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-avatar-slot-flow PRIVATE -g0)
    target_link_options(odh-avatar-slot-flow PRIVATE -Wl,--strip-debug)
  endif()
  # Asset-backed integration runner: explicitly invoked with isolated game/save
  # roots, rather than included in the resource-free CTest suite.
  add_executable(odh-residential-autonomy-flow EXCLUDE_FROM_ALL tests/residential_autonomy_flow.cpp)
  target_link_libraries(odh-residential-autonomy-flow PRIVATE odh_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(odh-residential-autonomy-flow PRIVATE -g0)
    target_link_options(odh-residential-autonomy-flow PRIVATE -Wl,--strip-debug)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND NOT CMAKE_CROSSCOMPILING)
    add_executable(odh-vita-printf-compat
      platform/vita/PrintfFormatCompat.cpp tests/vita_printf_compat.cpp)
    target_compile_features(odh-vita-printf-compat PRIVATE cxx_std_17)
    target_compile_options(odh-vita-printf-compat PRIVATE
      -fno-builtin -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
    target_link_options(odh-vita-printf-compat PRIVATE
      "-Wl,--wrap=printf,--wrap=fprintf,--wrap=sprintf,--wrap=snprintf,--wrap=vprintf,--wrap=vfprintf,--wrap=vsprintf,--wrap=vsnprintf,--wrap=malloc")
    add_test(NAME vita-printf-compat COMMAND odh-vita-printf-compat)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    add_executable(odh-language-setup-contract
      ${PROJECT_SOURCE_DIR}/tests/language_setup.cpp
      ${PROJECT_SOURCE_DIR}/tools/linux_language_setup.cpp
      ${PROJECT_SOURCE_DIR}/tools/linux_user_config.cpp
      ${PROJECT_SOURCE_DIR}/tools/neighborhood_storage.cpp)
    target_link_libraries(odh-language-setup-contract PRIVATE odh_original_ui)
    add_test(NAME language-setup-contract
      COMMAND odh-language-setup-contract ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  # Host filesystem policy, with synthetic device labels on Linux only.
  # These fixtures do not establish console filesystem or hardware execution.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(host_path_case pc switch vita)
      add_executable(odh-host-paths-${host_path_case} tests/host_resource_paths.cpp)
      target_compile_features(odh-host-paths-${host_path_case} PRIVATE cxx_std_17)
      target_include_directories(odh-host-paths-${host_path_case} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
      if(host_path_case STREQUAL "switch")
        target_compile_definitions(odh-host-paths-${host_path_case} PRIVATE __SWITCH__)
      elseif(host_path_case STREQUAL "vita")
        target_compile_definitions(odh-host-paths-${host_path_case} PRIVATE __vita__)
      endif()
      add_test(NAME host-paths-${host_path_case}
        COMMAND odh-host-paths-${host_path_case} "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
    add_executable(odh-bootstrap-unique-copy tests/bootstrap_unique_copy.cpp)
    target_link_libraries(odh-bootstrap-unique-copy PRIVATE odh_engine)
    add_test(NAME bootstrap-unique-copy COMMAND odh-bootstrap-unique-copy "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  add_executable(odh-console-device-paths tests/console_device_paths.cpp)
  target_compile_features(odh-console-device-paths PRIVATE cxx_std_17)
  add_test(NAME console-device-paths COMMAND odh-console-device-paths)
  add_executable(odh-visit-persistence-contract tests/visit_persistence_contract.cpp)
  target_link_libraries(odh-visit-persistence-contract PRIVATE odh_engine)
  add_test(NAME visit-persistence-contract COMMAND odh-visit-persistence-contract)
  add_executable(odh-ngbh-tail-persistence-contract tests/ngbh_tail_persistence_contract.cpp)
  target_link_libraries(odh-ngbh-tail-persistence-contract PRIVATE odh_engine)
  add_test(NAME ngbh-tail-persistence-contract COMMAND odh-ngbh-tail-persistence-contract)
  add_executable(odh-empty-object-snapshot tests/empty_object_snapshot.cpp)
  target_link_libraries(odh-empty-object-snapshot PRIVATE odh_engine)
  add_test(NAME empty-object-snapshot COMMAND odh-empty-object-snapshot)
  add_executable(odh-head-seek-contract tests/head_seek_contract.cpp)
  target_link_libraries(odh-head-seek-contract PRIVATE odh_engine)
  add_test(NAME head-seek-contract COMMAND odh-head-seek-contract)
  add_executable(odh-player-action-contract tests/player_action_contract.cpp)
  target_link_libraries(odh-player-action-contract PRIVATE odh_engine)
  add_test(NAME player-action-contract COMMAND odh-player-action-contract)
  add_executable(odh-engine-link-contract tests/engine_link_contract.cpp)
  target_link_libraries(odh-engine-link-contract PRIVATE odh_engine)
  add_test(NAME engine-link-contract COMMAND odh-engine-link-contract)
  # Manual fixture requires a licensed data root and an isolated prepared house69.
  # It is deliberately not a default CTest/commercial-data dependency.
  add_executable(odh-spellbook-progress-contract tests/spellbook_progress_contract.cpp)
  target_link_libraries(odh-spellbook-progress-contract PRIVATE odh_engine)
  add_test(NAME spellbook-progress-contract COMMAND odh-spellbook-progress-contract)
  add_executable(odh-help-queue-fixture tests/help_queue_fixture.cpp)
  target_link_libraries(odh-help-queue-fixture PRIVATE odh_engine)
  add_executable(odh-dialog-art-contract tests/dialog_art_contract.cpp)
  target_link_libraries(odh-dialog-art-contract PRIVATE odh_engine)
  add_test(NAME dialog-art-contract COMMAND odh-dialog-art-contract)
  if(ODH_BUILD_DESKTOP)
    add_executable(odh-desktop-pie-pointer tests/desktop_pie_pointer.cpp)
    target_link_libraries(odh-desktop-pie-pointer PRIVATE odh_engine)
    add_test(NAME desktop-pie-pointer COMMAND odh-desktop-pie-pointer)
    add_executable(odh-desktop-pie-head-motion tests/desktop_pie_head_motion.cpp)
    target_compile_features(odh-desktop-pie-head-motion PRIVATE cxx_std_17)
    target_include_directories(odh-desktop-pie-head-motion PRIVATE sims3ds/src)
    add_test(NAME desktop-pie-head-motion COMMAND odh-desktop-pie-head-motion)
    add_executable(odh-desktop-cursor-refresh tests/desktop_cursor_refresh.cpp)
    target_compile_features(odh-desktop-cursor-refresh PRIVATE cxx_std_17)
    target_include_directories(odh-desktop-cursor-refresh PRIVATE sims3ds/src)
    add_test(NAME desktop-cursor-refresh COMMAND odh-desktop-cursor-refresh)
    add_executable(odh-desktop-pointer-cancellation tests/desktop_pointer_cancellation.cpp)
    target_link_libraries(odh-desktop-pointer-cancellation PRIVATE odh_engine)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_compile_options(odh-desktop-pointer-cancellation PRIVATE -g0)
      target_link_options(odh-desktop-pointer-cancellation PRIVATE -Wl,--strip-debug)
    endif()
    add_test(NAME desktop-pointer-cancellation COMMAND odh-desktop-pointer-cancellation)
    add_executable(odh-desktop-input-sequence tests/desktop_input_sequence.cpp)
    target_compile_features(odh-desktop-input-sequence PRIVATE cxx_std_17)
    target_include_directories(odh-desktop-input-sequence PRIVATE
      "${ODH_ENGINE_SOURCE_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}/platform/desktop")
    add_test(NAME desktop-input-sequence COMMAND odh-desktop-input-sequence)
  endif()
endif()
