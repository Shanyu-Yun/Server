#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "acceptor.hpp"
#include "buffer.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "logger.hpp"
#include "tcpconnection.hpp"

namespace {

InetAddress getLocalAddr(int sockfd) {
  sockaddr_in localAddr {};
  socklen_t addrLen = sizeof(localAddr);
  if (::getsockname(sockfd, reinterpret_cast<sockaddr*>(&localAddr), &addrLen) < 0) {
    LOGERROR("getsockname error for fd {}", sockfd);
    return InetAddress();
  }
  return InetAddress(localAddr);
}

}  // namespace

int main() {
  EventLoop loop;
  InetAddress listenAddr(8888, "127.0.0.1");
  Acceptor acceptor(&loop, listenAddr, true);
  std::unordered_map<std::string, TcpConnectionPtr> connections;
  uint64_t nextConnId = 1;

  acceptor.setNewConnectionCallback([&](int connfd, const InetAddress& peerAddr) {
    const std::string connName = "tcp-demo-" + std::to_string(nextConnId++);
    const InetAddress localAddr = getLocalAddr(connfd);
    auto conn = std::make_shared<TcpConnection>(&loop, connName, connfd, localAddr, peerAddr);

    conn->setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buffer, Timestamp) {
      const std::string message = buffer->retrieveAsString(buffer->readableBytes());
      LOGINFO("[{}] received {} bytes: {}", conn->getName(), message.size(), message);
      conn->send("server received: " + message);
    });

    conn->setCloseCallback([&connections](const TcpConnectionPtr& conn) {
      LOGINFO("[{}] close connection from {}", conn->getName(), conn->getPeerAddr().toIpPort());
      connections.erase(conn->getName());
    });

    connections[connName] = conn;
    conn->connectEstablished();

    LOGINFO("[{}] new connection {} -> {}", connName, peerAddr.toIpPort(),
            localAddr.toIpPort());
    conn->send("hello from muduo_learn tcp demo\n");
  });

  acceptor.listen();
  LOGINFO("acceptor is listening on {}", listenAddr.toIpPort());

  loop.loop();
  LOGINFO("acceptor test finished");

  return 0;
}
