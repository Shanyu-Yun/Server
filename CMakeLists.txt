cmake_minimum_required(VERSION 3.10)

project(muduo_learn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(muduo_learn INTERFACE)

target_include_directories(muduo_learn
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

file(GLOB_RECURSE MUDUO_SOURCES CONFIGURE_DEPENDS
  acceptor.cpp
  channel.cpp
  curthread.cpp
  epollpoller.cpp
  eventloop.cpp
  inetaddress.cpp
  logger.cpp
  main.cpp
  poller.cpp
  socket.cpp
  timestamp.cpp
  buffer.cpp
)

add_executable(muduo ${MUDUO_SOURCES})
target_link_libraries(muduo PRIVATE muduo_learn)
