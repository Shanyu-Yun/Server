#include "wsserver.hpp"

#include "base64.hpp"
#include "sha1.hpp"
#include "httpcontext.hpp"
#include "wscontext.hpp"

namespace protocol {

static constexpr char kWsMagic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WsServer::WsServer(transport::EventLoop* loop, const transport::InetAddress& listenAddr, std::string name)
    : server_(loop, listenAddr, std::move(name)) {
  // WS 升级请求在 httpCallback_ 里识别：
  // bypass=true  → appendToBuffer 什么都不发，避免 101 前先发出 200 OK
  // streaming=true → 触发 streamCallback_，由 onStream/tryUpgrade 发 101
  server_.setHttpCallback([](const HttpRequest& req, HttpResponse* resp) {
    if (req.getHeader("upgrade") == "websocket") {
      resp->setBypass(true);
      resp->setStreaming(true);
      resp->setCloseConnection(false);
    }
  });

  server_.setStreamCallback([this](const HttpRequest& req, HttpResponse* resp,
                                   const transport::TcpConnectionPtr& conn) { onStream(req, resp, conn); });

  server_.setRawMessageCallback(
      [this](const transport::TcpConnectionPtr& conn, transport::Buffer* buf, transport::Timestamp t) { onMessage(conn, buf, t); });
}

void WsServer::setThreadNum(int n) {
  server_.setThreadNum(n);
}
void WsServer::start() {
  server_.start();
}

std::string WsServer::buildHandshakeResponse(const std::string& clientKey) {
  std::string accept = transport::base64Encode(transport::sha1(clientKey + kWsMagic));
  return "HTTP/1.1 101 Switching Protocols\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Accept: " +
         accept + "\r\n\r\n";
}

bool WsServer::tryUpgrade(const transport::TcpConnectionPtr& conn, const HttpRequest& req) {
  if (req.getHeader("upgrade") != "websocket")
    return false;
  if (req.getHeader("sec-websocket-version") != "13")
    return false;
  std::string key = req.getHeader("sec-websocket-key");
  if (key.empty())
    return false;

  conn->send(buildHandshakeResponse(key));

  WsContext ctx;

  // lambda 存在 context_ 里，context_ 被 TcpConnection 持有
  // 若捕获 strong TcpConnectionPtr 会形成循环引用导致连接无法释放
  // 必须用 weak_ptr 打破环
  transport::WeakTcpConnectionPtr weak = conn;

  ctx.setSendCallback([weak](const std::string& raw) {
    if (auto c = weak.lock())
      c->send(raw.data(), raw.size());
  });

  WsMessageCallback cb = messageCb_;
  ctx.setFrameCallback([weak, cb](const WsFrame& frame) {
    auto c = weak.lock();
    if (!c) return;
    if (frame.opcode == Opcode::kClose) {
      c->shutdown();
      return;
    }
    if (cb)
      cb(std::make_shared<WsConnection>(c), frame.payload,
         frame.opcode == Opcode::kBinary);
  });

  conn->setContext(std::move(ctx));
  return true;
}

void WsServer::onStream(const HttpRequest& req, HttpResponse* /*resp*/, const transport::TcpConnectionPtr& conn) {
  tryUpgrade(conn, req);
}

void WsServer::onMessage(const transport::TcpConnectionPtr& conn, transport::Buffer* buf, transport::Timestamp) {
  auto* ctx = std::any_cast<WsContext>(conn->getMutableContext());
  if (!ctx)
    return;

  if (!ctx->parse(buf))
    conn->shutdown();
}

}  // namespace protocol
