cmake_minimum_required( VERSION 3.8.0 FATAL_ERROR)

project( kvstore_testbed
  VERSION 0.0.1
  DESCRIPTION "testing redis and epoll based server")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_library(LIBRT rt REQUIRED)
find_library(LIBM m REQUIRED)

set(WARNING_FLAGS "-Wpedantic -Wconversion -Wall -Wno-unused-variable -Wno-unused-function")
set(MEMORY_LEAK_CHECK_FLAGS "-fsanitize=address -fno-omit-frame-pointer") #-fno-common")
#we can use gnu11 not c11 so POSIX and other extensions to standard C are visible by default
#ref: https://stackoverflow.com/questions/3875197/gcc-with-std-c99-complains-about-not-knowing-struct-timespec
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -std=gnu11 -pthread -g -lrt")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -std=c11 -pthread -g -lrt")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNING_FLAGS}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -std=c++11 -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS}")

#set(SERVER_SRCS "tcp_epollserver.c" "timestamping.c")
add_library(cpp_dist SHARED "cpp_dist.cc" "dist_gen.cc")
set_target_properties(cpp_dist PROPERTIES CXX_STANDARD 11)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MEMORY_LEAK_CHECK_FLAGS}")
add_executable(tcp_server tcp_epollserver.c)
set_target_properties(tcp_server PROPERTIES C_STANDARD 11)

add_executable(udp_server udp_epollserver.c)
set_target_properties(udp_server PROPERTIES C_STANDARD 11)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MEMORY_LEAK_CHECK_FLAGS}")
add_executable(tcp_client tcp_mtclient.c)
set_target_properties(tcp_client PROPERTIES C_STANDARD 11)
target_link_libraries(tcp_client PRIVATE cpp_dist )
target_link_libraries(tcp_client PRIVATE m)

add_executable(udp_client udp_mtclient.c)
set_target_properties(udp_client PROPERTIES C_STANDARD 11)
target_link_libraries(udp_client PRIVATE cpp_dist )
target_link_libraries(udp_client PRIVATE m)

add_executable(timestamp_test timestamp_test.c timestamping.c)
add_executable(udp_timestamped_server udp_timestamped_server.c timestamping.c)
set_target_properties(udp_timestamped_server PROPERTIES C_STANDARD 11)

add_executable(feedback_test timestmap_feedback_test.c timestamping.c)

add_executable(multidest_test multidest_test.c multi_dest_protocol.c) 
add_executable(timerwheel_client timerwheel_closedloop_client.c multi_dest_protocol.c)
add_executable(timerwheel_openloop_client timerwheel_openloop_client.c multi_dest_protocol.c)
target_link_libraries(timerwheel_openloop_client PRIVATE cpp_dist)
target_link_libraries(timerwheel_openloop_client PRIVATE m)

add_executable(timerwheel_baseline_client timerwheel_baseline_client.c multi_dest_protocol.c)
target_link_libraries(timerwheel_baseline_client PRIVATE cpp_dist)
target_link_libraries(timerwheel_baseline_client PRIVATE m)

add_executable(timerwheel_server timerwheel_server.c)
target_link_libraries(timerwheel_server PRIVATE cpp_dist)
target_link_libraries(timerwheel_server PRIVATE m)
#add_executable(timerwheel_roundrobin_server timerwheel_roundrobin_server.c)

add_executable(dist_test dist_test.c)
target_link_libraries(dist_test PRIVATE cpp_dist )
target_link_libraries(dist_test PRIVATE m)
set_target_properties(dist_test PROPERTIES C_STANDARD 11)


