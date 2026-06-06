#include "udpserver.hpp"

#include <sys/socket.h>

#include <cerrno>

#include "logger.hpp"
#include "eventloop.hpp"

namespace transport {

namespace {

int createUdpSocket() {
  int sockfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sockfd < 0) {
    LOGERROR("UdpServer: create socket error");
    abort();
  }
  return sockfd;
}

}  // namespace

UdpServer::UdpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name)
    : loop_(loop),
      name_(std::move(name)),
      socket_(std::make_unique<Socket>(createUdpSocket())),
      channel_(std::make_unique<Channel>(loop, socket_->fd())) {
  socket_->setReuseAddr(true);
  socket_->bindAddress(listenAddr);
  channel_->setReadCallback([this](Timestamp t) { handleRead(t); });
  channel_->setWriteCallback([this]() { handleWrite(); });
}

UdpServer::~UdpServer() {
  channel_->disableAll();
  channel_->remove();
}

void UdpServer::start() {
  loop_->runInLoop([this] { channel_->enableReading(); });
}

void UdpServer::sendTo(std::string data, const InetAddress& peer) {
  if (loop_->isInLoopThread()) {
    sendInLoop(std::move(data), peer);
  } else {
    // 跨线程：data/peer 拷进 lambda，由 IO 线程执行实际发送
    loop_->runInLoop([this, d = std::move(data), peer]() mutable { sendInLoop(std::move(d), peer); });
  }
}

void UdpServer::handleRead(Timestamp receiveTime) {
  // 循环排空内核接收队列：一次可读事件可能堆积多条报文。
  // 非阻塞 socket 在队列读空时返回 EAGAIN，循环随之结束。
  while (true) {
    sockaddr_in peerAddr{};
    socklen_t addrLen = sizeof(peerAddr);
    ssize_t n = ::recvfrom(socket_->fd(), recvBuf_, sizeof(recvBuf_), 0,
                           reinterpret_cast<sockaddr*>(&peerAddr), &addrLen);
    if (n >= 0) {
      // n == 0 是合法的空报文；data 为对 recvBuf_ 的零拷贝视图，仅在本次回调内有效
      if (messageCallback_)
        messageCallback_(this, std::string_view(recvBuf_, static_cast<size_t>(n)), InetAddress(peerAddr),
                         receiveTime);
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;  // 内核队列已排空
    } else if (errno == EINTR) {
      continue;  // 被信号打断，重试
    } else {
      LOGERROR("UdpServer::handleRead() recvfrom error {}", errno);
      break;
    }
  }
}

void UdpServer::sendInLoop(std::string data, const InetAddress& peer) {
  // IPv4 UDP payload 上限：65535 - 20(IP头) - 8(UDP头) = 65507 字节
  static constexpr size_t kMaxUdpPayload = 65507;
  if (data.size() > kMaxUdpPayload) {
    LOGERROR("UdpServer::sendInLoop() datagram too large: {} bytes (max {})", data.size(), kMaxUdpPayload);
    return;
  }

  // 快路径：队列为空时直接 sendto，省掉一次 EPOLLOUT 往返
  if (sendQueue_.empty()) {
    ssize_t n = ::sendto(socket_->fd(), data.data(), data.size(), 0,
                         reinterpret_cast<const sockaddr*>(peer.getSockAddr()), sizeof(sockaddr_in));
    if (n >= 0)
      return;  // UDP 报文原子发出，完成
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      // 硬错误（EMSGSIZE/ENOBUFS/ECONNREFUSED 等）重试无意义，直接丢弃
      LOGERROR("UdpServer::sendInLoop() sendto error {}", errno);
      return;
    }
    // EWOULDBLOCK：内核发送缓冲满，落到入队逻辑
  }

  // 队列满：head-drop 丢最旧的，给新报文腾位（新报文比旧报文更值得保留）
  if (sendQueue_.size() >= maxQueueSize_) {
    sendQueue_.pop_front();
    ++droppedCount_;
  }
  sendQueue_.push_back({std::move(data), peer});
  if (!channel_->isWriting())  // 仅在有积压时开 EPOLLOUT，否则 socket 恒可写会导致空转
    channel_->enableWriting();
}

void UdpServer::handleWrite() {
  while (!sendQueue_.empty()) {
    Datagram& dg = sendQueue_.front();
    ssize_t n = ::sendto(socket_->fd(), dg.data.data(), dg.data.size(), 0,
                         reinterpret_cast<const sockaddr*>(dg.peer.getSockAddr()), sizeof(sockaddr_in));
    if (n >= 0) {
      sendQueue_.pop_front();  // 整报文发出才出队，无偏移可记
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;  // 内核缓冲仍满，等下次 EPOLLOUT
    } else {
      LOGERROR("UdpServer::handleWrite() sendto error {}", errno);
      sendQueue_.pop_front();  // 硬错误：丢这一包，继续发后面的
    }
  }
  if (sendQueue_.empty())
    channel_->disableWriting();  // 排空立即关 EPOLLOUT，避免空转
}

}  // namespace transport
