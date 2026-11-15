cmake_minimum_required(VERSION 3.22)
project(OpenDollhouseVita VERSION 0.0.1 LANGUAGES C CXX)
if(NOT VITASDK)
  message(FATAL_ERROR "Use VitaSDK's vita.toolchain.cmake")
endif()
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(ODH_BUILD_DESKTOP OFF CACHE BOOL "" FORCE)
set(ODH_BUILD_LEGACY_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
find_package(SDL2 2.0.18 CONFIG REQUIRED)
set(ODH_VITA_ICONV_ROOT "" CACHE PATH "Local static libiconv prefix from prepare-vita-iconv.sh")
if(NOT EXISTS "${ODH_VITA_ICONV_ROOT}/include/iconv.h" OR
   NOT EXISTS "${ODH_VITA_ICONV_ROOT}/lib/libiconv.a")
  message(FATAL_ERROR "Prepare Vita iconv and set ODH_VITA_ICONV_ROOT; SDK headers alone are insufficient")
endif()
set(Iconv_IS_BUILT_IN FALSE CACHE BOOL "Use the Vita static conversion library" FORCE)
set(Iconv_INCLUDE_DIR "${ODH_VITA_ICONV_ROOT}/include" CACHE PATH "Vita iconv header" FORCE)
set(Iconv_LIBRARY "${ODH_VITA_ICONV_ROOT}/lib/libiconv.a" CACHE FILEPATH "Vita iconv library" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../.." "${CMAKE_BINARY_DIR}/engine-root")
add_executable(opendollhouse VitaMain.cpp StdioCapture.cpp PrintfFormatCompat.cpp ../desktop/SdlAudio.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../../tools/reference_patch_setup.cpp")
target_compile_features(opendollhouse PRIVATE cxx_std_17)
target_compile_definitions(opendollhouse PRIVATE SDL_MAIN_HANDLED ODH_PLATFORM_VITA)
target_link_libraries(opendollhouse PRIVATE odh_engine SDL2::SDL2-static SceSysmem_stub)
# The installed Vita newlib does not support the C99 z/j/t printf lengths.
# Keep this ABI adapter local to this executable, including static dependencies.
target_link_options(opendollhouse PRIVATE
  "-Wl,--wrap=printf,--wrap=fprintf,--wrap=sprintf,--wrap=snprintf,--wrap=vprintf,--wrap=vfprintf,--wrap=vsprintf,--wrap=vsnprintf")
vita_create_self(eboot.bin opendollhouse UNSAFE)
set(odh_notices
  FILE "${CMAKE_CURRENT_LIST_DIR}/../../sims3ds/LICENSE" "licenses/Sims3DS-MPL-2.0.txt"
  FILE "${CMAKE_CURRENT_LIST_DIR}/../../sims3ds/CREDITS.md" "licenses/Sims3DS-CREDITS.md"
  FILE "${CMAKE_CURRENT_LIST_DIR}/../../sims3ds/src/third_party/minimp3/LICENSE" "licenses/minimp3.txt"
  FILE "${ODH_SDL_SOURCE}/LICENSE.txt" "licenses/SDL2.txt"
  FILE "${ODH_ZLIB_SOURCE}/LICENSE" "licenses/zlib.txt"
  FILE "${ODH_VITA_ICONV_ROOT}/share/licenses/libiconv-LGPL-2.1.txt" "licenses/libiconv-LGPL-2.1.txt"
  FILE "${ODH_VITA_ICONV_ROOT}/share/licenses/libiconv-source.txt" "licenses/libiconv-source.txt")
file(GLOB odh_engine_notices "${CMAKE_CURRENT_LIST_DIR}/../../sims3ds/LICENSES/*" "${CMAKE_CURRENT_LIST_DIR}/licenses/*")
foreach(odh_notice IN LISTS odh_engine_notices)
  get_filename_component(odh_leaf "${odh_notice}" NAME)
  list(APPEND odh_notices FILE "${odh_notice}" "licenses/${odh_leaf}")
endforeach()
vita_create_vpk(OpenDollhouse.vpk ODH000001 eboot.bin VERSION 00.01 NAME "OpenDollhouse" ${odh_notices})
