#include "httpserver.hpp"

#include "buffer.hpp"
#include "timestamp.hpp"
#include "httpcontext.hpp"
#include "httprequest.hpp"
#include "httpresponse.hpp"
#include "callbacks.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "tcpserver.hpp"

namespace protocol {
HttpServer::HttpServer(transport::EventLoop* loop, const transport::InetAddress& listenAddr, std::string name)
    : server_(loop, listenAddr, std::move(name), transport::kReusePort) {
  //将TcpServer的回调设置为自己的私有方法
  server_.setConnectionCallback([this](const transport::TcpConnectionPtr& conn) { onConnection(conn); });
  server_.setMessageCallback(
      [this](const transport::TcpConnectionPtr& conn, transport::Buffer* buf, transport::Timestamp t) { onMessage(conn, buf, t); });
}

void HttpServer::setThreadNum(int n) {
  server_.setThreadNum(n);
}

void HttpServer::start() {
  server_.start();
}

void HttpServer::onConnection(const transport::TcpConnectionPtr& conn) {
  if (conn->isConnected()) {
    conn->setContext(HttpContext{});
  }
}

void HttpServer::onMessage(const transport::TcpConnectionPtr& conn, transport::Buffer* buf, transport::Timestamp receiveTime) {
  HttpContext* ctx = std::any_cast<HttpContext>(conn->getMutableContext());
  if (!ctx) {
    // context_ 已被替换（如 WebSocket 升级后），转发给注册方处理
    if (rawMessageCallback_) rawMessageCallback_(conn, buf, receiveTime);
    return;
  }
  if (!ctx->parse(buf, receiveTime)) {
    conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
    conn->shutdown();
    return;
  }

  while (ctx->gotAll()) {
    onRequest(conn, ctx->request());
    // onRequest 里可能发生协议升级（如 WS），setContext() 会销毁原 HttpContext
    // 必须重新拿指针，拿不到说明 context 已被替换，停止 HTTP 解析
    ctx = std::any_cast<HttpContext>(conn->getMutableContext());
    if (!ctx) break;
    ctx->reset();
    ctx->parse(buf, receiveTime);
  }
}

void HttpServer::onRequest(const transport::TcpConnectionPtr& conn, const HttpRequest& req) {
  HttpResponse resp(!req.keepAlive());
  if (httpCallback_) httpCallback_(req, &resp);

  transport::Buffer buf{};
  resp.appendToBuffer(&buf);
  conn->send(buf.retrieveAsString(buf.readableBytes()));

  if (resp.getStreaming() && streamCallback_) {
    streamCallback_(req, &resp, conn);
  }

  if (resp.closeConnection()) {
    conn->shutdown();
  }
}

std::string HttpServer::makeSseFrame(const std::string& data) {
  return "data: " + data + "\n\n";
}

}  // namespace protocol
