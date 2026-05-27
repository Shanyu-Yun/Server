#include "base/logger.hpp"
#include "net/eventloop.hpp"
#include "net/inetaddress.hpp"
#include "net/tcpconnection.hpp"
#include "net/tcpserver.hpp"

int main() {
  EventLoop loop;
  InetAddress listenAddr(8888, "127.0.0.1");

  TcpServer server(&loop, listenAddr, "echo-server", kReusePort);
  server.setThreadNum(4);

  server.setConnectionCallback([](const TcpConnectionPtr& conn) {
    if (conn->isConnected()) {
      LOGINFO("[{}] connected from {}", conn->getName(), conn->getPeerAddr().toIpPort());
      conn->send("hello from echo-server\n");
    } else {
      LOGINFO("[{}] disconnected", conn->getName());
    }
  });

  server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
    const std::string msg = buf->retrieveAsString(buf->readableBytes());
    LOGINFO("[{}] received {} bytes: {}", conn->getName(), msg.size(), msg);
    conn->send(msg);
  });

  server.start();
  LOGINFO("echo-server listening on {}", listenAddr.toIpPort());
  loop.loop();
  return 0;
}
