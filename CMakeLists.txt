cmake_minimum_required(VERSION 3.16)

project(tinynet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ══════════════════════════════════════════════════════════════════════════════
# 目录约定
#   lib/
#     transport/          ← 库1：TCP/UDP 底层连接
#       base/{public,private}/
#       net/{public,private}/
#     protocol/           ← 库2：HTTP · WebSocket · (未来 TLS/SSE)
#       http/{public,private}/
#       ws/{public,private}/
#     agent/              ← 库3：AI Agent 后端
#       llm/{public,private}/
#       session/{public,private}/
#       server/{public,private}/
#
#   public/  = 对外头文件；include 根直接设为该目录，#include "xxx.hpp" 无前缀
#   private/ = 实现 .cpp 与模块内部私有头，仅本库编译可见
#
#   依赖方向（单向）：agent → protocol → transport
# ══════════════════════════════════════════════════════════════════════════════

# helper：收集一个库下所有子模块的 private/*.cpp
# 用法：glob_lib_sources(VARNAME lib/<library>)
function(glob_lib_sources OUT_VAR LIB_DIR)
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS "${LIB_DIR}/*/private/*.cpp")
  set(${OUT_VAR} ${srcs} PARENT_SCOPE)
endfunction()

# helper：将一个库下所有子模块的 public/ 设为 target 的 include 目录
function(add_lib_includes TARGET_NAME VISIBILITY LIB_DIR)
  file(GLOB subdirs LIST_DIRECTORIES true "${LIB_DIR}/*")
  foreach(sub ${subdirs})
    if(IS_DIRECTORY "${sub}/public")
      target_include_directories(${TARGET_NAME} ${VISIBILITY} "${sub}/public")
    endif()
    if(IS_DIRECTORY "${sub}/private")
      target_include_directories(${TARGET_NAME} PRIVATE "${sub}/private")
    endif()
  endforeach()
endfunction()

# ── 库1：transport ───────────────────────────────────────────────────────────
#   base（Buffer · Logger · Thread · Timestamp）
#   net （EventLoop · TcpServer · TcpConnection · UdpServer · TimingWheel…）
glob_lib_sources(TRANSPORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/transport)
add_library(transport STATIC ${TRANSPORT_SOURCES})
add_lib_includes(transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/transport)

# ── 库2：protocol ────────────────────────────────────────────────────────────
#   http（HttpServer · HttpRequest · HttpResponse · HttpContext）
#   ws  （WsServer · WsConnection · WsFrame · WsContext）
#   未来：tls/  sse/  等子模块按相同结构追加，cmake 自动感知。
glob_lib_sources(PROTOCOL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/protocol)
if(PROTOCOL_SOURCES)
  add_library(protocol STATIC ${PROTOCOL_SOURCES})
  add_lib_includes(protocol PUBLIC   ${CMAKE_CURRENT_SOURCE_DIR}/lib/protocol)
  target_link_libraries(protocol PUBLIC transport)
endif()

# ── 库3：agent ───────────────────────────────────────────────────────────────
#   llm    （LlmClient：OpenAI 兼容，流式转发）
#   session（消息历史 · 窗口截断/摘要 · 多会话隔离）
#   server （AgentServer · 路由 · SSE/WS 推送）
#   未来：tools/  rag/  asr/  等子模块按相同结构追加。

# third_party：vendored 单头 JSON（只有 agent 依赖，其它库保持零三方）
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/include)

glob_lib_sources(AGENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/agent)
if(AGENT_SOURCES)
  add_library(agent STATIC ${AGENT_SOURCES})
  add_lib_includes(agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/agent)
  target_link_libraries(agent PUBLIC nlohmann_json)
  if(TARGET protocol)
    target_link_libraries(agent PUBLIC protocol)
  else()
    target_link_libraries(agent PUBLIC transport)
  endif()
endif()

# ── 主程序 ───────────────────────────────────────────────────────────────────
add_executable(tinynet_demo app/main.cpp)
target_link_libraries(tinynet_demo PRIVATE transport)

# ── 测试：test/ 下每个 .cpp 自动编成一个可执行 ──────────────────────────────
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp)
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE transport)
  if(TARGET protocol)
    target_link_libraries(${test_name} PRIVATE protocol)
  endif()
  if(TARGET agent)
    target_link_libraries(${test_name} PRIVATE agent)
  endif()
endforeach()
