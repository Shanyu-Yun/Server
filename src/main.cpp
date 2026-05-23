#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "acceptor.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "logger.hpp"

int main() {
  EventLoop loop;
  InetAddress listenAddr(8888, "127.0.0.1");
  Acceptor acceptor(&loop, listenAddr, true);

  acceptor.setNewConnectionCallback([&loop](int connfd, const InetAddress& peerAddr) {
    LOGINFO("new connection from {}", peerAddr.toIpPort());
  });

  acceptor.listen();
  LOGINFO("acceptor is listening on {}", listenAddr.toIpPort());

  loop.loop();
  LOGINFO("acceptor test finished");

  return 0;
}
