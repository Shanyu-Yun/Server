#include "base/logger.hpp"
#include "http/httpcontext.hpp"
#include "http/httprequest.hpp"
#include "http/httpresponse.hpp"
#include "http/httpserver.hpp"
#include "net/eventloop.hpp"
#include "net/inetaddress.hpp"

using namespace net;

// 验证方式：
//   telnet 127.0.0.1 8080
//   GET / HTTP/1.1\r\nHost: localhost\r\n\r\n
//
// 或者用 curl：
//   curl -v http://127.0.0.1:8080/
//   curl -v http://127.0.0.1:8080/hello
//   curl -v -X POST -d '{"msg":"hi"}' http://127.0.0.1:8080/echo

static void onRequest(const http::HttpRequest& req, http::HttpResponse* resp) {
  LOGINFO("method={} path={} version={}",
          static_cast<int>(req.method()),
          req.path(),
          static_cast<int>(req.version()));

  // 打印所有请求头
  for (const auto& h : req.headers()) {
    LOGINFO("  header: {}={}", h.first, h.second);
  }

  if (req.path() == "/") {
    resp->setStatusCode(http::k200Ok);
    resp->setContentType("text/html; charset=utf-8");
    resp->setBody("<html><body><h1>tinynet HTTP server</h1></body></html>\r\n");

  } else if (req.path() == "/hello") {
    resp->setStatusCode(http::k200Ok);
    resp->setContentType("text/plain; charset=utf-8");
    resp->setBody("hello, tinynet!\r\n");

  } else if (req.path() == "/echo") {
    // 把请求体原样 echo 回去
    resp->setStatusCode(http::k200Ok);
    resp->setContentType(req.getHeader("content-type").empty()
                             ? "application/octet-stream"
                             : req.getHeader("content-type"));
    resp->setBody(req.body());

  } else {
    resp->setStatusCode(http::k404NotFound);
    resp->setContentType("text/plain");
    resp->setBody("404 Not Found\r\n");
  }
}

int main() {
  EventLoop loop;
  InetAddress listenAddr(8080, "127.0.0.1");

  http::HttpServer server(&loop, listenAddr, "http-test");
  server.setHttpCallback(onRequest);
  server.setThreadNum(2);
  server.start();

  LOGINFO("http-test listening on {}", listenAddr.toIpPort());
  LOGINFO("try: curl -v http://127.0.0.1:8080/");
  LOGINFO("     curl -v http://127.0.0.1:8080/hello");
  LOGINFO("     curl -v -X POST -d '{{\"msg\":\"hi\"}}' http://127.0.0.1:8080/echo");

  loop.loop();
  return 0;
}
