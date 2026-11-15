# Windows x86-64 from a Linux host with the POSIX-threaded MinGW toolchain.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

if(NOT ODH_MINGW_SYSROOT)
  set(ODH_MINGW_SYSROOT /usr/x86_64-w64-mingw32 CACHE PATH "MinGW target headers and libraries")
endif()
set(CMAKE_FIND_ROOT_PATH "${ODH_MINGW_SYSROOT}")
# Additional dependencies must contain Windows libraries, not Linux libraries.
if(ODH_TARGET_DEPS)
  list(APPEND CMAKE_FIND_ROOT_PATH "${ODH_TARGET_DEPS}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
