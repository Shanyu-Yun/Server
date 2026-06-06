#include <memory>
#include <vector>

#include "logger.hpp"
#include "httprequest.hpp"
#include "httpresponse.hpp"
#include "httpserver.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "timer.hpp"

using namespace transport;
using namespace protocol;

// 验证方式：
//   普通路由：curl -v http://127.0.0.1:8080/
//             curl -v http://127.0.0.1:8080/hello
//             curl -v -X POST -d 'hello' http://127.0.0.1:8080/echo
//   SSE 路由：curl -N http://127.0.0.1:8080/stream
//             浏览器 console: new EventSource("http://127.0.0.1:8080/stream").onmessage=e=>console.log(e.data)

// 模拟 token 序列，代替真实 llama.cpp 输出
static const std::vector<std::string> kFakeTokens = {
    "今", "天", "天", "气", "不", "错", "，", "适", "合", "写", "代", "码", "。"
};

static void onRequest(const protocol::HttpRequest& req, protocol::HttpResponse* resp) {
  LOGINFO("method={} path={}", static_cast<int>(req.method()), req.path());

  if (req.path() == "/") {
    resp->setStatusCode(protocol::k200Ok);
    resp->setContentType("text/html; charset=utf-8");
    resp->setBody("<html><body>"
                  "<h1>tinynet HTTP server</h1>"
                  "<p><a href='/stream'>SSE stream test</a></p>"
                  "</body></html>\r\n");

  } else if (req.path() == "/hello") {
    resp->setStatusCode(protocol::k200Ok);
    resp->setContentType("text/plain; charset=utf-8");
    resp->setBody("hello, tinynet!\r\n");

  } else if (req.path() == "/echo") {
    resp->setStatusCode(protocol::k200Ok);
    resp->setContentType(req.getHeader("content-type").empty()
                             ? "application/octet-stream"
                             : req.getHeader("content-type"));
    resp->setBody(req.body());

  } else if (req.path() == "/stream") {
    // SSE 路由：只填响应头，body 由 StreamCallback 异步推送
    resp->setStatusCode(protocol::k200Ok);
    resp->setContentType("text/event-stream");
    resp->addHeader("Cache-Control", "no-cache");
    resp->setStreaming(true);
    resp->setCloseConnection(false);

  } else {
    resp->setStatusCode(protocol::k404NotFound);
    resp->setContentType("text/plain");
    resp->setBody("404 Not Found\r\n");
  }
}

// SSE 流式回调：拿到 conn 后用定时器逐 token 推送
static void onStream(const protocol::HttpRequest& req,
                     protocol::HttpResponse*,
                     const TcpConnectionPtr& conn) {
  if (req.path() != "/stream") return;

  // 用 weak_ptr 观察连接是否存活，不延长其生命周期
  WeakTcpConnectionPtr weakConn = conn;
  auto tokens = std::make_shared<std::vector<std::string>>(kFakeTokens);
  auto index  = std::make_shared<size_t>(0);

  // 拿到 conn 所在的 loop，定时器必须在同一个 loop 里注册
  EventLoop* loop = conn->getLoop();

  // 每 200ms 推一个 token
  std::shared_ptr<TimerId> timerId = std::make_shared<TimerId>();
  *timerId = loop->runEvery(0.2, [weakConn, tokens, index, loop, timerId]() {
    auto c = weakConn.lock();
    if (!c) {
      // 连接已断，取消定时器，不再推送
      loop->cancel(*timerId);
      return;
    }
    if (*index < tokens->size()) {
      c->send(protocol::HttpServer::makeSseFrame((*tokens)[(*index)++]));
    } else {
      // 所有 token 推完，发终止帧并结束
      c->send(protocol::HttpServer::makeSseFrame("[DONE]"));
      loop->cancel(*timerId);
      // SSE 结束后保持连接（keep-alive），等待下一个请求
    }
  });

  LOGINFO("SSE stream started for conn {}", conn->getName());
}

int main() {
  EventLoop loop;
  InetAddress listenAddr(8080, "127.0.0.1");

  protocol::HttpServer server(&loop, listenAddr, "http-test");
  server.setHttpCallback(onRequest);
  server.setStreamCallback(onStream);
  server.setThreadNum(2);
  server.start();

  LOGINFO("http-test listening on 127.0.0.1:8080");
  LOGINFO("普通路由: curl -v http://127.0.0.1:8080/");
  LOGINFO("SSE 验证: curl -N http://127.0.0.1:8080/stream");

  loop.loop();
  return 0;
}
