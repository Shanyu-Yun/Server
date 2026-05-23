#pragma once

#include <functional>

#include "channel.hpp"
#include "eventloop.hpp"
#include "socket.hpp"

/**
 * @brief 负责监听端口并接受新连接。
 *
 * Acceptor 持有一个监听 socket，并通过 acceptChannel_ 将该 socket 的
 * 可读事件注册到 EventLoop。每当有新连接到来时，Acceptor 接受连接
 * 并通过 NewConnectionCallback 把已连接 fd 和对端地址交给上层处理。
 */
class Acceptor {
 public:
  /**
   * @brief 新连接回调类型。
   *
   * 第一个参数是 accept 返回的已连接 socket fd，第二个参数是对端地址。
   */
  using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;

  /**
   * @brief 创建一个监听指定地址的 Acceptor。
   * @param loop 所属事件循环。
   * @param listenAddr 监听地址。
   * @param reuseport 是否开启 SO_REUSEPORT。
   */
  Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);

  /**
   * @brief 析构 Acceptor，并从 EventLoop/Poller 中移除监听 Channel。
   */
  ~Acceptor();

  /**
   * @brief 设置新连接到达时的回调。
   * @param cb 新连接回调。
   */
  void setNewConnectionCallback(const NewConnectionCallback& cb) {
    newConnectionCallback_ = cb;
  }

  /**
   * @brief 判断当前是否已经开始监听。
   * @return 已调用 listen() 时返回 true，否则返回 false。
   */
  bool listenning() const {
    return listenning_;
  }

  /**
   * @brief 开始监听并关注监听 socket 的可读事件。
   */
  void listen();

 private:
  /**
   * @brief 处理监听 socket 的可读事件，接受一个新连接。
   */
  void handleRead();

  /**
   * @brief 所属事件循环。
   */
  EventLoop* loop_;

  /**
   * @brief 监听 socket，拥有监听 fd。
   */
  Socket acceptSocket_;

  /**
   * @brief 监听 socket 对应的 Channel，不拥有 fd。
   */
  Channel acceptChannel_;

  /**
   * @brief 是否已经调用 listen()。
   */
  bool listenning_;

  /**
   * @brief 新连接到达时调用的回调。
   */
  NewConnectionCallback newConnectionCallback_;
};
