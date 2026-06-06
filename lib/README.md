# backend/ — AI Agent 网络后端（应用 · 项目重心）

与 `modules/`（tinynet 库）平级的顶层 realm，物理上画死接缝：
**`字节/帧/状态码/连接` → `modules/` 库；`token/工具/消息历史/转发/prompt` → `backend/` 应用。**

后端的本职是网络后端的活：**接入 → 转发 → 会话管理 → 流式编排**。AI 的"聪明"是它转发的对象，不是它要自证的智能。

## 子模块（核心主干）

| 目录 | 组件 | 职责 |
|---|---|---|
| `server/` | `AgentServer` | **接入 + 流式编排**：HTTP/SSE/WS 路由、token 逐字回推、连接生命周期 |
| `llm/` | `LlmClient` | **转发**：OpenAI 兼容适配（云端为主 + 本地 llama.cpp 后备），内部用库的 `HttpClient`/阻塞桥 |
| `session/` | `Session` | **会话管理**：消息历史、上下文窗口截断/摘要、多会话隔离 |

## `ext/`（进阶 · 后端主干跑通后才叠加）

让后端从"纯转发"升级成"会自己调工具的 agent"，不是第一阶段重心：

- `ext/tools/` — `ToolRegistry` + `AgentLoop`（ReAct 控制环）
- `ext/rag/` — embedding + hnswlib，封成 `rag_search` 工具
- `ext/asr/` — 语音输入适配（可选）

> 各子模块沿用 `X/{include/X, src}` 两段式、只暴露自己的 include 根
> （`backend/llm/include` → `#include "llm/llmclient.hpp"`）。建目录沿用
> 「做到哪个模块才建哪个」——ext 子模块用到时再落 include/src。

详见 `docs/应用层-Agent-路线图-v2.md`。
