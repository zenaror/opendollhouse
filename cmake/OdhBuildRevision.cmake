include_guard(GLOBAL)
set_property(GLOBAL PROPERTY ODH_BUILD_REVISION_SCRIPT
  "${CMAKE_CURRENT_LIST_DIR}/UpdateBuildRevision.cmake")

# Run the probe on every build, but only invalidate C++ compilation when its
# contents change. This also supports Git worktrees, whose .git is a file.
function(odh_attach_build_revision target source_dir)
  get_property(revision_script GLOBAL PROPERTY ODH_BUILD_REVISION_SCRIPT)
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
  set(header "${output_dir}/odh_build_revision.hpp")
  add_custom_target(${target}_build_revision
    COMMAND "${CMAKE_COMMAND}" "-DODH_REVISION_SOURCE_DIR=${source_dir}"
      "-DODH_REVISION_HEADER=${header}"
      -P "${revision_script}"
    BYPRODUCTS "${header}"
    VERBATIM)
  add_dependencies(${target} ${target}_build_revision)
  target_include_directories(${target} PRIVATE "${output_dir}")
  target_compile_definitions(${target} PRIVATE ODH_HAVE_BUILD_REVISION_HEADER=1)
endfunction()
