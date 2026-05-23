#include <functional>

#include "eventloop.hpp"
#include "socket.hpp"
class Acceptor {
 public:
  Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
  ~Acceptor();

  void setNewConnectionCallback(const std::function<void(int sockfd, const InetAddress&)>& cb) {
    newConnectionCallback_ = cb;
  }

  bool listenning() const {
    return listenning_;
  }
  void listen();

 private:
  void handleRead();

  EventLoop* loop_;
  Socket acceptSocket_;
  Channel acceptChannel_;
  bool listenning_;

  std::function<void(int sockfd, const InetAddress&)> newConnectionCallback_;
};