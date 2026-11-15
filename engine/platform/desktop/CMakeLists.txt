# SDL's imported macOS library has C linkage and @rpath metadata. Load CMake's
# C platform rules even though the application's own translation units are C++.
if(APPLE)
  enable_language(C)
endif()
find_package(SDL2 2.0.18 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(ODH_SDL2 QUIET IMPORTED_TARGET sdl2>=2.0.18)
  endif()
  if(NOT TARGET PkgConfig::ODH_SDL2)
    message(FATAL_ERROR "The desktop application requires SDL2 development files (2.0.18 or newer). Install the SDL2 development package or set CMAKE_PREFIX_PATH to its prefix.")
  endif()
  add_library(SDL2::SDL2 ALIAS PkgConfig::ODH_SDL2)
endif()

add_executable(opendollhouse main.cpp SdlAudio.cpp
  ${PROJECT_SOURCE_DIR}/tools/reference_patch_setup.cpp)
target_compile_features(opendollhouse PRIVATE cxx_std_17)
target_compile_definitions(opendollhouse PRIVATE SDL_MAIN_HANDLED)
if(ODH_UI_HAS_PANGO)
  target_compile_definitions(opendollhouse PRIVATE ODH_DESKTOP_PANGO)
endif()
target_link_libraries(opendollhouse PRIVATE odh_engine SDL2::SDL2)
if((CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID) OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_compile_definitions(opendollhouse PRIVATE ODH_DESKTOP_LANGUAGE_SETUP)
  target_sources(opendollhouse PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/linux_language_setup.cpp
    ${PROJECT_SOURCE_DIR}/tools/linux_user_config.cpp
    ${PROJECT_SOURCE_DIR}/tools/neighborhood_storage.cpp)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(opendollhouse PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS opendollhouse RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  add_executable(odh-desktop-audio
    ${PROJECT_SOURCE_DIR}/engine/tests/desktop_audio.cpp SdlAudio.cpp)
  target_compile_features(odh-desktop-audio PRIVATE cxx_std_17)
  target_compile_definitions(odh-desktop-audio PRIVATE SDL_MAIN_HANDLED)
  target_include_directories(odh-desktop-audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(odh-desktop-audio PRIVATE odh_engine SDL2::SDL2)
  add_test(NAME desktop-audio COMMAND odh-desktop-audio
    "${CMAKE_CURRENT_BINARY_DIR}/desktop-audio-fixtures")
  add_executable(odh-sdl-portable-input
    ${PROJECT_SOURCE_DIR}/engine/tests/sdl_portable_input.cpp)
  target_compile_features(odh-sdl-portable-input PRIVATE cxx_std_17)
  target_compile_definitions(odh-sdl-portable-input PRIVATE SDL_MAIN_HANDLED)
  target_include_directories(odh-sdl-portable-input PRIVATE
    ${PROJECT_SOURCE_DIR}/engine/platform/desktop
    ${PROJECT_SOURCE_DIR}/engine/sims3ds/src ${PROJECT_SOURCE_DIR}/include)
  target_link_libraries(odh-sdl-portable-input PRIVATE SDL2::SDL2)
  add_test(NAME sdl-portable-input COMMAND odh-sdl-portable-input)
  add_executable(odh-desktop-paths-contract
    ${PROJECT_SOURCE_DIR}/engine/tests/desktop_paths.cpp)
  target_compile_features(odh-desktop-paths-contract PRIVATE cxx_std_17)
  add_test(NAME desktop-paths-contract COMMAND odh-desktop-paths-contract
    "${CMAKE_CURRENT_BINARY_DIR}/desktop-path-fixtures")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    # SDL declares these entry points with C linkage; use the existing C++
    # toolchain and SDL include paths for this optional Linux interposer.
    set_source_files_properties(${PROJECT_SOURCE_DIR}/engine/tests/desktop_presentation_fault.c
      PROPERTIES LANGUAGE CXX)
    add_library(odh-desktop-presentation-fault SHARED EXCLUDE_FROM_ALL
      ${PROJECT_SOURCE_DIR}/engine/tests/desktop_presentation_fault.c)
    target_link_libraries(odh-desktop-presentation-fault PRIVATE SDL2::SDL2 ${CMAKE_DL_LIBS})
    target_compile_options(odh-desktop-presentation-fault PRIVATE -g0)
  endif()
  add_executable(odh-reference-patch-setup-contract
    ${PROJECT_SOURCE_DIR}/tests/reference_patch_setup.cpp
    ${PROJECT_SOURCE_DIR}/tools/reference_patch_setup.cpp)
  target_link_libraries(odh-reference-patch-setup-contract PRIVATE odh_original_ui)
  add_test(NAME reference-patch-setup-contract COMMAND odh-reference-patch-setup-contract
    "${CMAKE_CURRENT_BINARY_DIR}/reference-patch-fixtures")
endif()
