# Existing portable presentation and asset readers, shared with diagnostic tools.
# Deliberately excludes the previous VM, World implementation and Linux frontend.
set(ODH_ORIGINAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_library(odh_original_ui STATIC
  ${ODH_ORIGINAL_ROOT}/src/formats.cpp
  ${ODH_ORIGINAL_ROOT}/src/cursor.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_cursors.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_cursor_policy.cpp
  ${ODH_ORIGINAL_ROOT}/src/animation.cpp
  ${ODH_ORIGINAL_ROOT}/src/career.cpp
  ${ODH_ORIGINAL_ROOT}/tools/career_dialog_text.cpp
  ${ODH_ORIGINAL_ROOT}/src/skeleton.cpp
  ${ODH_ORIGINAL_ROOT}/src/mesh.cpp
  ${ODH_ORIGINAL_ROOT}/src/bitmap.cpp
  ${ODH_ORIGINAL_ROOT}/src/sprite.cpp
  ${ODH_ORIGINAL_ROOT}/src/dgrp.cpp
  ${ODH_ORIGINAL_ROOT}/src/objd.cpp
  ${ODH_ORIGINAL_ROOT}/src/object_definition.cpp
  ${ODH_ORIGINAL_ROOT}/src/definition_index.cpp
  ${ODH_ORIGINAL_ROOT}/src/pose.cpp
  ${ODH_ORIGINAL_ROOT}/src/skinning.cpp
  ${ODH_ORIGINAL_ROOT}/src/projection.cpp
  ${ODH_ORIGINAL_ROOT}/src/raster.cpp
  ${ODH_ORIGINAL_ROOT}/src/compositor.cpp
  ${ODH_ORIGINAL_ROOT}/src/ui_bitmap.cpp
  ${ODH_ORIGINAL_ROOT}/src/sha256.cpp
  ${ODH_ORIGINAL_ROOT}/src/pie_menu.cpp
  ${ODH_ORIGINAL_ROOT}/src/neighborhood_picker.cpp
  ${ODH_ORIGINAL_ROOT}/src/lot_locations.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_ui_art.cpp
  ${ODH_ORIGINAL_ROOT}/tools/house_panel_metrics.cpp
  ${ODH_ORIGINAL_ROOT}/tools/house_panel_resources.cpp
  ${ODH_ORIGINAL_ROOT}/src/ts1_curve.cpp
  ${ODH_ORIGINAL_ROOT}/src/named_float_constants.cpp
  ${ODH_ORIGINAL_ROOT}/tools/live_panel_view.cpp
  ${ODH_ORIGINAL_ROOT}/tools/linux_motive_panel.cpp
  ${ODH_ORIGINAL_ROOT}/tools/linux_ui_strings.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_control_panel.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_options_view.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_settings_view.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_queue_view.cpp
  ${ODH_ORIGINAL_ROOT}/tools/catalog_object_thumbnail_prepared.cpp
  ${ODH_ORIGINAL_ROOT}/tools/catalog_info_view.cpp
  ${ODH_ORIGINAL_ROOT}/tools/catalog_info_text.cpp
  ${ODH_ORIGINAL_ROOT}/tools/character_ui_images.cpp
  ${ODH_ORIGINAL_ROOT}/tools/saved_appearance.cpp
  ${ODH_ORIGINAL_ROOT}/tools/scene_renderer.cpp
  ${ODH_ORIGINAL_ROOT}/tools/linux_cas_preview.cpp
  ${ODH_ORIGINAL_ROOT}/tools/linux_pie_menu.cpp
  ${ODH_ORIGINAL_ROOT}/tools/linux_vm_dialog.cpp
  ${ODH_ORIGINAL_ROOT}/src/dialog_text.cpp
  ${ODH_ORIGINAL_ROOT}/tools/ui_hover.cpp
  ${ODH_ORIGINAL_ROOT}/tools/phonebook_candidates.cpp
  ${ODH_ORIGINAL_ROOT}/tools/language_picker.cpp
  ${ODH_ORIGINAL_ROOT}/tools/original_tooltip_view.cpp
  ${ODH_ORIGINAL_ROOT}/tools/text_overlay_backend.cpp
  ${ODH_ORIGINAL_ROOT}/tools/neighborhood_presentation.cpp
  ${ODH_ORIGINAL_ROOT}/tools/neighborhood_resources.cpp
  ${ODH_ORIGINAL_ROOT}/tools/selected_sim_crystal.cpp
  ${ODH_ORIGINAL_ROOT}/tools/cas_presentation.cpp
  ${ODH_ORIGINAL_ROOT}/tools/linux_text_codec.cpp)
target_include_directories(odh_original_ui PUBLIC
  "${ODH_ORIGINAL_ROOT}/include" "${ODH_ORIGINAL_ROOT}/tools")
target_compile_features(odh_original_ui PUBLIC cxx_std_17)
# Historical UI headers use this capability name; the implementation is now
# a replaceable text backend and does not require a Linux window or frontend.
target_compile_definitions(odh_original_ui PUBLIC ODH_HAVE_LINUX_TEXT_RENDERER)
if(NOT WIN32)
  # Reuse strict per-language conversion; console/Android toolchains must
  # supply this service rather than silently corrupting character names.
  find_package(Iconv REQUIRED)
  target_link_libraries(odh_original_ui PRIVATE Iconv::Iconv)
endif()
option(ODH_ORIGINAL_UI_PANGO "Use the preserved Pango text backend on native Linux" ON)
set(ODH_UI_HAS_PANGO FALSE PARENT_SCOPE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING AND ODH_ORIGINAL_UI_PANGO)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ODH_UI_PANGO REQUIRED IMPORTED_TARGET pangocairo)
  target_sources(odh_original_ui PRIVATE ${ODH_ORIGINAL_ROOT}/tools/linux_text_overlay_backend.cpp)
  target_compile_definitions(odh_original_ui PRIVATE ODH_TEXT_PANGO_BACKEND)
  target_link_libraries(odh_original_ui PRIVATE PkgConfig::ODH_UI_PANGO)
  set(ODH_UI_HAS_PANGO TRUE PARENT_SCOPE)
endif()
set_target_properties(odh_original_ui PROPERTIES CXX_EXTENSIONS OFF)

# Preserve localized UTF-8 returned by the shared original UI reader.
set_source_files_properties(${ODH_ORIGINAL_ROOT}/tools/linux_ui_strings.cpp
  PROPERTIES COMPILE_DEFINITIONS ODH_HAVE_LINUX_TEXT_CODEC)
