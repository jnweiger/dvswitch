message(STATUS "checking for liveMedia")

set(LiveMedia_FOUND TRUE)

find_path(LiveMedia_INCLUDEDIR
	  NAMES liveMedia/liveMedia.hh
	  DOC "liveMedia header installation directory"
	  NO_SYSTEM_ENVIRONMENT_PATH)
if(NOT LiveMedia_INCLUDEDIR)
    set(LiveMedia_FOUND FALSE)
    if(LiveMedia_FIND_REQUIRED)
        message(SEND_ERROR "liveMedia headers not found")
    endif(LiveMedia_FIND_REQUIRED)
endif(NOT LiveMedia_INCLUDEDIR)

foreach(library_name BasicUsageEnvironment groupsock liveMedia UsageEnvironment)
    # Forget previous library_path
    set(library_path "library_path-NOTFOUND" CACHE INTERNAL "")
    find_library(library_path
                 NAMES ${library_name}
		 NO_SYSTEM_ENVIRONMENT_PATH)
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
    set(LiveMedia_INCLUDE_DIRS
        ${LiveMedia_INCLUDEDIR}/BasicUsageEnvironment
        ${LiveMedia_INCLUDEDIR}/groupsock
        ${LiveMedia_INCLUDEDIR}/liveMedia
        ${LiveMedia_INCLUDEDIR}/UsageEnvironment
        CACHE FILEPATH "liveMedia include paths")
    set(LiveMedia_LIBRARIES ${LiveMedia_LIBRARIES}
        CACHE FILEPATH "liveMedia library paths")
endif(LiveMedia_FOUND)
