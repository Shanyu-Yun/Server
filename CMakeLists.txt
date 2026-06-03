cmake_minimum_required(VERSION 3.10)

project(tinynet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 公共库 ──────────────────────────────────────────────────────────────
add_library(tinynet STATIC
  src/base/timestamp.cpp
  src/base/logger.cpp
  src/base/buffer.cpp
  src/base/currentthread.cpp
  src/base/thread.cpp
  src/net/inetaddress.cpp
  src/net/socket.cpp
  src/net/channel.cpp
  src/net/poller.cpp
  src/net/epollpoller.cpp
  src/net/eventloop.cpp
  src/net/acceptor.cpp
  src/net/tcpconnection.cpp
  src/net/tcpserver.cpp
  src/net/eventloopthread.cpp
  src/net/eventloopthreadpool.cpp
  src/net/timer.cpp
  src/net/timerqueue.cpp
  src/net/udpserver.cpp
  src/net/timingwheel.cpp
)

target_include_directories(tinynet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── 主程序 ──────────────────────────────────────────────────────────────
add_executable(tinynet_demo src/main.cpp)
target_link_libraries(tinynet_demo PRIVATE tinynet)

# ── 测试 ────────────────────────────────────────────────────────────────
add_executable(timer_test test/timer_test.cpp)
target_link_libraries(timer_test PRIVATE tinynet)

add_executable(udp_echo_test test/udp_echo_test.cpp)
target_link_libraries(udp_echo_test PRIVATE tinynet)
