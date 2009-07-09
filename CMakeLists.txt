cmake_minimum_required(VERSION 2.4)
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0005 OLD)
endif(COMMAND cmake_policy)

project(DVSWITCH CXX C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
include(FindPkgConfig)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(GTKMM REQUIRED gtkmm-2.4)
pkg_check_modules(LIBAVCODEC REQUIRED libavcodec)

find_library(BOOST_THREAD_LIBRARIES
	     NAMES boost_thread-mt boost_thread
	     DOC "Boost.Thread library path"
	     NO_SYSTEM_ENVIRONMENT_PATH)
if(NOT BOOST_THREAD_LIBRARIES)
    message(FATAL_ERROR "Could not find Boost.Thread library")
endif(NOT BOOST_THREAD_LIBRARIES)

find_path(LIVEMEDIA_INCLUDEDIR
	  NAMES liveMedia/liveMedia.hh
	  DOC "liveMedia header installation directory"
	  NO_SYSTEM_ENVIRONMENT_PATH)
if(NOT LIVEMEDIA_INCLUDEDIR)
    message(FATAL_ERROR "Could not find liveMedia headers")
endif(NOT LIVEMEDIA_INCLUDEDIR)
set(LIVEMEDIA_INCLUDE_DIRS
    ${LIVEMEDIA_INCLUDEDIR}/BasicUsageEnvironment
    ${LIVEMEDIA_INCLUDEDIR}/groupsock
    ${LIVEMEDIA_INCLUDEDIR}/liveMedia
    ${LIVEMEDIA_INCLUDEDIR}/UsageEnvironment
    CACHE FILEPATH "liveMedia include paths")

foreach(library_name BasicUsageEnvironment groupsock liveMedia UsageEnvironment)
    # Forget previous library_path
    set(library_path "library_path-NOTFOUND" CACHE INTERNAL "")
    find_library(library_path
                 NAMES ${library_name}
		 NO_SYSTEM_ENVIRONMENT_PATH)
    if(NOT library_path)
        message(FATAL_ERROR "Could not find liveMedia libraries")
    endif(NOT library_path)
    set(LIVEMEDIA_LIBRARIES ${LIVEMEDIA_LIBRARIES} ${library_path})
endforeach(library_name)
set(LIVEMEDIA_LIBRARIES ${LIVEMEDIA_LIBRARIES}
    CACHE FILEPATH "liveMedia library paths")

# Cope with recent move of ffmpeg headers into subdirectories
set(LIBAVCODEC_INCLUDE_DIRS ${LIBAVCODEC_INCLUDE_DIRS} ${LIBAVCODEC_INCLUDEDIR}/libavcodec)

set(CMAKE_INSTALL_PREFIX "" CACHE INTERNAL "") # we don't use this
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

install(FILES ChangeLog
        DESTINATION ${docdir}/dvswitch)
