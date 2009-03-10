add_definitions(${LIBAVCODEC_CFLAGS_OTHER})
include_directories(${LIBAVCODEC_INCLUDE_DIRS})
link_directories(${LIBAVCODEC_LIBRARY_DIRS})

add_executable(mixer mixer.cpp ../src/mixer.cpp ../src/frame_timer.c
  ../src/dif.c ../src/dif_audio.c ../src/frame_pool.cpp ../src/auto_codec.cpp
  ../src/frame.c ../src/os_error.cpp ../src/video_effect.c)
target_link_libraries(mixer pthread rt ${BOOST_THREAD_LIBRARIES}
                      ${LIBAVCODEC_LIBRARIES})

add_executable(ring_buffer ring_buffer.cpp)

add_executable(pic_in_pic pic_in_pic.cpp ../src/video_effect.c)
target_link_libraries(pic_in_pic ${LIBAVCODEC_LIBRARIES})

add_executable(pic_in_pic_speed pic_in_pic.cpp ../src/video_effect.c)
set_target_properties(pic_in_pic_speed
                      PROPERTIES COMPILE_FLAGS -DTEST_SPEED)
target_link_libraries(pic_in_pic_speed ${LIBAVCODEC_LIBRARIES})
