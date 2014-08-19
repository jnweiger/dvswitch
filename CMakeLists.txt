cmake_minimum_required(VERSION 2.4)
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
    cmake_policy(SET CMP0005 OLD)
endif(COMMAND cmake_policy)

project(DVSWITCH CXX C)
set(PACKAGE_VERSION 0.9.2)

enable_testing()

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

# Recent versions of Boost.Thread require Boost.System
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES ${BOOST_THREAD_LIBRARIES})
check_cxx_source_compiles("#include <boost/thread.hpp>
    void f() {}
    int main() { boost::thread thread(f); thread.join(); }"
    BOOST_THREAD_WORKS_WITHOUT_SYSTEM)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT BOOST_THREAD_WORKS_WITHOUT_SYSTEM)
    message(STATUS "checking for Boost.System")
    find_library(BOOST_SYSTEM_LIBRARIES
        NAMES boost_system-mt boost_system
        DOC "Boost.System library path"
        NO_SYSTEM_ENVIRONMENT_PATH)
    if(NOT BOOST_SYSTEM_LIBRARIES)
        message(FATAL_ERROR "Boost.System library not found")
    endif(NOT BOOST_SYSTEM_LIBRARIES)
    message(STATUS "  found Boost.System")
endif(NOT BOOST_THREAD_WORKS_WITHOUT_SYSTEM)

# Check if we have av_frame_alloc()
include(CheckSymbolExists)
CHECK_SYMBOL_EXISTS("av_frame_alloc" "<libavcodec/avcodec.h>" AVC_HAVE_FRAME_ALLOC)

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
