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

namespace http {
HttpServer::HttpServer(net::EventLoop* loop, const net::InetAddress& listenAddr, std::string name)
    : server_(loop, listenAddr, std::move(name), net::kReusePort) {
  //将TcpServer的回调设置为自己的私有方法
  server_.setConnectionCallback([this](const net::TcpConnectionPtr& conn) { onConnection(conn); });
  server_.setMessageCallback(
      [this](const net::TcpConnectionPtr& conn, net::Buffer* buf, net::Timestamp t) { onMessage(conn, buf, t); });
}

void HttpServer::setThreadNum(int n) {
  server_.setThreadNum(n);
}

void HttpServer::start() {
  server_.start();
}

void HttpServer::onConnection(const net::TcpConnectionPtr& conn) {
  if (conn->isConnected()) {
    conn->setContext(HttpContext{});
  }
}

void HttpServer::onMessage(const net::TcpConnectionPtr& conn, net::Buffer* buf, net::Timestamp receiveTime) {
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
    ctx->reset();
    ctx->parse(buf, receiveTime);
  }
}

void HttpServer::onRequest(const net::TcpConnectionPtr& conn, const HttpRequest& req) {
  HttpResponse resp(!req.keepAlive());
  httpCallback_(req, &resp);

  net::Buffer buf{};
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

}  // namespace http
