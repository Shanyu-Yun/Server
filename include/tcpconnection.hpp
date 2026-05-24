#pragma once
#include <cstddef>
#include <memory>

#include "buffer.hpp"
#include "callbacks.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "noncopyable.hpp"
#include "socket.hpp"

enum class StateE : int { kDisconnected, kConnecting, kConnected, kDisconnecting };

class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection> {
 public:
  TcpConnection(EventLoop* loop, std::string name, int sockfd, const InetAddress& localAddr,
                const InetAddress& peerAddr);
  ~TcpConnection();

  void send(const std::string& message);
  void shutdown();

  // ====== setters ======
  void setConnectionCallback(const ConnectionCallback& cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback& cb) {
    messageCallback_ = cb;
  }
  void setWriteCompleteCallback(const WriteCompleteCallback& cb) {
    writeCompleteCallback_ = cb;
  }
  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark) {
    highWaterMarkCallback_ = cb;
    highWaterMark_ = highWaterMark;
  }
  void setCloseCallback(const CloseCallback& cb) {
    closeCallback_ = cb;
  }

  // ======getters ======
  EventLoop* getLoop() const {
    return loop_;
  }

  const std::string& getName() const {
    return name_;
  }

  const InetAddress& getLocalAddr() const {
    return localAddr_;
  }

  const InetAddress& getPeerAddr() const {
    return peerAddr_;
  }

  bool isConnected() const {
    return state_ == StateE::kConnected;
  }

  void connectEstablished();
  void connectDestroyed();

 private:
  EventLoop* loop_;
  const std::string name_;
  std::atomic<StateE> state_;
  bool reading_;

  std::unique_ptr<Socket> socket_;
  std::unique_ptr<Channel> channel_;

  const InetAddress localAddr_;
  const InetAddress peerAddr_;

  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  HighWaterMarkCallback highWaterMarkCallback_;
  CloseCallback closeCallback_;
  size_t highWaterMark_;

  Buffer inputBuffer_;
  Buffer outputBuffer_;

 private:
  void handleRead(Timestamp receiveTime);
  void handleWrite();
  void handleClose();
  void handleError();
  void sendInLoop(const std::string& message);
  void shutdownInLoop();
};
