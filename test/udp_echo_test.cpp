// UDP echo 自验证测试：
// 服务端在 main 线程的 EventLoop 上 echo 回所有报文；
// 另起一个线程充当客户端，用普通阻塞 UDP socket 发包并收回，断言一致后退出 loop。

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <string>
#include <thread>

#include "logger.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "udpserver.hpp"

using namespace transport;

namespace {
constexpr uint16_t kPort = 9999;
std::atomic<bool> g_passed{false};

// 客户端线程：发若干报文，逐条收回校验，全部通过则置位并退出 loop。
void runClient(EventLoop* loop) {
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  timeval tv{2, 0};  // 2s 接收超时，避免测试卡死
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  sockaddr_in srv{};
  srv.sin_family = AF_INET;
  srv.sin_port = htons(kPort);
  ::inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

  const std::string msgs[] = {"hello-udp", "", std::string(40 * 1024, 'x')};  // 含空报文与大报文
  bool ok = true;
  for (const std::string& msg : msgs) {
    ::sendto(fd, msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&srv), sizeof(srv));

    char buf[64 * 1024];
    ssize_t n = ::recvfrom(fd, buf, sizeof(buf), 0, nullptr, nullptr);
    if (n < 0) {
      LOGERROR("client recvfrom timeout/error");
      ok = false;
      break;
    }
    if (std::string(buf, static_cast<size_t>(n)) != msg) {
      LOGERROR("client echo mismatch: expected {} bytes, got {}", msg.size(), n);
      ok = false;
      break;
    }
    LOGINFO("client echo ok: {} bytes", n);
  }

  ::close(fd);
  g_passed = ok;
  loop->quit();
}
}  // namespace

int main() {
  EventLoop loop;
  InetAddress listenAddr(kPort, "127.0.0.1");

  UdpServer server(&loop, listenAddr, "udp-echo");
  server.setMessageCallback([](UdpServer* s, std::string_view data, const InetAddress& peer, Timestamp) {
    LOGINFO("server recv {} bytes from {}", data.size(), peer.toIpPort());
    s->sendTo(std::string(data), peer);  // 原样 echo 回去
  });
  server.start();
  LOGINFO("udp-echo listening on {}", listenAddr.toIpPort());

  std::thread client(runClient, &loop);
  loop.loop();          // 客户端校验完会调 loop.quit() 让其返回
  client.join();

  if (g_passed) {
    LOGINFO("UDP echo test PASSED");
    return 0;
  }
  LOGERROR("UDP echo test FAILED");
  return 1;
}
