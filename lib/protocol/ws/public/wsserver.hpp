#pragma once
#include <functional>
#include <string>

#include "httpserver.hpp"
#include "inetaddress.hpp"
#include "noncopyable.hpp"
#include "wsconnection.hpp"

namespace protocol {

using WsMessageCallback = std::function<void(const WsConnectionPtr&, std::string_view, bool isBinary)>;

class WsServer : transport::noncopyable {
 public:
  WsServer(transport::EventLoop* loop, const transport::InetAddress& listenAddr, std::string name);

  void setMessageCallback(WsMessageCallback cb) {
    messageCb_ = std::move(cb);
  }
  void setThreadNum(int n);
  void start();

 private:
  // 校验 Upgrade 请求，发 101，把 context_ 从 HttpContext 切换到 WsContext
  bool tryUpgrade(const transport::TcpConnectionPtr& conn, const HttpRequest& req);

  // HttpServer 的 StreamCallback：同时承接 HTTP（普通请求）和 WS 升级请求
  void onStream(const HttpRequest& req, HttpResponse* resp, const transport::TcpConnectionPtr& conn);

  // 升级后 TcpServer 原始 onMessage 接管，直接喂给 WsContext
  void onMessage(const transport::TcpConnectionPtr& conn, transport::Buffer* buf, transport::Timestamp t);

  static std::string buildHandshakeResponse(const std::string& clientKey);

  HttpServer server_;
  WsMessageCallback messageCb_;
};

}  // namespace protocol
