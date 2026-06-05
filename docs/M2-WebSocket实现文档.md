# M2 · WebSocket 实现路线图

> 前置：M1 已完成（HTTP/1.1 codec + SSE + 经 localhost HTTP 调 llama-server）。
> 本文档规划 WebSocket（RFC 6455）的应用层实现，只记录新增与改动，HTTP 基础部分不重复。
> 纪律不变：**只做够用的子集，不追 RFC 完整性**，每节末尾标"现在不做"。

---

## 为什么 WebSocket 的握手是 HTTP，之后却不是？

WebSocket 的设计是**借 HTTP 的壳完成握手，然后把同一条 TCP 连接升级成全双工的帧通道**。这带来一个关键事实：**握手阶段完全复用你已有的 HTTP codec**，升级之后才需要全新的帧解析器。

一次完整的生命周期：

```
① 客户端发一个特殊的 HTTP GET 请求（带 Upgrade 头）
        ↓  ← 这一步 HttpContext 原样能解析
② 服务端校验头，回 101 Switching Protocols
        ↓  ← 从此这条 TCP 连接不再说 HTTP
③ 双方用 WebSocket 帧协议互发消息（全双工，无请求-响应约束）
        ↓
④ 任一方发 Close 帧，挥手关闭
```

所以 SSE 和 WebSocket 的本质区别要分清：**SSE 是单向的（服务端→客户端），仍是一个没完没了的 HTTP 响应；WebSocket 是双向的，握手后脱离 HTTP 语义**。语音场景需要双向——浏览器持续上传录音、服务端持续下发转写——这正是必须上 WebSocket 而非 SSE 的原因。

---

## 类分解与职责

| 类 | 职责 | 性质 |
|---|---|---|
| `WsContext` | **每连接**的帧解析状态机，跨多次 onMessage 攒一个完整帧/消息 | 核心难点 |
| `WsFrame` | 一帧的解析结果：opcode、fin、payload | 被动数据 |
| `WsConnection` | 包在 `TcpConnection` 上，对用户暴露 `send(text)` / `sendBinary` / `close`，内部负责帧编码 | 胶水 + 编码 |
| `WsServer` | 包装 `HttpServer`，拦截 Upgrade 请求做握手，升级后把字节流交给 `WsContext` | 胶水 |

用户最终对接的回调（消息级，不是字节级）：

```cpp
using WsMessageCallback =
    std::function<void(const WsConnectionPtr&, std::string_view, bool isBinary)>;
```

---

## 握手：复用 HTTP codec

### 前置改动 —— 新增 SHA1 与 base64

握手响应要算 `Sec-WebSocket-Accept`，依赖 **SHA1** 和 **base64**，当前 `base/` 里都没有。这两个是协议无关的纯算法，按第 0 节哲学下沉到 `base/`：

```cpp
// base/sha1.hpp —— 输入任意字节，输出 20 字节摘要
std::array<uint8_t, 20> sha1(const std::string& data);

// base/base64.hpp
std::string base64Encode(const uint8_t* data, size_t len);
```

SHA1 找一份公有领域实现贴进来即可（约 100 行），base64 编码自己写也就 20 行。**不要为此引入 OpenSSL**——TLS 才值得上 OpenSSL（M4），握手这点哈希不该拉整个库。

### 握手要校验和计算什么

客户端的 Upgrade 请求长这样（被 `HttpContext` 解析成一个普通 `HttpRequest`）：

```http
GET /chat HTTP/1.1
Host: 127.0.0.1:8080
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

服务端用已有的 `req.getHeader(...)` 校验（注意你的 `getHeader` 已小写化，查 `"upgrade"` 而非 `"Upgrade"`）：

```
req.getHeader("upgrade")            含 "websocket"（大小写不敏感）
req.getHeader("connection")         含 "upgrade"
req.getHeader("sec-websocket-key")  非空
req.getHeader("sec-websocket-version") == "13"
```

`Sec-WebSocket-Accept` 的算法是固定的——把客户端的 key 拼上一个魔法 GUID，SHA1 后 base64：

```cpp
static constexpr char kWsMagic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string acceptKey(const std::string& clientKey) {
  std::string s = clientKey + kWsMagic;
  auto digest = sha1(s);
  return base64Encode(digest.data(), digest.size());
}
```

### 101 响应

握手成功回这个，注意是 `101` 且带回算出的 accept：

```http
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: <acceptKey 的结果>

```

直接 `conn->send(...)` 这段固定文本即可，不需要走 `HttpResponse`（它会自动补 Content-Length，而 101 不该有 body）。

### 关键：握手后切换 context_

M1 里连接的 `context_` 装的是 `HttpContext`。握手成功后，**这条连接不再说 HTTP，要把 context_ 换成 `WsContext`**：

```cpp
// WsServer 检测到合法 Upgrade 请求时
conn->send(handshakeResponse);          // 101
conn->setContext(WsContext{});          // ★ 从 HttpContext 切到 WsContext
```

从此 `onMessage` 里取出的是 `WsContext`，喂给帧解析器而不是 HTTP 解析器。这个切换是 HTTP→WS 生命周期转换的核心动作。

**现在不做**：`Sec-WebSocket-Protocol`（子协议协商）、`Sec-WebSocket-Extensions`（如 permessage-deflate 压缩）——握手时直接忽略这两个头。

---

## 帧格式

升级之后，字节流不再是行文本，而是二进制帧。一帧的位布局（RFC 6455 §5.2）：

```
 0               1               2               3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| op    |M| Payload len |  Extended payload length      |
|I|S|S|S| code  |A|    (7)      |     (16 或 64 位，按需)        |
|N|V|V|V| (4)   |S|             |                               |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
|         Masking-key (4 字节，仅当 MASK=1)                     |
+---------------------------------------------------------------+
|                      Payload Data                             |
+---------------------------------------------------------------+
```

逐字段拆解：

- **FIN（1 bit）**：是不是消息的最后一帧（分片用）。
- **opcode（4 bit）**：帧类型。`0x0` 续帧、`0x1` 文本、`0x2` 二进制、`0x8` 关闭、`0x9` ping、`0xA` pong。
- **MASK（1 bit）**：payload 是否被掩码。**客户端→服务端的帧必须置 1；服务端→客户端必须置 0**——这是 RFC 强制，方向不同规则相反。
- **Payload len（7 bit）**：`0~125` 就是长度本身；`126` 表示真实长度在后续 **2 字节**；`127` 表示在后续 **8 字节**（大端）。
- **Masking-key（4 字节）**：仅 MASK=1 时存在，用于还原 payload。
- **Payload**：真实数据。

**掩码还原**很简单，逐字节异或循环的 key：

```cpp
for (size_t i = 0; i < payloadLen; ++i)
  payload[i] ^= maskKey[i % 4];
```

---

## 帧解析状态机

WebSocket 帧是**长度前缀**协议（不像 HTTP 那样靠 `\r\n` 分隔），所以解析比 HTTP 反而更直接：先读够帧头算出总长，再判断 Buffer 里够不够一整帧。`findCRLF` 在这里完全用不上，要回到 `peek` + `readableBytes` + `consume` 的字节级操作。

核心是一个"试着解析一帧"的函数，被循环调用（同一次 onMessage 可能来多帧）：

```
tryParseFrame(buf) -> 是否解出了一个完整帧:
    if buf.readableBytes() < 2:        return false        // 连帧头都不够
    读前 2 字节 → fin, opcode, masked, len7

    headerLen = 2
    if len7 == 126:  headerLen += 2     // 扩展长度 16 位
    elif len7 == 127: headerLen += 8    // 扩展长度 64 位
    if masked:        headerLen += 4    // 掩码 key

    if buf.readableBytes() < headerLen:  return false       // 帧头没到齐

    解析出 payloadLen（来自 len7 / 扩展字段）
    if payloadLen > kMaxFrameSize:  → 协议错，关连接（DoS 防御）

    total = headerLen + payloadLen
    if buf.readableBytes() < total:  return false           // payload 没到齐，等下次

    取出 payload，若 masked 则用 maskKey 还原
    buf.consume(total)                                       // 整帧消费
    交给 onFrame(opcode, fin, payload)
    return true
```

**和 HTTP 一样的纪律**：不够一整帧就 `return false`，一个字节都不 `consume`，剩余留在 Buffer 等下次 onMessage——这是帧级增量解析能跨 TCP 段成立的根本。

---

## 分片与控制帧 —— 三个必须处理的细节

**① 分片重组（数据帧）**。一条逻辑消息可以拆成多帧：首帧 opcode 是 `0x1`/`0x2` 且 FIN=0，后续是 `0x0`（续帧），最后一帧 FIN=1。`WsContext` 要把分片的 payload 攒进一个缓冲，直到 FIN=1 才把整条消息交给用户回调。必须配 **重组上限**（如 16MB），否则恶意客户端用无尽续帧吃爆内存。

**② 控制帧可以插在分片中间**。`ping`/`pong`/`close`（opcode ≥ 0x8）不受分片约束，可以出现在一条分片消息的两帧之间，且**控制帧自身不能被分片**、payload ≤ 125 字节。解析时控制帧要单独处理，不能混进数据重组缓冲。

**③ ping/pong/close 的自动响应**：

```
收到 ping(0x9)   → 立刻回一个 pong(0xA)，payload 原样带回
收到 pong(0xA)   → 心跳确认，通常无需动作
收到 close(0x8)  → 回一个 close 帧，然后 conn->shutdown()
```

这三种控制帧由 `WsConnection` 框架层自动处理，**不暴露给用户回调**——用户只关心文本/二进制消息。

---

## `WsConnection` 接口

包在 `TcpConnectionPtr` 上，对用户提供消息级发送（内部负责帧编码，服务端发出的帧 MASK=0、不掩码）：

```cpp
class WsConnection {
 public:
  void send(std::string_view text);        // 编码成 opcode=0x1 文本帧
  void sendBinary(const void* data, size_t len);  // opcode=0x2
  void close(uint16_t code = 1000);         // 发 close 帧并关连接
  const net::TcpConnectionPtr& tcp() const; // 需要底层连接时
 private:
  net::TcpConnectionPtr conn_;
};
```

发送侧的帧编码是解析的逆过程：拼帧头（FIN=1、opcode、按 payload 长度选 7/16/64 位长度字段、MASK=0），接 payload，整个 `conn_->send`。

---

## `onMessage` 控制流 —— 升级前后分流

`WsServer` 复用 `HttpServer`，关键在 `onMessage` 里根据 `context_` 当前装的是 `HttpContext` 还是 `WsContext` 分流：

```
onMessage(conn, buf, t):
    if context_ 里是 HttpContext:          // 还没升级
        走 HTTP 解析（M1 原逻辑）
        if 解出的请求是合法 Upgrade:
            发 101，conn->setContext(WsContext{})   // 切换
        else:
            当普通 HTTP 请求处理（普通页面/接口照常工作）
    else:                                  // 已是 WsContext
        while tryParseFrame(buf):          // 循环解帧（可能多帧粘连）
            onFrame(...)                   // 控制帧自动处理；数据帧重组后回调用户
```

好处：**同一个端口同时支持普通 HTTP 和 WebSocket**——浏览器先 GET 拿到页面（HTTP），页面里的 JS 再 `new WebSocket(...)` 升级，两者复用一套服务器。

---

## 易错点清单

| 坑 | 说明 |
|---|---|
| **服务端发帧不能掩码** | 客户端→服务端必须掩码，服务端→客户端必须不掩码，方向相反，搞反浏览器直接断开 |
| **大端字节序** | 扩展长度字段（16/64 位）是网络字节序（大端），别按本机序读 |
| **控制帧穿插** | ping/close 可能夹在数据分片中间，重组逻辑要先判 opcode 再决定是否进重组缓冲 |
| **header 小写查** | 复用 HttpRequest 的 `getHeader`，记得查 `"sec-websocket-key"` 全小写 |
| **101 不带 body** | 不要走 HttpResponse（会自动补 Content-Length），直接 send 固定文本 |
| **重组无上限** | 分片消息和续帧都要设 size 上限，否则是 DoS 面 |

---

## 现在不做（范围纪律）

| 不做 | 原因 / 推迟 |
|---|---|
| `permessage-deflate` 压缩扩展 | 协商 + 解压子状态机，收益对本场景小 |
| 子协议协商 `Sec-WebSocket-Protocol` | 用到再说 |
| 客户端侧（发起 WS 连接、要掩码） | 服务端用不到；M4 出站才可能需要 |
| 完整 close code 语义 | 简单版只发 1000（正常关闭）/ 1002（协议错） |
| 严格 UTF-8 校验文本帧 | 务实子集，不做编码合法性强校验 |

---

## 验收标准

- [ ] 浏览器 `new WebSocket("ws://127.0.0.1:8080/ws")`，`onopen` 触发（握手成功）
- [ ] 浏览器 `ws.send("hello")`，服务端回调收到完整文本
- [ ] 服务端 `wsConn.send(...)`，浏览器 `onmessage` 收到
- [ ] 发一条 >64KB 的大消息（触发 16/64 位扩展长度路径），正确收发
- [ ] 拆成多帧的分片消息能正确重组
- [ ] `ping` 自动回 `pong`；浏览器关页面时服务端收到 `close` 并清理
- [ ] 命令行 `wscat -c ws://127.0.0.1:8080/ws` 可交互
- [ ] 普通 HTTP 请求（M1 的 `/`、`/chat`）在同端口仍正常

---

## M2 终点 · 接 whisper.cpp

帧通道通了之后，语音闭环就是把帧内容接上 whisper.cpp：

```
浏览器 getUserMedia 录音 → 编码成音频块 → ws.send(binary)
        ↓
服务端 onMessage 收到二进制帧 → 攒够一段 → 喂 whisper.cpp 转写
        ↓ （独立线程推理，weak_ptr 防悬空，同 M1 的 SSE 做法）
转写结果文本 → 喂 M1 的 llama 链路 → 流式回答
        ↓
wsConn.send(token) 逐 token 推回浏览器
```

whisper.cpp 的接法和 M1 的 llama 完全同构：要么进程内库调用，要么起 `whisper-server` 走 localhost HTTP。推理在独立线程、用 `WeakTcpConnectionPtr` 观察连接存活——这套 M1 已经验证过，M2 直接复用。

注意 **localhost 被浏览器视为安全上下文**，`getUserMedia`（麦克风）在本地开发期不需要 HTTPS，故 M2 全程不阻塞于 TLS，TLS 仍留到 M4 上线时再做。

至此，WebSocket 层的真正难点只有两处——**握手的 SHA1/base64 + 101 切换**，和**帧的二进制增量解析（掩码/分片/控制帧）**；其余的连接管理、线程模型、推理接入全部复用 M0 传输层和 M1 已验证的模式，没有新的架构改动。
