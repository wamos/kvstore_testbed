cmake_minimum_required( VERSION 3.8.0 FATAL_ERROR)

project( kvstore_testbed
  VERSION 0.0.1
  DESCRIPTION "testing redis and epoll based server")

#set(THREADS_PREFER_PTHREAD_FLAG ON)
#find_package(Threads REQUIRED)
#find_library(LIBRT rt REQUIRED)
#find_library(LIBM m REQUIRED)

set(WARNING_FLAGS "-Wpedantic -Wconversion -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -std=c11 -pthread -g -lrt")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNING_FLAGS}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -std=c++11 -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS}")

#set(SERVER_SRCS "tcp_epollserver.c" "timestamping.c")

#add_executable(server tcp_epollserver.c)
#set_target_properties(server PROPERTIES C_STANDARD 11)

add_library(cpp_rand SHARED "cpp_dist.cc" "dist_gen.cc")
set_target_properties(cpp_rand PROPERTIES CXX_STANDARD 11)

add_executable( dist_test dist_test.c)
target_link_libraries( dist_test PRIVATE cpp_rand )
set_target_properties(dist_test PROPERTIES C_STANDARD 11)


