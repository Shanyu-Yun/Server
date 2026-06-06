cmake_minimum_required(VERSION 3.16)

project(tinynet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 布局约定：lib/<模块>/{public,private} ─────────────────────────────────────
#   public/  对外头文件（其它模块/可执行可见，include 根，#include "xxx.hpp" 无前缀）
#   private/ 实现 .cpp 与模块内部私有头（仅本模块编译时可见）
# 各模块只把自己的 public/ 暴露为 PUBLIC include 目录，依赖经 link 单向传递。

# ── net 库：传输层（base + net 两模块编进同一库）────────────────────────────
# CONFIGURE_DEPENDS：新增/删除 private/*.cpp 时，下次 build 自动重扫，无需手动跑 cmake。
file(GLOB NET_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/base/private/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/net/private/*.cpp
)
add_library(net STATIC ${NET_SOURCES})
target_include_directories(net
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/base/public
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/net/public
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/base/private
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/net/private
)

# ── http 库：应用层（依赖 net，单向）────────────────────────────────────────
# lib/http/private/ 为空时不定义此库，构建照常；放入第一个 .cpp 后自动生成。
file(GLOB HTTP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/http/private/*.cpp)
if(HTTP_SOURCES)
  add_library(http STATIC ${HTTP_SOURCES})
  target_include_directories(http
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/lib/http/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/http/private
  )
  # PUBLIC：依赖方向钉死 http → net，net 看不见 http 的头。
  target_link_libraries(http PUBLIC net)
endif()

# ── ws 库：WebSocket 层（依赖 http，单向）───────────────────────────────────
file(GLOB WS_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/ws/private/*.cpp)
if(WS_SOURCES)
  add_library(ws STATIC ${WS_SOURCES})
  target_include_directories(ws
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/lib/ws/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/ws/private
  )
  target_link_libraries(ws PUBLIC http)
endif()

# ══ lib/{llm,session,server} — AI Agent 网络后端（应用 · 重心）═══════════════
# 与传输/协议库平级的应用模块。库见字节/帧/连接，这些见 token/转发/会话。

# third_party：vendored 单头 JSON 库（只有后端依赖，库保持零三方）。
# json.hpp 用到 B1 LlmClient 时再下载进 third_party/nlohmann/include/nlohmann/。
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/include)

# llm — 转发：LlmClient（OpenAI 兼容，先阻塞桥走 net，A4 后换异步 https）。
file(GLOB LLM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/llm/private/*.cpp)
if(LLM_SOURCES)
  add_library(llm STATIC ${LLM_SOURCES})
  target_include_directories(llm
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/lib/llm/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/llm/private
  )
  target_link_libraries(llm PUBLIC net nlohmann_json)
endif()

# session — 会话管理：消息历史 · 窗口截断/摘要 · 多会话隔离。
file(GLOB SESSION_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/session/private/*.cpp)
if(SESSION_SOURCES)
  add_library(session STATIC ${SESSION_SOURCES})
  target_include_directories(session
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/lib/session/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/session/private
  )
  target_link_libraries(session PUBLIC net nlohmann_json)
endif()

# server — 接入 + 流式编排：AgentServer + 路由 + SSE/WS 推送。
file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/server/private/*.cpp)
if(SERVER_SOURCES)
  add_library(agentserver STATIC ${SERVER_SOURCES})
  target_include_directories(agentserver
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/lib/server/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/server/private
  )
  target_link_libraries(agentserver PUBLIC ws)
  if(TARGET llm)
    target_link_libraries(agentserver PUBLIC llm)
  endif()
  if(TARGET session)
    target_link_libraries(agentserver PUBLIC session)
  endif()
endif()
# lib/ext/{tools,rag,asr} 进阶子模块：用到时照此追加 guarded 块。

# ── 主程序 ──────────────────────────────────────────────────────────────
add_executable(tinynet_demo app/main.cpp)
target_link_libraries(tinynet_demo PRIVATE net)

# ── 测试：test/ 下每个 .cpp 自动注册成一个可执行，统一链接 net（http/ws 存在则一并链）──
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp)
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE net)
  if(TARGET http)
    target_link_libraries(${test_name} PRIVATE http)
  endif()
  if(TARGET ws)
    target_link_libraries(${test_name} PRIVATE ws)
  endif()
endforeach()
