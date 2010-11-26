message(STATUS "checking for liveMedia")

set(LiveMedia_FOUND TRUE)

if(LiveMedia_BUILDDIR)
    find_path(LiveMedia_INCLUDEDIR
              NAMES liveMedia/include/liveMedia.hh
              PATHS ${LiveMedia_BUILDDIR}
              DOC "liveMedia header installation directory"
              NO_DEFAULT_PATH)
    if(LiveMedia_INCLUDEDIR)
        set(LiveMedia_INCLUDE_DIRS
            ${LiveMedia_INCLUDEDIR}/BasicUsageEnvironment/include
            ${LiveMedia_INCLUDEDIR}/groupsock/include
            ${LiveMedia_INCLUDEDIR}/liveMedia/include
            ${LiveMedia_INCLUDEDIR}/UsageEnvironment/include
            CACHE FILEPATH "liveMedia include paths")
    endif(LiveMedia_INCLUDEDIR)
else(LiveMedia_BUILDDIR)
    find_path(LiveMedia_INCLUDEDIR
              NAMES liveMedia/liveMedia.hh
              DOC "liveMedia header installation directory"
              NO_SYSTEM_ENVIRONMENT_PATH)
    if(LiveMedia_INCLUDEDIR)
        set(LiveMedia_INCLUDE_DIRS
            ${LiveMedia_INCLUDEDIR}/BasicUsageEnvironment
            ${LiveMedia_INCLUDEDIR}/groupsock
            ${LiveMedia_INCLUDEDIR}/liveMedia
            ${LiveMedia_INCLUDEDIR}/UsageEnvironment
            CACHE FILEPATH "liveMedia include paths")
    endif(LiveMedia_INCLUDEDIR)
endif(LiveMedia_BUILDDIR)
if(NOT LiveMedia_INCLUDEDIR)
    set(LiveMedia_FOUND FALSE)
    if(LiveMedia_FIND_REQUIRED)
        message(SEND_ERROR "liveMedia headers not found")
    endif(LiveMedia_FIND_REQUIRED)
endif(NOT LiveMedia_INCLUDEDIR)

foreach(library_name liveMedia groupsock UsageEnvironment BasicUsageEnvironment)
    # Forget previous library_path
    set(library_path "library_path-NOTFOUND" CACHE INTERNAL "")
    if(LiveMedia_BUILDDIR)
        find_library(library_path
                     PATHS ${LiveMedia_BUILDDIR}/${library_name}
                     NAMES ${library_name}
                     NO_DEFAULT_PATH)
    else(LiveMedia_BUILDDIR)
        find_library(library_path
                     NAMES ${library_name}
                     NO_SYSTEM_ENVIRONMENT_PATH)
    endif(LiveMedia_BUILDDIR)
    if(library_path)
        set(LiveMedia_LIBRARIES ${LiveMedia_LIBRARIES} ${library_path})
    else(library_path)
        set(LiveMedia_FOUND FALSE)
        if(LiveMedia_FIND_REQUIRED)
            message(SEND_ERROR "${library_name} library not found")
        endif(LiveMedia_FIND_REQUIRED)
    endif(library_path)
endforeach(library_name)
set(library_path "library_path-NOTFOUND" CACHE INTERNAL "")

if(LiveMedia_FOUND)
    message(STATUS "  found liveMedia")
    set(LiveMedia_LIBRARIES ${LiveMedia_LIBRARIES}
        CACHE FILEPATH "liveMedia library paths")
endif(LiveMedia_FOUND)
