#pragma once

#include <functional>

#include "net/channel.hpp"
#include "net/eventloop.hpp"
#include "net/socket.hpp"

namespace tinynet {

/**
 * @brief 负责监听端口并接受新连接。
 *
 * Acceptor 持有一个处于监听状态的 Socket，并通过 acceptChannel_ 在
 * EventLoop 中等待可读事件。每当 epoll 通知有新连接时，调用 accept()
 * 并通过 newConnectionCallback_ 将已连接 sockfd 交给上层（TcpServer）。
 */
class Acceptor {
 public:
  /**
   * @brief 有新连接到来时通知上层的回调类型。
   *
   * 参数为已接受连接的 sockfd 和对端地址。
   */
  using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;

  /**
   * @brief 构造 Acceptor，创建监听 socket 并绑定地址。
   * @param loop       所属的事件循环（通常是 TcpServer 的 main loop）。
   * @param listenAddr 监听的本地地址与端口。
   * @param reuseport  是否开启 SO_REUSEPORT。
   */
  Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);

  /**
   * @brief 析构 Acceptor，禁用 acceptChannel_ 并关闭 acceptSocket_。
   */
  ~Acceptor();

  /**
   * @brief 设置新连接到来时的通知回调。
   * @param cb 由 TcpServer 提供的连接分发回调。
   */
  void setNewConnectionCallback(const NewConnectionCallback& cb) {
    newConnectionCallback_ = cb;
  }

  /**
   * @brief 返回是否已开始监听。
   */
  bool listenning() const {
    return listenning_;
  }

  /**
   * @brief 调用 Socket::listen() 并在 Poller 中启用读事件监听。
   *
   * 必须在所属 EventLoop 线程中调用（由 TcpServer::start 通过 runInLoop 保证）。
   */
  void listen();

 private:
  /**
   * @brief 处理 acceptChannel_ 上的可读事件，调用 accept 并通知上层。
   */
  void handleRead();

  EventLoop* loop_;                              ///< 所属事件循环。
  Socket acceptSocket_;                          ///< 监听 socket 的 RAII 封装。
  Channel acceptChannel_;                        ///< 监听 socket 对应的 Channel，注册读事件。
  bool listenning_;                              ///< 是否已调用 listen()。
  NewConnectionCallback newConnectionCallback_;  ///< 新连接到来时调用的回调，由 TcpServer 设置。
};

}  // namespace tinynet
