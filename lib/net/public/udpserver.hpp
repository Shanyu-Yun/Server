#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "noncopyable.hpp"
#include "timestamp.hpp"
#include "channel.hpp"
#include "inetaddress.hpp"
#include "socket.hpp"

namespace net {
class EventLoop;
class UdpServer;
using UdpServerPtr = std::shared_ptr<UdpServer>;

/**
 * @brief 收到一条 UDP 报文时触发。
 *
 * data 是对内部接收缓冲区的零拷贝视图，仅在回调返回前有效；
 * 若需异步持有，请自行拷贝为 std::string。peer 为报文源地址。
 */
using UdpMessageCallback =
    std::function<void(UdpServer* server, std::string_view data, const InetAddress& peer, Timestamp receiveTime)>;

/**
 * @brief 面向无连接数据报的 UDP 服务器。
 *
 * 持有单个未连接的 SOCK_DGRAM socket：recvfrom 收取任意对端的报文并回调，
 * sendTo 向任意对端发送报文。发送采用「整报文 + 目标地址」队列：内核发送
 * 缓冲满时报文入队并注册 EPOLLOUT，待可写时排空；队列满时按 head-drop
 * 丢弃最旧报文（UDP 语义允许丢包），丢弃计数由 droppedCount() 暴露。
 */
class UdpServer : noncopyable {
 public:
  /**
   * @brief 构造 UdpServer，创建并绑定 SOCK_DGRAM socket，但不开始收发。
   * @param loop       所属 EventLoop。
   * @param listenAddr 本地监听地址与端口。
   * @param name       服务器名称，用于日志。
   */
  UdpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name);

  /**
   * @brief 析构 UdpServer，从 Poller 注销 channel_ 后关闭 socket。
   */
  ~UdpServer();

  /**
   * @brief 启用读事件，开始接收报文。
   */
  void start();

  /**
   * @brief 设置收到报文时的回调。
   */
  void setMessageCallback(UdpMessageCallback cb) {
    messageCallback_ = std::move(cb);
  }

  /**
   * @brief 向指定对端发送一条报文，可从任意线程调用。
   *
   * 在 IO 线程则直接走 sendInLoop；否则跨线程投递。值传入 data 便于
   * move 进发送队列，避免缓冲时的额外拷贝。
   * @param data 报文负载。
   * @param peer 目标对端地址。
   */
  void sendTo(std::string data, const InetAddress& peer);

  /**
   * @brief 设置发送队列的最大积压报文数，超出时触发 head-drop。
   * @param n 队列上限（报文条数）。
   */
  void setMaxQueueSize(size_t n) {
    maxQueueSize_ = n;
  }

  /**
   * @brief 返回因队列溢出而被丢弃的报文累计数，用于观测发送压力。
   */
  size_t droppedCount() const {
    return droppedCount_;
  }

 private:
  /**
   * @brief 处理可读事件，recvfrom 一条报文并触发 messageCallback_。
   */
  void handleRead(Timestamp receiveTime);

  /**
   * @brief 处理可写事件，按序 sendto 排空 sendQueue_，遇 EWOULDBLOCK 即止。
   */
  void handleWrite();

  /**
   * @brief 在 IO 线程中执行实际发送：快路径直发，发不动则入队（必要时 head-drop）。
   */
  void sendInLoop(std::string data, const InetAddress& peer);

  /**
   * @brief 待发送的单条报文：整报文负载与其目标地址绑定。
   */
  struct Datagram {
    std::string data;   ///< 报文负载（拥有一份拷贝）。
    InetAddress peer;   ///< 目标对端地址。
  };

  EventLoop* loop_;                   ///< 所属事件循环。
  std::string name_;                  ///< 服务器名称。
  std::unique_ptr<Socket> socket_;    ///< 持有 SOCK_DGRAM fd 的 RAII 对象。
  std::unique_ptr<Channel> channel_;  ///< socket fd 对应的 Channel。
  UdpMessageCallback messageCallback_;  ///< 收到报文回调。

  std::deque<Datagram> sendQueue_;    ///< 待发送报文队列（整报文 + 地址），非字节流缓冲。
  size_t maxQueueSize_ = 1024;        ///< 发送队列上限，超出触发 head-drop。
  size_t droppedCount_ = 0;           ///< 因队列溢出被丢弃的报文累计数。

  char recvBuf_[64 * 1024];           ///< 单条报文接收缓冲（UDP 理论上限 64KB），供回调零拷贝视图借用。
};
}  // namespace net
