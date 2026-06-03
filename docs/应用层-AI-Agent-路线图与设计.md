# tinynet · 应用层(AI Agent)路线图与设计文档

> 目标定位：在已完成的**传输层**（`TcpServer` / `UdpServer` / `TimingWheel` / `EventLoop`）之上，**全 C++ 自建**一个 AI agent 服务栈，需求覆盖 RAG、音频识别、语音发送、AI 消息流式传输；客户端可能是 Web 前端（非仅 Qt）。
>
> 不走 Python 网关/sidecar：HTTP/SSE/WebSocket/TLS、出站 HTTPS 客户端、RAG 编排全部用 C++ 实现。C++ 这边有平行的原生组件可用——ASR 用 whisper.cpp、本地模型用 llama.cpp、向量检索用 hnswlib/faiss，**均进程内运行**，避免 FFI 整个 Python 生态。
>
> 本文档与 `通用网络库-路线图与设计.md` 平行：那份是传输层（明确"不做客户端"），本份是其上的应用层。API 代码块只给**声明**，实现留给动手；风格沿用现有约定：`noncopyable` 基类、`xxx_` 成员、`kXxx` 常量、`runInLoop` 跨线程投递、回调走 `using` 别名，类型位于 `namespace tinynet`。

---

## 0. 设计哲学（承接传输层那把尺子）

> **协议无关、任何上层都用得到的 → 留在 net 传输层；和某个协议（HTTP/WS/…）绑定的 → 上浮到本应用层。**

分层始终清晰：

```
AI 编排层     RAG 检索 · 对话状态 · 工具调用 · 模型/ASR 适配     ← llama.cpp / whisper.cpp / hnswlib
   ↑
协议层        HTTP codec · SSE · WebSocket · (出站)HTTP 客户端     ← 本文档主体
   ↑
传输层(net)   TcpServer · UdpServer · TimingWheel                ← 已完成，原样复用
   ↑
Reactor 核心  EventLoop · Poller · Channel · 线程池              ← 协议无关，已完成
```

线程模型不变：**one loop per thread**。协议层所有对象都归属某个 IO loop，跨线程操作一律 `runInLoop / queueInLoop` 投递回属主 loop。

贯穿全程的纪律：**每一步只做"够用的子集"，不追 RFC 完整性**。下面每个里程碑都标出"现在不做"清单。

---

## 1. 整体技术路线

把能力收成 4 个里程碑，每个都是一个**能独立演示的完整闭环**，避免陷在"做了一半看不到东西"。

| 里程碑 | 内容 | 依赖 | 可演示产出 |
|---|---|---|---|
| **M0** | 传输层（TcpServer/UdpServer/TimingWheel/Buffer/EventLoop） | — | ✅ 已完成 |
| **M1** | HTTP/1.1 codec → SSE → 接 llama.cpp（进程内） | M0 | 浏览器输入文本，token 逐个流式吐出。**零外部依赖、零 TLS** |
| **M2** | WebSocket(RFC6455) → 接 whisper.cpp（进程内） | M1 | 网页录音 → 实时转写 → 喂 M1 → 流式回答。dev 用 localhost 免 TLS |
| **M3** | embedding → hnswlib 向量索引 → RAG 检索拼接 | M1 | 回答能引用喂进去的文档 |
| **M4** | TLS(OpenSSL 集成 Reactor) → 出站 HTTPS 客户端(Connector) | M2/M3 | 可部署、浏览器麦克风可用、可调云端模型 |

**为什么是这个顺序**

- M1 把"流式"这条最核心的竖线打通——它是 AI agent 的灵魂，且全本地最快见效。
- TLS 放最后（M4），因为它是架构上最具侵入性的改动：OpenSSL 的 `SSL_ERROR_WANT_READ/WANT_WRITE` 状态机要重构进 `TcpConnection` 的 `handleRead/handleWrite` 读写路径。而 **localhost 被浏览器视为安全上下文**，`getUserMedia`（网页录音）在本地开发期不需要 HTTPS，故 TLS 不阻塞 M2 的语音开发，只在上线时成为硬需求。
- **出站客户端（Connector）此前在传输层被判为"不做"**（Qt 当客户端、数据库用库时确实不需要）。但全 C++ 路径下调云端模型/ASR 是出站 HTTPS，需要 Connector + HTTP 客户端 + TLS 客户端——故归到 M4。若只用本地 llama.cpp/whisper.cpp，可整体绕过。

---

## 2. 第一步 · HTTP/1.1 服务端（M1 起点）

HTTP 栈是 M1～M4 的地基：SSE 是它之上的长连接响应，WebSocket 握手是它的一个 `Upgrade` 请求。先做扎实。

### 2.1 类分解与职责

| 类 | 职责 | 性质 |
|---|---|---|
| `HttpRequest` | 解析结果的**值对象**：method、path、query、version、headers、body | 被动数据 |
| `HttpResponse` | 响应**构造器**：status、headers、body，能序列化成字节 | 被动数据 |
| `HttpContext` | **每连接**的解析状态机，持有半成品 `HttpRequest` | 核心难点 |
| `HttpServer` | 包装 `TcpServer`，接线 onConnection/onMessage，暴露用户回调 | 胶水 |

用户最终只对接一个回调：

```cpp
using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;
```

### 2.2 解析状态放哪 —— 复用 `context_`

`TcpConnection` 已有 `std::any context_`（TimingWheel 曾往里塞 `weak_ptr<Entry>`）。HTTP 把**每连接一个 `HttpContext`** 塞进去：

- `HttpServer::onConnection`：连接建立时 `conn->setContext(HttpContext{})`
- `HttpServer::onMessage`：`std::any_cast<HttpContext&>(*conn->getMu tableContext())` 取出，喂数据

一条连接的解析进度天然隔离在自己的 context 里，多连接互不干扰，**不需要任何额外的 map**。

### 2.3 前置改动 —— 给 `Buffer` 加 `findCRLF`

HTTP 是**行协议**，而当前 `Buffer` 只有 `peek()/retrieve(len)/retrieveAsString(len)/readableBytes()`，**没有行扫描原语**。下沉一个小工具到 Buffer（协议无关、可复用，符合 0 节哲学）：

```cpp
// 在 [peek(), peek()+readableBytes()) 中查找 "\r\n"，找不到返回 nullptr
const char* findCRLF() const;
```

有了它，解析一行的套路统一为：

```
const char* crlf = buf->findCRLF();
if (crlf == nullptr) return;                 // 半行，留在 Buffer，等下次 onMessage
// 用 [buf->peek(), crlf) 这一行（不含 CRLF）做解析
buf->retrieve(crlf - buf->peek() + 2);       // 消费这一行 + CRLF
```

**只在拿到完整 token 时才 `retrieve`**，半截的留在 Buffer——这是增量解析的核心机制。

### 2.4 解析状态机总览

```
kExpectRequestLine ──→ kExpectHeaders ──→ kExpectBody ──→ kGotAll
                                   └──（无 body）──────────┘
                          （任意步畸形）──→ kError
```

| 状态 | 在等什么 | 完成条件 | 下一步 |
|---|---|---|---|
| RequestLine | `GET /p?q HTTP/1.1\r\n` | 找到首个 CRLF | 拆三段，转 Headers |
| Headers | `Key: Value\r\n` 若干 | 遇到**空行**（仅 CRLF） | 看 Content-Length 决定有无 body |
| Body | N 字节实体 | 已读满 Content-Length | 转 GotAll |
| GotAll | — | — | 派发 + reset |

### 2.5 `HttpContext::parse()` 逐状态推进

职责分工：`parse()` 内部**尽可能多地推进**状态机，直到数据不够或集齐一个请求；外层 `onMessage` 负责"派发—reset"循环。`parse()` 返回值只表达**对错**（畸形 = false），"够不够"由调用方查 `gotAll()` 判断。

```
bool HttpContext::parse(Buffer* buf, Timestamp t):
    bool ok = true, hasMore = true
    while hasMore:
        switch state_:

        case kExpectRequestLine:
            crlf = buf->findCRLF()
            if crlf == null:  hasMore = false; break          // 半行，等下次
            ok = parseRequestLine(buf->peek(), crlf)           // 拆 method/path/query/version
            if not ok:        hasMore = false; break           // 畸形 → 报错退出
            request_.setReceiveTime(t)
            buf->retrieve(crlf - buf->peek() + 2)
            state_ = kExpectHeaders

        case kExpectHeaders:
            crlf = buf->findCRLF()
            if crlf == null:  hasMore = false; break
            colon = find ':' in [peek, crlf)
            if colon < crlf:
                request_.addHeader(peek, colon, crlf)          // key 小写化、value 去空白
            else:                                              // 空行 = 头部结束
                state_ = wantBody() ? kExpectBody : kGotAll
                if state_ == kGotAll: hasMore = false
            buf->retrieve(crlf - peek + 2)

        case kExpectBody:
            need = contentLength_ - request_.bodySize()
            if buf->readableBytes() < need: hasMore = false; break   // body 没到齐
            request_.appendBody(buf->peek(), need)
            buf->retrieve(need)
            state_ = kGotAll; hasMore = false

        default:  hasMore = false                              // kGotAll / kError
    return ok
```

三个设计判断：

- **"够不够"不靠返回值，靠 `state_`**。`parse` 返回 true 但 `state_ != kGotAll` 即"等更多数据"。这样畸形（false）与半包（true+未完成）区分干净。
- **body 用 Content-Length 一次性收齐再消费**。简单版不做边收边交付——代价是大 body 整块驻留内存，故**必须配 Content-Length 上限**（见 2.10 ③）。流式/分块上传推迟到 M2。
- **状态跨 onMessage 持久**。`state_`、半成品 `request_`、`contentLength_` 全在 `HttpContext` 里，而它活在 `conn->context_`，故请求拆成几次 TCP 段到达也不丢进度——增量解析得以成立。

### 2.6 `HttpRequest` 字段与接口

```cpp
enum Method  { kInvalid, kGet, kPost, kHead, kPut, kDelete };
enum Version { kUnknown, kHttp10, kHttp11 };

class HttpRequest {
 public:
  // —— 解析期写入（由 HttpContext 调用）——
  bool setMethod(const char* start, const char* end);   // 字符串→枚举，认不出返回 false
  void setPath(const char* start, const char* end);
  void setQuery(const char* start, const char* end);    // '?' 之后的原始串
  void setVersion(Version v);
  void addHeader(const char* start, const char* colon, const char* end);  // key 小写化 + value 去首尾空白
  void appendBody(const char* data, size_t len);
  void setReceiveTime(Timestamp t);

  // —— 用户回调期读取 ——
  Method method() const;
  const std::string& path() const;
  const std::string& query() const;
  std::string getHeader(const std::string& field) const;   // field 也按小写查
  const std::map<std::string, std::string>& headers() const;
  const std::string& body() const;
  bool keepAlive() const;                                   // 由 version + Connection 头推导

  void reset();                                            // keep-alive 下复用前清空
 private:
  Method method_; Version version_;
  std::string path_, query_, body_;
  std::map<std::string, std::string> headers_;             // key 已小写，可直接用 map 查找
  Timestamp receiveTime_;
};
```

`keepAlive()` 推导规则（**别硬编码成"总是保持"**）：

```
若 Connection 头含 "close"      → false
否则 HTTP/1.1                   → true
否则（HTTP/1.0 且无 keep-alive） → false
```

### 2.7 `HttpResponse` 字段与接口

```cpp
enum HttpStatusCode {
  k200Ok = 200, k400BadRequest = 400, k404NotFound = 404,
  k413PayloadTooLarge = 413, k500InternalServerError = 500,
};

class HttpResponse {
 public:
  explicit HttpResponse(bool closeConnection);            // 把 req.keepAlive() 取反传进来
  void setStatusCode(HttpStatusCode code);
  void setStatusMessage(std::string msg);                 // 不传则按 code 查默认（"OK"/"Not Found"…）
  void setCloseConnection(bool on);
  bool closeConnection() const;
  void addHeader(const std::string& key, const std::string& value);
  void setContentType(const std::string& type);           // = addHeader("Content-Type", type) 便捷封装
  void setBody(std::string body);
  void appendToBuffer(Buffer* out) const;                 // 序列化：状态行 + 头 + CRLF + body
 private:
  HttpStatusCode statusCode_; std::string statusMessage_;
  std::map<std::string, std::string> headers_;
  std::string body_; bool closeConnection_;
};
```

`appendToBuffer` 序列化要点：写 `HTTP/1.1 <code> <msg>\r\n` → 逐个 `k: v\r\n` → **自动补** `Content-Length: <body.size()>\r\n` → 按 `closeConnection_` 补 `Connection: close|keep-alive\r\n` → 空行 `\r\n` → body。**Content-Length 让框架补，别让用户手填（必错）**。

### 2.8 `HttpServer` 接线

```cpp
class HttpServer : noncopyable {
 public:
  HttpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name);
  void setHttpCallback(const HttpCallback& cb);
  void setThreadNum(int n);                                // 透传给内部 TcpServer
  void start();
 private:
  void onConnection(const TcpConnectionPtr& conn);         // 装入新的 HttpContext
  void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp t);
  void onRequest(const TcpConnectionPtr& conn, const HttpRequest& req);
  TcpServer server_;
  HttpCallback httpCallback_;
};
```

### 2.9 `onMessage` / `onRequest` 控制流

`onMessage` 一次可能拿到**半个请求** / **正好一个** / **好几个**（pipelining），故派发必须是循环：

```
onMessage(conn, buf, t):
    ctx = any_cast<HttpContext&>(*conn->getMutableContext())
    while true:
        if not ctx.parse(buf, t):                  // 推进状态机，消费 buf
            conn->send(400 响应); conn->shutdown(); return   // 畸形请求
        if not ctx.gotAll():
            break                                  // 数据不够，等下次 onMessage
        onRequest(conn, ctx.request())             // 派发一个完整请求
        ctx.reset()                                // ★ keep-alive 灵魂：复位以解析下一个请求

onRequest(conn, req):
    HttpResponse resp(/*closeConnection=*/ not req.keepAlive())
    httpCallback_(req, &resp)                       // 用户填 status/headers/body
    Buffer out;  resp.appendToBuffer(&out)
    conn->send(out.retrieveAllAsString())
    if resp.closeConnection():
        conn->shutdown()
```

`ctx.reset()` 漏了，keep-alive 就废了：同一条 TCP 连接上一个请求处理完，状态机必须回到 `kExpectRequestLine` 准备下一个。

### 2.10 三个易错细节

**① 头部名大小写不敏感。** `Content-Length` 与 `content-length` 等价（RFC）。在 `addHeader` 里**统一把 key 转小写**再入 map，`getHeader` 也按小写查；否则查找会漏。

**② 必须拷贝，不能用 string_view。** `HttpRequest` 的生命周期**跨越多次 onMessage**（body 可能下次才到），而 Buffer 内容会被 `retrieve` 消费、被后续数据覆盖。故 method/path/header/body 全部 `std::string` 拷贝持有。
> 这与 UDP 回调那个"零拷贝 string_view"是**相反**的取舍——因为两者生命周期不同：UDP 视图只在单次回调内有效，HTTP 请求要跨多次读事件存活。

**③ body 大小上限（DoS 面）。** `Content-Length` 超过上限（如 64MB）直接回 `413 Payload Too Large` 并关连接，否则一个声称 `Content-Length: 9999999999` 的请求能把内存吃爆。
> 呼应传输层已做的报文大小检测（UDP 65507 上限、TCP 单发不超 highWaterMark）——同一种"拒绝而非静默堆积"的防御思路。

### 2.11 现在不做（范围纪律）

| 不做 | 原因 / 推迟到 |
|---|---|
| `Transfer-Encoding: chunked` | M2 流式上传时再加（需 chunk-size 子状态机） |
| HTTP/2、HTTP/3 | 也许永远不需要；HTTP/1.1 + keep-alive 够用 |
| `100-continue`、`Expect` | 用到再说 |
| 完整 RFC 7230 合规 | 要的是务实子集，不是 nginx |
| cookie/session/路由框架 | 更上层，不属于 codec |

### 2.12 验收标准 / 测试用例

`HttpContext` 纯函数式、可单测：构造 Buffer 塞字节，调 parse，断言结果。必须覆盖：

- [ ] 一个完整 GET 请求 → method/path/query/version/headers 正确
- [ ] **把一个请求拆成两半**分两次 append → 增量解析不丢状态（**最重要的回归**）
- [ ] 一个 buffer 塞**两个**请求 → pipelining 循环逐个派发
- [ ] POST + Content-Length + body → body 完整
- [ ] keep-alive：解析完一个 reset 后能接着解析下一个
- [ ] 畸形请求行 → 进 error 态，回 400，不崩
- [ ] `Content-Length` 超上限 → 413
- [ ] `curl -v http://127.0.0.1:port/` 真实联调通过

**落点建议**：`include/http/`、`src/http/`，新增 `http_request` / `http_response` / `http_context` / `http_server`，与 `net/` 平级；`findCRLF` 加在 `base/buffer`。CMake 里把 `src/http/*.cpp` 加入 `tinynet` 库或单列 `tinyhttp`。

---

## 3. 后续里程碑预览（占位，做到再展开）

- **M1 续 · SSE**：HTTP 之上的长连接响应。`Content-Type: text/event-stream`，handler 不结束、持续 `conn->send("data: <token>\n\n")`。难点在连接生命周期——响应不能在 handler 返回后自动关闭，需要一个"流式响应"模式让连接保持。
- **M2 · WebSocket**：握手是 HTTP `Upgrade` 请求（`Sec-WebSocket-Key` → SHA1+base64 → `101`）；之后是帧 codec（FIN/opcode/mask/变长 payload、ping-pong、close 握手）。建 `WsConnection` 包在 `TcpConnection` 上。
- **M3 · RAG**：embedding（出站 API 或本地模型）+ hnswlib 进程内索引 + 检索拼接进 prompt。
- **M4 · TLS + 出站客户端**：OpenSSL 的 BIO/`WANT_READ`/`WANT_WRITE` 状态机集成进读写路径；`Connector`（非阻塞 connect + 重试退避）+ `HttpClient`（复用本文 codec 的客户端侧）。注意：调 OpenAI 等的上游本身是 SSE 流——你将同时是 SSE server 与 SSE client。
