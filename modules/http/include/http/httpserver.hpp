#pragma once
#include <functional>
#include <string>

#include "base/noncopyable.hpp"
#include "http/httprequest.hpp"
#include "http/httpresponse.hpp"
#include "net/tcpserver.hpp"

namespace http {

/**
 * @brief 用户处理一次 HTTP 请求的回调。
 *
 * 框架解析出 req 后调用，用户在 resp 上填写状态码/头/body。
 */
using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;

/**
 * @brief HTTP 服务器，封装 TcpServer，把字节流解析为 HTTP 请求并派发。
 *
 * 接线 TcpServer 的 onConnection/onMessage：连接建立时装入一个 HttpContext，
 * 数据到达时喂给它增量解析，集齐一个请求后调用用户回调，并支持 pipelining
 * 与 keep-alive。
 */
class HttpServer : tinynet::noncopyable {
 public:
  /**
   * @brief 构造 HttpServer。
   * @param loop       main loop。
   * @param listenAddr 监听地址。
   * @param name       服务器名称，用于日志与连接命名。
   */
  HttpServer(tinynet::EventLoop* loop, const tinynet::InetAddress& listenAddr, std::string name);

  /** @brief 设置请求处理回调。 */
  void setHttpCallback(const HttpCallback& cb) {
    httpCallback_ = cb;
  }

  /** @brief 设置 IO 线程数，透传给内部 TcpServer，须在 start() 前调用。 */
  void setThreadNum(int n);

  /** @brief 启动服务器开始监听。 */
  void start();

 private:
  /**
   * @brief 新连接建立：为其装入一个 HttpContext。
   */
  void onConnection(const tinynet::TcpConnectionPtr& conn);

  /**
   * @brief 数据到达：取出 HttpContext 增量解析，循环派发已集齐的请求。
   */
  void onMessage(const tinynet::TcpConnectionPtr& conn, Buffer* buf, tinynet::Timestamp receiveTime);

  /**
   * @brief 派发一个完整请求：构造响应、调用用户回调、写回并按需关连接。
   */
  void onRequest(const tinynet::TcpConnectionPtr& conn, const HttpRequest& req);

  tinynet::TcpServer server_;  ///< 底层 TCP 服务器。
  HttpCallback httpCallback_;  ///< 用户请求处理回调。
};

}  // namespace http
