#pragma once

#include <functional>
#include <string>

#include "noncopyable.hpp"
#include "httpserver.hpp"
#include "inetaddress.hpp"
#include "wsconnection.hpp"

namespace ws {

using WsMessageCallback =
    std::function<void(const WsConnectionPtr&, std::string_view, bool isBinary)>;

class WsServer : net::noncopyable {
 public:
  WsServer(net::EventLoop* loop, const net::InetAddress& listenAddr, std::string name);

  void setMessageCallback(WsMessageCallback cb) { messageCb_ = std::move(cb); }
  void setThreadNum(int n);
  void start();

 private:
  // 校验 Upgrade 请求，发 101，把 context_ 从 HttpContext 切换到 WsContext
  bool tryUpgrade(const net::TcpConnectionPtr& conn, const http::HttpRequest& req);

  // HttpServer 的 StreamCallback：同时承接 HTTP（普通请求）和 WS 升级请求
  void onStream(const http::HttpRequest& req,
                http::HttpResponse*      resp,
                const net::TcpConnectionPtr& conn);

  // 升级后 TcpServer 原始 onMessage 接管，直接喂给 WsContext
  void onMessage(const net::TcpConnectionPtr& conn,
                 net::Buffer*                  buf,
                 net::Timestamp                t);

  static std::string buildHandshakeResponse(const std::string& clientKey);

  http::HttpServer server_;
  WsMessageCallback messageCb_;
};

}  // namespace ws
