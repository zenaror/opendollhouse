cmake_minimum_required(VERSION 3.16)
project(odh_simulation VERSION 1.0 LANGUAGES C CXX)

add_library(odh_simulation SHARED src/odh_simulation.cpp)
target_compile_features(odh_simulation PRIVATE cxx_std_17)
target_compile_definitions(odh_simulation PRIVATE ODH_SIMULATION_SHARED_BUILD)
target_include_directories(odh_simulation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
set_target_properties(odh_simulation PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION 1.0
    SOVERSION 1)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(export_map "${CMAKE_CURRENT_BINARY_DIR}/odh_simulation.exports.map")
    file(WRITE "${export_map}" "{ global: odh_wake_*; local: *; };\n")
    target_link_options(odh_simulation PRIVATE "LINKER:--version-script=${export_map}")
elseif(APPLE)
    set(export_list "${CMAKE_CURRENT_BINARY_DIR}/odh_simulation.exports.list")
    file(WRITE "${export_list}" "_odh_wake_abi_version\n_odh_wake_create_v1\n_odh_wake_destroy_v1\n_odh_wake_schedule_v1\n_odh_wake_schedule_current_v1\n_odh_wake_deschedule_v1\n_odh_wake_prepare_interrupt_v1\n_odh_wake_begin_v1\n_odh_wake_select_current_v1\n_odh_wake_current_count_v1\n_odh_wake_current_at_v1\n_odh_wake_complete_v1\n_odh_wake_reset_v1\n_odh_wake_stats_v1\n")
    target_link_options(odh_simulation PRIVATE "LINKER:-exported_symbols_list,${export_list}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(odh_simulation_tests tests/test_queue.cpp)
    target_compile_features(odh_simulation_tests PRIVATE cxx_std_17)
    target_link_libraries(odh_simulation_tests PRIVATE odh_simulation)
    add_test(NAME odh_simulation_queue COMMAND odh_simulation_tests)
    add_executable(odh_simulation_c_header tests/test_c_header.c)
    target_link_libraries(odh_simulation_c_header PRIVATE odh_simulation)
    add_test(NAME odh_simulation_c_header COMMAND odh_simulation_c_header)
endif()
