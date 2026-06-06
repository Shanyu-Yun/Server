#include "tcpserver.hpp"

#include <sys/socket.h>

#include "logger.hpp"
#include "tcpconnection.hpp"
#include "timingwheel.hpp"

namespace transport {

namespace {

InetAddress getLocalAddr(int sockfd) {
  sockaddr_in localAddr{};
  socklen_t addrLen = sizeof(localAddr);
  if (::getsockname(sockfd, reinterpret_cast<sockaddr*>(&localAddr), &addrLen) < 0) {
    LOGERROR("TcpServer: getsockname error for fd {}", sockfd);
    return InetAddress();
  }
  return InetAddress(localAddr);
}

}  // namespace

TcpServer::TcpServer(EventLoop* loop, const InetAddress listenAddr, std::string name, Option option)
    : loop_(loop),
      name_(std::move(name)),
      ipPort_(listenAddr.toIpPort()),
      acceptor_(std::make_unique<Acceptor>(loop, listenAddr, option == kReusePort)),
      threadPool_(std::make_shared<EventLoopThreadPool>(loop, name_)),
      started_(0),
      nextConnId_(1) {
  acceptor_->setNewConnectionCallback(
      [this](int sockfd, const InetAddress& peerAddr) { newConnection(sockfd, peerAddr); });
}

TcpServer::~TcpServer() {
  for (auto& [name, conn] : connections_) {
    // 这里其实直接使用conn->connectDestroyed()也行
    // 只是风格选择，让析构路径按照map先放手，再调用connectDestroyed这个流程
    TcpConnectionPtr localConn(conn);
    conn.reset();
    localConn->getLoop()->runInLoop([localConn] { localConn->connectDestroyed(); });
  }
}

void TcpServer::start() {
  int expected = 0;
  if (started_.compare_exchange_strong(expected, 1)) {
    threadPool_->start(threadInitCallback_);
    if (idleTimeout_ > 0) {
      for (EventLoop* loop : threadPool_->getAllLoops()) {
        wheels_[loop] = std::make_shared<TimingWheel>(loop, idleTimeout_);
      }
    }
    loop_->runInLoop([this] { acceptor_->listen(); });
  }
}

void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr) {
  EventLoop* ioLoop = threadPool_->getNextLoop();
  const InetAddress localAddr = getLocalAddr(sockfd);
  const std::string connName = name_ + "#" + ipPort_ + "#" + std::to_string(nextConnId_++);

  LOGINFO("TcpServer [{}] new connection [{}] from {}", name_, connName, peerAddr.toIpPort());

  auto conn = std::make_shared<TcpConnection>(ioLoop, connName, sockfd, localAddr, peerAddr);
  connections_.emplace(connName, conn);
  if (idleTimeout_ > 0) {
    auto wheel = wheels_[ioLoop];
    conn->setConnectionCallback([wheel, cb = connectionCallback_](const TcpConnectionPtr& c) {
      if (c->isConnected()) wheel->onConnection(c);
      else                  wheel->onClose(c);
      if (cb) cb(c);
    });
    conn->setMessageCallback([wheel, cb = messageCallback_](const TcpConnectionPtr& c, Buffer* buf, Timestamp ts) {
      wheel->onMessage(c, buf, ts);
      if (cb) cb(c, buf, ts);
    });
  } else {
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
  }
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback([this](const TcpConnectionPtr& c) { removeConnection(c); });
  ioLoop->runInLoop([conn] { conn->connectEstablished(); });
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
  loop_->runInLoop([this, conn] { removeConnectionInLoop(conn); });
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
  LOGINFO("TcpServer [{}] remove connection [{}]", name_, conn->getName());
  connections_.erase(conn->getName());
  conn->getLoop()->runInLoop([conn] { conn->connectDestroyed(); });
}

}  // namespace transport
