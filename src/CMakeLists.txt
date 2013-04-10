include(Symlink)

add_definitions(${ALSA_CFLAGS_OTHER} ${GTKMM_CFLAGS_OTHER} ${LIBAVCODEC_CFLAGS_OTHER})
include_directories(${OSC_INCLUDE_DIRS} ${JACK_INCLUDE_DIRS} ${ALSA_INCLUDE_DIRS} ${GTKMM_INCLUDE_DIRS} ${LIBAVCODEC_INCLUDE_DIRS} ${LiveMedia_INCLUDE_DIRS} ${GETTEXT_INCLUDE_DIR})
link_directories(${OSC_LIBRARY_DIRS} ${JACK_LIBRARY_DIRS} ${ALSA_LIBRARY_DIRS} ${GTKMM_LIBRARY_DIRS} ${LIBAVCODEC_LIBRARY_DIRS})

add_definitions(-DSHAREDIR="\\"${sharedir}\\"")

set(common_sources config.c dif.c socket.c)

add_executable(dvsink-command dvsink-command.c ${common_sources})

add_executable(dvsink-files dvsink-files.c ${common_sources})

add_executable(dvsource-file dvsource-file.c frame_timer.c ${common_sources})
target_link_libraries(dvsource-file pthread rt)

add_executable(dvsource-dvgrab dvsource-dvgrab.c ${common_sources})

add_executable(dvsource-firewire-rtsp dvsource-firewire.cpp os_error.cpp
  DVVideoStreamFramer.cpp tally_rtsp_server.cpp ${common_sources})
target_link_libraries(dvsource-firewire-rtsp pthread raw1394
  ${LiveMedia_LIBRARIES})

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
add_executable(dvsource-alsa dvsource-alsa.c dif_audio.c ${common_sources})
target_link_libraries(dvsource-alsa m ${ALSA_LIBRARIES})
endif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

add_executable(dvsource-jack dvsource-jack.c dif_audio.c ${common_sources})
set_target_properties(dvsource-jack PROPERTIES COMPILE_FLAGS -DUSE_JACK)

target_link_libraries(dvsource-jack m pthread ${JACK_LIBRARIES})

add_executable(dvswitch dvswitch.cpp mixer.cpp frame_timer.c
  mixer_window.cpp dv_display_widget.cpp dv_selector_widget.cpp
  server.cpp auto_pipe.cpp os_error.cpp video_effect.c frame_pool.cpp
  frame.c auto_codec.cpp format_dialog.cpp dif_audio.c vu_meter.cpp
  status_overlay.cpp connector.cpp sources_dialog.cpp osc_ctrl.cpp ${common_sources})
target_link_libraries(dvswitch m pthread rt X11 Xext Xv
  ${BOOST_THREAD_LIBRARIES} ${GTKMM_LIBRARIES} ${LIBAVCODEC_LIBRARIES}
  ${LIBAVUTIL_LIBRARIES} ${LiveMedia_LIBRARIES} ${GETTEXT_LIBRARIES}
  ${OSC_LIBRARIES})

install(TARGETS dvsink-command dvsink-files dvsource-file dvsource-dvgrab
                dvswitch dvsource-firewire-rtsp dvsource-jack
                dvswitch dvsource-firewire-rtsp dvsource-jack
        DESTINATION ${bindir})

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
install(TARGETS dvsource-alsa DESTINATION ${bindir})
endif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
install_symlink(dvsource-dvgrab "${bindir}/dvsource-firewire")
install_symlink(dvsource-dvgrab "${bindir}/dvsource-v4l2-dv")
install_symlink(dvsource-dvgrab "${bindir}/dvsource-command")
