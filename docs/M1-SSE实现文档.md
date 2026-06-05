# M1 续 · SSE 实现文档

> 前置：HTTP/1.1 codec（HttpRequest / HttpResponse / HttpContext / HttpServer）已完成。
> 本文档只记录在此基础上做 SSE 所需的**改动和新增**，不重复 HTTP 基础部分。

---

## SSE 是什么？它和普通 HTTP 响应有什么不同？

普通 HTTP 是一问一答：客户端发请求，服务端回一个完整响应，连接随即关闭（或复用等下一个请求）。**SSE（Server-Sent Events）** 是一个特殊的 HTTP 响应，它的 body 永远不结束——服务端持续往里写数据，客户端持续读，直到一方主动断开。

格式极简。响应头固定两行：

```http
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

data: 今\n\n
data: 天\n\n
data: 天\n\n
data: 气\n\n
data: 不\n\n
data: 错\n\n
data: [DONE]\n\n
```

每一帧以 `data: <内容>\n\n` 的格式推送，两个 `\n` 是帧边界。浏览器端用 `EventSource` API 接收，每收到一帧就触发一次 `onmessage` 回调。这正是 AI token 流式输出的传输载体——llama.cpp 每生成一个 token，就 push 一帧。

---

## 现有框架的生命周期为什么不够用？

现在的 `onRequest` 是同步模型：

```cpp
void HttpServer::onRequest(const TcpConnectionPtr& conn, const HttpRequest& req) {
  HttpResponse resp(!req.keepAlive());
  httpCallback_(req, &resp);       // 用户回调同步填好整个 resp
  Buffer buf;
  resp.appendToBuffer(&buf);
  conn->send(buf.retrieveAsString(buf.readableBytes()));
  if (resp.closeConnection()) conn->shutdown();
}
```

`httpCallback_` 返回后，响应已经一次性序列化并发送完毕。SSE 需要的是：**回调返回之后，连接还活着，后续可以反复调 `conn->send`**。两者根本冲突——`HttpResponse` 是值对象，设计上就是"填完即用"，没有"先发头、后持续发 body"的概念。

---

## 需要哪些改动？

改动分三层，由小到大：

### ① `HttpResponse` 新增流式模式标志

在 `HttpResponse` 加一个 `streaming_` 布尔成员，`appendToBuffer` 检查它：

```cpp
void HttpResponse::setStreaming(bool on) { streaming_ = on; }
bool HttpResponse::streaming() const     { return streaming_; }
```

`streaming_` 为 true 时，`appendToBuffer` **只序列化响应头，不写 body、不写 `Content-Length`**（SSE 响应没有 Content-Length，body 长度未知）：

```cpp
void HttpResponse::appendToBuffer(net::Buffer* out) const {
  // 状态行 + 用户设置的头
  ...
  if (!streaming_) {
    out->append("Content-Length: " + std::to_string(body_.size()) + "\r\n");
  }
  out->append("Connection: " + std::string(closeConnection_ ? "close" : "keep-alive") + "\r\n");
  out->append("\r\n");        // 头部结束空行，必须有
  if (!streaming_) {
    out->append(body_);       // 非流式才写 body
  }
  // 流式模式：只发出头，body 由上层持续 conn->send
}
```

### ② `HttpServer::onRequest` 识别流式响应

`httpCallback_` 返回后检查 `resp.streaming()`：

```cpp
void HttpServer::onRequest(const TcpConnectionPtr& conn, const HttpRequest& req) {
  HttpResponse resp(!req.keepAlive());
  httpCallback_(req, &resp);

  net::Buffer buf;
  resp.appendToBuffer(&buf);
  conn->send(buf.retrieveAsString(buf.readableBytes()));

  if (resp.streaming()) {
    return;   // 头已发出，连接保持，不关闭——由上层持有 conn 继续 send
  }
  if (resp.closeConnection()) {
    conn->shutdown();
  }
}
```

关键：`streaming_` 为 true 时，既不调 `conn->shutdown()`，也不等 body——直接返回，连接保持活跃。

### ③ 用户侧：持有 `conn`，异步推 token

用户回调里拿到 conn，存进某个地方（定时器 / 模型推理回调），后续反复 send：

```cpp
// 伪代码，展示意图
server.setHttpCallback([](const HttpRequest& req, HttpResponse* resp) {
  if (req.path() == "/stream") {
    resp->setStatusCode(k200Ok);
    resp->setContentType("text/event-stream");
    resp->addHeader("Cache-Control", "no-cache");
    resp->setStreaming(true);          // 只发头，不发 body
    resp->setCloseConnection(false);   // keep-alive

    // conn 从哪来？见下文
  }
});
```

这里有个问题：`HttpCallback` 的签名是 `void(const HttpRequest&, HttpResponse*)`，**拿不到 `conn`**。

---

## 为什么拿不到 conn？如何解决？

现有回调签名：

```cpp
using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;
```

没有把 `conn` 传出来。SSE 需要上层持有 conn 才能后续 send，有两种接法：

**方案 A：新增一个携带 conn 的回调类型（推荐）**

```cpp
using HttpCallback    = std::function<void(const HttpRequest&, HttpResponse*)>;
using StreamCallback  = std::function<void(const HttpRequest&,
                                            HttpResponse*,
                                            const net::TcpConnectionPtr&)>;
```

`HttpServer` 多一个 `setStreamCallback`，在 `onRequest` 里检查请求路径 / Accept 头决定走哪条路。优点：普通请求和 SSE 请求的用户代码完全分开，互不干扰。

**方案 B：让 HttpCallback 也携带 conn**

把现有签名改成：

```cpp
using HttpCallback = std::function<void(const HttpRequest&,
                                         HttpResponse*,
                                         const net::TcpConnectionPtr&)>;
```

所有用户回调都能拿到 conn。缺点：普通请求的回调里多了一个用不到的参数，略啰嗦。

两种方案都可行，取决于你希望接口是"普通路由和流式路由分开"还是"统一一个回调"。

---

## SSE 帧格式

帧格式只有一行：

```
data: <内容>\n\n
```

封装成辅助函数，让发送侧不用手拼格式：

```cpp
// 封装在 httpserver 里或单独的 sse 工具头
static std::string makeSseFrame(const std::string& data) {
  return "data: " + data + "\n\n";
}
```

推理结束时发终止帧：

```cpp
conn->send(makeSseFrame("[DONE]"));
// 然后按 keepAlive 决定是否关连接
if (!req.keepAlive()) conn->shutdown();
```

浏览器端收到 `[DONE]` 后可以关闭 `EventSource`（约定俗成，不是协议强制）。

---

## 连接断开时如何感知？

上层持有 `conn` 异步推 token，但用户随时可能关浏览器。如何知道连接已断？

`TcpConnection` 有 `setCloseCallback`，但那是框架内部用的。上层应该用 `setConnectionCallback`：

```cpp
// 在 onConnection 里注册断开处理
void HttpServer::onConnection(const TcpConnectionPtr& conn) {
  if (conn->connected()) {
    conn->setContext(HttpContext{});
  } else {
    // 连接断开：如果有进行中的 SSE，通知上层停止推送
    // 具体机制：conn 本身是 shared_ptr，上层持有的 weak_ptr 会变 expired
  }
}
```

**推荐做法**：上层持有 `weak_ptr<TcpConnection>` 而不是 `shared_ptr`：

```cpp
net::WeakTcpConnectionPtr weakConn = conn;  // TcpConnectionPtr 的 weak 版

// 推理线程每次 send 前检查连接是否还活着
if (auto c = weakConn.lock()) {
  c->send(makeSseFrame(token));
} else {
  // 连接已断，停止推理 / 清理资源
  break;
}
```

用 `weak_ptr` 的好处：连接断开后 `TcpConnection` 对象销毁，`lock()` 自然返回空指针，不需要额外的"断开通知"机制，也不会因为上层持有 `shared_ptr` 而延长连接的生命周期。

---

## Content-Length 上限（DoS 防御）补充

设计文档 2.10 ③ 要求大 body 回 413，目前 `HttpContext::parse` 里取到 `content-length` 后还没做上限检查。补在取值之后：

```cpp
// kExpectHeaders，空行处
std::string len = request_.getHeader("content-length");
if (!len.empty()) {
  contentLength_ = std::stol(len);
  if (contentLength_ > kMaxBodySize) {   // kMaxBodySize 建议 64MB
    ok = false;
    state_ = kError;
  }
}
```

`kMaxBodySize` 定义为常量：

```cpp
static constexpr long kMaxBodySize = 64 * 1024 * 1024;  // 64MB
```

---

## 验收标准

- [ ] 浏览器打开测试页，发请求到 `/stream`，页面上逐字出现 token
- [ ] 用户关掉浏览器 tab，服务端的 `weakConn.lock()` 返回空，推理停止
- [ ] 普通 GET 请求（`/`、`/hello`）不受影响，响应正常
- [ ] `curl -N http://127.0.0.1:8080/stream` 可以看到 SSE 帧逐行输出（`-N` 禁用 curl 的缓冲）

至此，SSE 层的框架改动只集中在 `HttpResponse::streaming_` 和 `HttpServer::onRequest` 的十几行代码上，HTTP codec 本身不需要动——SSE 的帧推送完全靠已有的 `conn->send`，协议简单性在这里体现得最彻底。
