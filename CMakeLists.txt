cmake_minimum_required(VERSION 3.16)

project(tinynet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── net 库：传输层（base + net 两模块编进同一库）────────────────────────────
# CONFIGURE_DEPENDS：新增/删除 src/*.cpp 时，下次 build 自动重扫，无需手动跑 cmake。
file(GLOB NET_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/base/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/src/*.cpp
)
add_library(net STATIC ${NET_SOURCES})
# 每个模块只暴露自己的 include 根，#include "base/..." / "net/..." 前缀照旧。
target_include_directories(net PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/base/include
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/include
)

# ── http 库：应用层（依赖 net，单向）────────────────────────────────────────
# modules/http/src/ 为空时不定义此库，构建照常；放入第一个 .cpp 后自动生成。
file(GLOB HTTP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/modules/http/src/*.cpp)
if(HTTP_SOURCES)
  add_library(http STATIC ${HTTP_SOURCES})
  # http 只暴露自己的 include 根；net 的头经 PUBLIC link 传递进来。
  target_include_directories(http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/modules/http/include)
  # PUBLIC：依赖方向钉死 http → net，net 看不见 http 的头。
  target_link_libraries(http PUBLIC net)
endif()

# ── 主程序 ──────────────────────────────────────────────────────────────
add_executable(tinynet_demo app/main.cpp)
target_link_libraries(tinynet_demo PRIVATE net)

# ── 测试：test/ 下每个 .cpp 自动注册成一个可执行，统一链接 net（http 存在则一并链）──
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp)
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE net)
  if(TARGET http)
    target_link_libraries(${test_name} PRIVATE http)
  endif()
endforeach()
