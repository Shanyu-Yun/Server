#include "wsserver.hpp"

#include "base64.hpp"
#include "sha1.hpp"
#include "httpcontext.hpp"
#include "wscontext.hpp"

namespace ws {

static constexpr char kWsMagic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WsServer::WsServer(net::EventLoop* loop, const net::InetAddress& listenAddr, std::string name)
    : server_(loop, listenAddr, std::move(name)) {
  server_.setStreamCallback([this](const http::HttpRequest& req, http::HttpResponse* resp,
                                   const net::TcpConnectionPtr& conn) { onStream(req, resp, conn); });

  server_.setRawMessageCallback(
      [this](const net::TcpConnectionPtr& conn, net::Buffer* buf, net::Timestamp t) { onMessage(conn, buf, t); });
}

void WsServer::setThreadNum(int n) {
  server_.setThreadNum(n);
}
void WsServer::start() {
  server_.start();
}

std::string WsServer::buildHandshakeResponse(const std::string& clientKey) {
  std::string accept = base64Encode(sha1(clientKey + kWsMagic));
  return "HTTP/1.1 101 Switching Protocols\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Accept: " +
         accept + "\r\n\r\n";
}

bool WsServer::tryUpgrade(const net::TcpConnectionPtr& conn, const http::HttpRequest& req) {
  if (req.getHeader("upgrade") != "websocket")
    return false;
  if (req.getHeader("sec-websocket-version") != "13")
    return false;
  std::string key = req.getHeader("sec-websocket-key");
  if (key.empty())
    return false;

  conn->send(buildHandshakeResponse(key));
  conn->setContext(WsContext{});
  return true;
}

void WsServer::onStream(const http::HttpRequest& req, http::HttpResponse* /*resp*/, const net::TcpConnectionPtr& conn) {
  tryUpgrade(conn, req);
}

void WsServer::onMessage(const net::TcpConnectionPtr& conn, net::Buffer* buf, net::Timestamp) {
  auto* ctx = std::any_cast<WsContext>(conn->getMutableContext());
  if (!ctx)
    return;

  if (!ctx->parse(buf)) {
    conn->shutdown();
    return;
  }
}

}  // namespace ws
