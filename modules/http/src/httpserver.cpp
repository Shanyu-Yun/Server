#include "http/httpserver.hpp"

#include "base/buffer.hpp"
#include "base/timestamp.hpp"
#include "http/httpcontext.hpp"
#include "http/httprequest.hpp"
#include "http/httpresponse.hpp"
#include "net/callbacks.hpp"
#include "net/eventloop.hpp"
#include "net/inetaddress.hpp"
#include "net/tcpserver.hpp"

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

  if (resp.closeConnection()) {
    conn->shutdown();
  }
}
}  // namespace http
