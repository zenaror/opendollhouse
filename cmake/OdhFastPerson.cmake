include_guard(GLOBAL)

option(ODH_BUILD_FAST_PERSON
  "Add an explicit host-only odh-person-fast iteration target (not release acceptance)" OFF)

# Include only after odh-person's final source/property/dependency declarations.
# This duplicates application translation units, never the shared engine graph.
function(odh_add_fast_person)
  if(NOT ODH_BUILD_FAST_PERSON)
    return()
  endif()
  if(CMAKE_CROSSCOMPILING OR MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR "ODH_BUILD_FAST_PERSON requires a native GCC or Clang host compiler")
  endif()
  if(CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "ODH_BUILD_FAST_PERSON currently requires a single-config generator")
  endif()
  if(NOT TARGET odh-person)
    message(FATAL_ERROR "ODH_BUILD_FAST_PERSON requires the completed odh-person target")
  endif()
  if(TARGET odh-person-fast)
    message(FATAL_ERROR "odh-person-fast already exists")
  endif()

  get_property(reused_pch TARGET odh-person PROPERTY PRECOMPILE_HEADERS_REUSE_FROM)
  if(reused_pch)
    message(FATAL_ERROR "ODH_BUILD_FAST_PERSON cannot reuse a precompiled header built with production flags")
  endif()

  get_property(person_sources TARGET odh-person PROPERTY SOURCES)
  add_executable(odh-person-fast EXCLUDE_FROM_ALL ${person_sources})

  # Copy the final application graph rather than maintain another platform or
  # library list. SET distinguishes absent optional properties from values;
  # get_property does not inject get_target_property's *-NOTFOUND sentinel.
  set(person_properties
    COMPILE_DEFINITIONS COMPILE_FEATURES COMPILE_OPTIONS COMPILE_FLAGS
    INCLUDE_DIRECTORIES SYSTEM_INCLUDE_DIRECTORIES
    LINK_LIBRARIES LINK_OPTIONS LINK_FLAGS LINK_DIRECTORIES LINK_DEPENDS
    CXX_STANDARD CXX_STANDARD_REQUIRED CXX_EXTENSIONS LINKER_LANGUAGE
    POSITION_INDEPENDENT_CODE ENABLE_EXPORTS
    PRECOMPILE_HEADERS DISABLE_PRECOMPILE_HEADERS
    UNITY_BUILD UNITY_BUILD_BATCH_SIZE UNITY_BUILD_MODE
    BUILD_RPATH BUILD_RPATH_USE_ORIGIN BUILD_WITH_INSTALL_RPATH
    INSTALL_RPATH INSTALL_RPATH_USE_LINK_PATH SKIP_BUILD_RPATH
    WIN32_EXECUTABLE MACOSX_BUNDLE MACOSX_BUNDLE_INFO_PLIST
    MACOSX_BUNDLE_BUNDLE_NAME MACOSX_BUNDLE_GUI_IDENTIFIER
    MACOSX_BUNDLE_ICON_FILE MACOSX_BUNDLE_BUNDLE_VERSION
    MACOSX_BUNDLE_SHORT_VERSION_STRING MACOSX_BUNDLE_LONG_VERSION_STRING
    MACOSX_BUNDLE_COPYRIGHT OSX_ARCHITECTURES
    RUNTIME_OUTPUT_DIRECTORY LIBRARY_OUTPUT_DIRECTORY ARCHIVE_OUTPUT_DIRECTORY
    PREFIX SUFFIX)
  set(person_configurations DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" person_configuration)
    list(APPEND person_configurations "${person_configuration}")
  endif()
  list(REMOVE_DUPLICATES person_configurations)
  foreach(configuration IN LISTS person_configurations)
    foreach(property COMPILE_DEFINITIONS LINK_FLAGS
        RUNTIME_OUTPUT_DIRECTORY LIBRARY_OUTPUT_DIRECTORY ARCHIVE_OUTPUT_DIRECTORY)
      list(APPEND person_properties "${property}_${configuration}")
    endforeach()
    list(APPEND person_properties "${configuration}_POSTFIX")
  endforeach()
  foreach(property IN LISTS person_properties)
    get_property(property_set TARGET odh-person PROPERTY "${property}" SET)
    if(property_set)
      get_property(property_value TARGET odh-person PROPERTY "${property}")
      set_property(TARGET odh-person-fast PROPERTY "${property}" "${property_value}")
    endif()
  endforeach()
  get_property(person_dependencies TARGET odh-person PROPERTY MANUALLY_ADDED_DEPENDENCIES)
  if(person_dependencies)
    add_dependencies(odh-person-fast ${person_dependencies})
  endif()

  # Only this executable's sources get iteration flags. Engine libraries retain
  # their configured optimization. Never copy OUTPUT_NAME: both artifacts must
  # coexist, and no install rule or acceptance test targets the fast executable.
  target_compile_options(odh-person-fast PRIVATE -O0 -g0)
  set_property(TARGET odh-person-fast PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
  foreach(configuration IN LISTS person_configurations)
    set_property(TARGET odh-person-fast PROPERTY "INTERPROCEDURAL_OPTIMIZATION_${configuration}" OFF)
  endforeach()
endfunction()

odh_add_fast_person()
