#include "net/tcpconnection.hpp"

#include <unistd.h>

#include <cerrno>

#include "base/logger.hpp"
#include "base/timestamp.hpp"
#include "net/socket.hpp"

namespace net {

TcpConnection::TcpConnection(EventLoop* loop, std::string name, int sockfd, const InetAddress& localAddr,
                             const InetAddress& peerAddr)
    : loop_(loop),
      name_(std::move(name)),
      state_(StateE::kConnecting),
      reading_(true),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64 * 1024 * 1024) {
  socket_ = std::make_unique<Socket>(sockfd);
  channel_ = std::make_unique<Channel>(loop_, sockfd);
  channel_->setReadCallback([this](Timestamp t) { handleRead(t); });
  channel_->setWriteCallback([this]() { handleWrite(); });
  channel_->setCloseCallback([this]() { handleClose(); });
  channel_->setErrorCallback([this]() { handleError(); });
}

TcpConnection::~TcpConnection() {
  if (state_ == StateE::kConnected)
    LOGERROR("TcpConnection::~TcpConnection() [{}] still connected", name_);
  LOGINFO("TcpConnection::~TcpConnection() [{}] at {}", name_, static_cast<const void*>(this));
}

void TcpConnection::send(const std::string& message) {
  if (state_ == StateE::kConnected) {
    if (loop_->isInLoopThread())
      sendInLoop(message);
    else
      loop_->runInLoop([self = shared_from_this(), message]() { self->sendInLoop(message); });
  }
}

void TcpConnection::shutdown() {
  if (state_ == StateE::kConnected) {
    state_ = StateE::kDisconnecting;
    loop_->runInLoop([self = shared_from_this()]() { self->shutdownInLoop(); });
  }
}

void TcpConnection::connectEstablished() {
  state_ = StateE::kConnected;
  channel_->tie(shared_from_this());
  channel_->enableReading();
  if (connectionCallback_)
    connectionCallback_(shared_from_this());
  LOGINFO("TcpConnection::connectEstablished [{}] at {}", name_, static_cast<const void*>(this));
}

void TcpConnection::connectDestroyed() {
  if (state_ != StateE::kDisconnected) {
    state_ = StateE::kDisconnected;
    channel_->disableAll();
    channel_->remove();
    if (connectionCallback_)
      connectionCallback_(shared_from_this());
    LOGINFO("TcpConnection::connectDestroyed [{}] at {}", name_, static_cast<const void*>(this));
  }
}

void TcpConnection::forceClose() {
  if (state_ == StateE::kConnected || state_ == StateE::kDisconnecting) {
    state_ = StateE::kDisconnecting;
    loop_->runInLoop([self = shared_from_this()]() { self->forceCloseInLoop(); });
  }
}

void TcpConnection::forceCloseWithDelay(double seconds) {
  if (state_ == StateE::kConnected || state_ == StateE::kDisconnecting) {
    state_ = StateE::kDisconnecting;
    loop_->runAfter(seconds, [self = shared_from_this()]() { self->forceCloseInLoop(); });
  }
}

void TcpConnection::handleRead(Timestamp receiveTime) {
  int savedErrno = 0;
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if (n > 0)
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  else if (n == 0)
    handleClose();
  else {
    errno = savedErrno;
    LOGERROR("TcpConnection::handleRead() error");
    handleError();
  }
}

void TcpConnection::handleWrite() {
  int savedErrno = 0;
  const ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
  if (n > 0) {
    if (outputBuffer_.readableBytes() == 0) {
      channel_->disableWriting();
      if (writeCompleteCallback_)
        writeCompleteCallback_(shared_from_this());
      if (state_ == StateE::kDisconnecting)
        shutdownInLoop();
    }
  } else {
    errno = savedErrno;
    LOGERROR("TcpConnection::handleWrite() error");
    handleError();
  }
}

void TcpConnection::handleClose() {
  state_ = StateE::kDisconnected;
  channel_->disableAll();
  channel_->remove();
  LOGINFO("TcpConnection::handleClose [{}] at {}", name_, static_cast<const void*>(this));
  if (connectionCallback_)
    connectionCallback_(shared_from_this());
  if (closeCallback_)
    closeCallback_(shared_from_this());
}

void TcpConnection::handleError() {
  LOGERROR("TcpConnection::handleError [{}] - SO_ERROR", name_);
}

void TcpConnection::sendInLoop(const std::string& message) {
  ssize_t nwrote = 0;
  size_t remaining = message.size();
  bool faultError = false;

  // 连接已断开，放弃发送
  if (state_ == StateE::kDisconnected) {
    LOGERROR("TcpConnection::sendInLoop() disconnected, give up writing");
    return;
  }

  // 单次发送超过高水位：几乎必然是调用方 bug，拒绝而非静默堆积
  if (message.size() > highWaterMark_) {
    LOGERROR("TcpConnection::sendInLoop() [{}] message too large: {} bytes (highWaterMark {})",
             name_, message.size(), highWaterMark_);
    return;
  }

  // 如果没有正在写且输出缓冲区没有待发送数据，尝试直接写入 socket
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    // 直接写入数据到 socket，减少一次内核拷贝
    nwrote = ::write(channel_->fd(), message.data(), message.size());
    if (nwrote >= 0) {
      remaining = message.size() - static_cast<size_t>(nwrote);
      if (remaining == 0 && writeCompleteCallback_)
        loop_->queueInLoop([self = shared_from_this()] { self->writeCompleteCallback_(self); });
    } else {
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        LOGERROR("TcpConnection::sendInLoop() write error: {}", errno);
        if (errno == EPIPE || errno == ECONNRESET)
          faultError = true;
      }
    }
  }

  // 检查是否有剩余数据需要发送，或之前写入时发生了错误
  if (!faultError && remaining > 0) {
    const size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_) {
      loop_->queueInLoop(
          [self = shared_from_this(), len = oldLen + remaining] { self->highWaterMarkCallback_(self, len); });
    }
    outputBuffer_.append(message.data() + nwrote, remaining);
    if (!channel_->isWriting())
      channel_->enableWriting();
  }
}

void TcpConnection::shutdownInLoop() {
  if (state_ == StateE::kDisconnecting && !channel_->isWriting())
    socket_->shutdownWrite();
}

void TcpConnection::forceCloseInLoop() {
  handleClose();
}
}  // namespace net
