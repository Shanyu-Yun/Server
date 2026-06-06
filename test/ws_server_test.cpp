#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "logger.hpp"
#include "wsconnection.hpp"
#include "wsserver.hpp"

using namespace transport;
using namespace protocol;

// 验证方式：
//   1. 用浏览器打开 html/ws_test.html，点击"连接"
//   2. 在输入框发送消息，服务端原样 echo 回来
//   3. curl 确认非 WS 请求被忽略（不升级，连接断开）

int main() {
  transport::EventLoop loop;
  transport::InetAddress addr(9090, "0.0.0.0");

  protocol::WsServer server(&loop, addr, "ws-test");

  server.setMessageCallback([](const protocol::WsConnectionPtr& conn, std::string_view msg, bool isBinary) {
    LOGINFO("recv: binary={} len={} payload={}", isBinary, msg.size(),
            isBinary ? "<binary>" : std::string(msg));
    if (isBinary)
      conn->sendBinary(msg.data(), msg.size());
    else
      conn->send(msg);
  });

  server.setThreadNum(2);
  server.start();

  LOGINFO("ws-test listening on 0.0.0.0:9090");
  LOGINFO("打开 html/ws_test.html 进行前端测试");

  loop.loop();
  return 0;
}
