#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "net/eventloop.hpp"
#include "net/inetaddress.hpp"
#include "net/tcpserver.hpp"

// ──────────────────── 配置 ────────────────────
namespace cfg {
constexpr uint16_t kPort = 9877;
constexpr int kIOThreads = 8;
constexpr int kClients = 16;
constexpr int kDurationSec = 5;
constexpr int kBufSize = 4096;
}  // namespace cfg

// ──────────────────── 全局计数 ────────────────────
std::atomic<int64_t> gBytesSent{0};
std::atomic<int64_t> gBytesRecv{0};
std::atomic<bool> gStop{false};

// ──────────────────── 客户端线程 ────────────────────
void clientWorker() {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(cfg::kPort);
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return;
  }

  char wbuf[cfg::kBufSize];
  char rbuf[cfg::kBufSize];
  ::memset(wbuf, 'x', sizeof(wbuf));

  while (!gStop.load(std::memory_order_relaxed)) {
    ssize_t nw = ::send(fd, wbuf, sizeof(wbuf), MSG_NOSIGNAL);
    if (nw <= 0)
      break;
    gBytesSent.fetch_add(nw, std::memory_order_relaxed);

    ssize_t total = 0;
    while (total < nw) {
      ssize_t nr = ::read(fd, rbuf, sizeof(rbuf));
      if (nr <= 0)
        goto done;
      gBytesRecv.fetch_add(nr, std::memory_order_relaxed);
      total += nr;
    }
  }
done:
  ::close(fd);
}

// ──────────────────── 主函数 ────────────────────
int main() {
  EventLoop* serverLoop = nullptr;
  std::atomic<bool> serverReady{false};

  std::thread serverThread([&] {
    EventLoop loop;
    serverLoop = &loop;

    InetAddress addr(cfg::kPort, "127.0.0.1");
    TcpServer server(&loop, addr, "bench", kReusePort);
    server.setThreadNum(cfg::kIOThreads);
    server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
      conn->send(buf->retrieveAsString(buf->readableBytes()));
    });
    server.start();
    serverReady.store(true, std::memory_order_release);
    loop.loop();
  });

  while (!serverReady.load(std::memory_order_acquire))
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::thread> clients;
  clients.reserve(cfg::kClients);
  for (int i = 0; i < cfg::kClients; ++i)
    clients.emplace_back(clientWorker);

  const auto t0 = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::seconds(cfg::kDurationSec));
  gStop.store(true, std::memory_order_release);

  for (auto& t : clients)
    t.join();
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  if (serverLoop)
    serverLoop->quit();
  serverThread.join();

  const double sentMB = gBytesSent.load() / 1e6;
  const double recvMB = gBytesRecv.load() / 1e6;
  const double sendThr = sentMB / elapsed;
  const double recvThr = recvMB / elapsed;

  std::cout << "\n=== muduo benchmark ===\n"
            << std::fixed << std::setprecision(2) << "  IO threads : " << cfg::kIOThreads << "\n"
            << "  Clients    : " << cfg::kClients << "\n"
            << "  Duration   : " << elapsed << " s\n"
            << "  Buf size   : " << cfg::kBufSize << " bytes\n"
            << "\n"
            << "  Sent       : " << sentMB << " MB  (" << sendThr << " MB/s  /  "
            << sendThr * 8 / 1000 << " Gbps)\n"
            << "  Received   : " << recvMB << " MB  (" << recvThr << " MB/s  /  "
            << recvThr * 8 / 1000 << " Gbps)\n"
            << "========================\n";
  return 0;
}
