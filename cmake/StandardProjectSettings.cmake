# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Make compilation more verbose if required.
option(VERBOSE "Verbose compiler output" OFF)

# If not build type is specified, default to Release.
set (default_build_type "Release")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message (STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set (CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
    set_property (CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif ()

# Set C++ compiler flags.
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)

if (VERBOSE)
  set (CMAKE_VERBOSE_MAKEFILE ON)
endif ()

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
  set (BUILD_TYPE "rel")
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    string (APPEND CMAKE_CXX_FLAGS " -O3 -g0")
  elseif (MSVC)
    string (APPEND CMAKE_CXX_FLAGS " /MD /DEBUG:NONE /O2")
  else ()
    message (FATAL_ERROR "Unsupported compiler")
  endif ()
else ()
  set (BUILD_TYPE "dbg")
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O0 -g3")
  elseif (MSVC)
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Wall /Od")
  else ()
    message (FATAL_ERROR "Unsupported compiler")
  endif ()
endif ()

# Enable standard C++ exception handling on MSVC. Qt and C++/WinRT both throw (e.g.
# winrt::hresult_error from check_hresult), and our own try/catch containment (log.h
# Guarded, the WGC capture handler) only works with unwind semantics enabled. Without
# /EHsc the toolchain warns C4577 ("termination on exception is not guaranteed") and a
# thrown exception can bypass our catches and abort — exactly the crash we hardened
# against. Assume extern "C" functions never throw (the 'c').
if (MSVC)
  string (APPEND CMAKE_CXX_FLAGS " /EHsc")
endif ()