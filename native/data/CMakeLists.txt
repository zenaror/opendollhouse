cmake_minimum_required(VERSION 3.16)
project(odh_data VERSION 1.0 LANGUAGES C CXX)

add_library(odh_data SHARED
    src/odh_data.cpp
    ../../src/formats.cpp)
target_compile_features(odh_data PRIVATE cxx_std_17)
target_compile_definitions(odh_data PRIVATE ODH_DATA_SHARED_BUILD)
target_include_directories(odh_data PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_include_directories(odh_data PRIVATE ../../include)
set_target_properties(odh_data PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION 1.0
    SOVERSION 1)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(export_map "${CMAKE_CURRENT_BINARY_DIR}/odh_data.exports.map")
    file(WRITE "${export_map}" "{ global: odh_data_abi_version; odh_bcon_decode_v1; local: *; };\n")
    target_link_options(odh_data PRIVATE "LINKER:--version-script=${export_map}")
elseif(APPLE)
    set(export_list "${CMAKE_CURRENT_BINARY_DIR}/odh_data.exports.list")
    file(WRITE "${export_list}" "_odh_data_abi_version\n_odh_bcon_decode_v1\n")
    target_link_options(odh_data PRIVATE "LINKER:-exported_symbols_list,${export_list}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(odh_data_tests tests/test_abi.cpp)
    target_compile_features(odh_data_tests PRIVATE cxx_std_17)
    target_link_libraries(odh_data_tests PRIVATE odh_data)
    add_test(NAME odh_data_abi COMMAND odh_data_tests)
    add_executable(odh_data_c_header tests/test_c_header.c)
    target_link_libraries(odh_data_c_header PRIVATE odh_data)
    add_test(NAME odh_data_c_header COMMAND odh_data_c_header)
endif()
