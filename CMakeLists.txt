cmake_minimum_required(VERSION 3.10)

project(muduo_learn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 公共库 ──────────────────────────────────────────────────────────────
add_library(muduo_core STATIC
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
)

target_include_directories(muduo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── 主程序 ──────────────────────────────────────────────────────────────
add_executable(muduo src/main.cpp)
target_link_libraries(muduo PRIVATE muduo_core)

# ── 基准测试 ─────────────────────────────────────────────────────────────
add_executable(benchmark benchmark/benchmark.cpp)
target_link_libraries(benchmark PRIVATE muduo_core)
