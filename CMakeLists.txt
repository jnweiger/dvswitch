cmake_minimum_required(VERSION 2.4)
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
    cmake_policy(SET CMP0005 OLD)
endif(COMMAND cmake_policy)

project(DVSWITCH CXX C)
set(PACKAGE_VERSION 0.9~alpha)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
include(FindGettext)
include(FindPkgConfig)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(OSC REQUIRED liblo)
pkg_check_modules(JACK REQUIRED jack)
pkg_check_modules(GTKMM REQUIRED gtkmm-2.4)
pkg_check_modules(LIBAVCODEC REQUIRED libavcodec)
pkg_check_modules(LIBAVUTIL REQUIRED libavutil)

message(STATUS "checking for Boost.Thread")
find_library(BOOST_THREAD_LIBRARIES
	     NAMES boost_thread-mt boost_thread
	     DOC "Boost.Thread library path"
	     NO_SYSTEM_ENVIRONMENT_PATH)
if(NOT BOOST_THREAD_LIBRARIES)
    message(FATAL_ERROR "Boost.Thread library not found")
endif(NOT BOOST_THREAD_LIBRARIES)
message(STATUS "  found Boost.Thread")

find_library(ALSA_LIBRARIES asound)

find_package(LiveMedia REQUIRED)

# Cope with recent move of ffmpeg headers into subdirectories
set(LIBAVCODEC_INCLUDE_DIRS ${LIBAVCODEC_INCLUDE_DIRS} ${LIBAVCODEC_INCLUDEDIR}/libavcodec)

set(prefix /usr/local
    CACHE PATH "Installation prefix")
set(bindir ${prefix}/bin
    CACHE PATH "Executable installation directory")
set(sharedir ${prefix}/share
    CACHE PATH "Shared data installation directory")
set(docdir ${sharedir}/doc
    CACHE PATH "Documentation installation directory")
set(mandir ${sharedir}/man
    CACHE PATH "Manual page installation directory")
# Some modules require this
set(CMAKE_INSTALL_PREFIX "${prefix}" CACHE INTERNAL "")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98 -Wall -Wextra")
add_definitions(-D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_POSIX_C_SOURCE)
include_directories(src)

# Suppress bogus warnings (GCC PR7302)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-non-virtual-dtor")

# Suppress deprecation warnings from libavcodec headers about themselves
if(LIBAVCODEC_VERSION MATCHES "^51\\.")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-deprecated-declarations")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
endif(LIBAVCODEC_VERSION MATCHES "^51\\.")

# Remove bogus -fPIC (Debian bug #478404)
set(CMAKE_SHARED_LIBRARY_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "")

add_subdirectory(data)
add_subdirectory(doc)
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(po)

install(FILES ChangeLog
        DESTINATION ${docdir}/dvswitch)
