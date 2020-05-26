cmake_minimum_required( VERSION 3.12 FATAL_ERROR)

project( kvstore_testbed
  VERSION 0.0.1
  DESCRIPTION "testing redis and epoll based server")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_library(LIBRT rt REQUIRED)
find_library(LIBM m REQUIRED)
