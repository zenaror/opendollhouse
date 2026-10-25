# A Git hook may export an alternate index or repository. Probe only this
# source checkout; environment changes remain local to this cmake -P process.
foreach(variable GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE
    GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES
    GIT_CONFIG_COUNT GIT_CONFIG_PARAMETERS GIT_CONFIG GIT_CONFIG_SYSTEM GIT_CONFIG_GLOBAL)
  unset(ENV{${variable}})
endforeach()
set(revision unknown)
# An archive unpacked below some other repository must never inherit its HEAD.
if(EXISTS "${ODH_REVISION_SOURCE_DIR}/.git")
  find_program(git_executable git)
  if(git_executable)
    execute_process(COMMAND "${git_executable}" -C "${ODH_REVISION_SOURCE_DIR}"
      rev-parse --show-toplevel OUTPUT_VARIABLE git_root
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE root_result)
    get_filename_component(source_root "${ODH_REVISION_SOURCE_DIR}" REALPATH)
    get_filename_component(git_root "${git_root}" REALPATH)
    if(root_result EQUAL 0 AND git_root STREQUAL source_root)
      execute_process(COMMAND "${git_executable}" -C "${source_root}" rev-parse --verify HEAD
        OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE head_result)
      string(LENGTH "${head}" head_length)
      execute_process(COMMAND "${git_executable}" -C "${source_root}"
        status --porcelain --untracked-files=no --ignore-submodules=untracked
        OUTPUT_VARIABLE tracked_status OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE status_result)
      if(head_result EQUAL 0 AND status_result EQUAL 0 AND head MATCHES "^[0-9a-fA-F]+$"
          AND (head_length EQUAL 40 OR head_length EQUAL 64))
        set(revision "${head}")
        if(NOT tracked_status STREQUAL "")
          string(APPEND revision "-dirty")
        endif()
      endif()
    endif()
  endif()
endif()
set(content "#pragma once\n#define ODH_BUILD_REVISION \"${revision}\"\n")
set(previous_content "")
if(EXISTS "${ODH_REVISION_HEADER}")
  file(READ "${ODH_REVISION_HEADER}" previous_content)
endif()
if(NOT content STREQUAL previous_content)
  get_filename_component(output_dir "${ODH_REVISION_HEADER}" DIRECTORY)
  file(MAKE_DIRECTORY "${output_dir}")
  file(WRITE "${ODH_REVISION_HEADER}" "${content}")
endif()
