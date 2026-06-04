#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "net/acceptor.hpp"
#include "net/callbacks.hpp"
#include "net/eventloop.hpp"
#include "net/eventloopthreadpool.hpp"
#include "net/inetaddress.hpp"
#include "net/tcpconnection.hpp"  // IWYU pragma: keep
#include "net/timingwheel.hpp"

namespace net {

/**
 * @brief 监听端口复用选项。
 */
enum Option { kNoReusePort, kReusePort };

/**
 * @brief 面向用户的 TCP 服务器，封装 Acceptor、IO 线程池与连接管理。
 *
 * TcpServer 运行在用户提供的 main loop 上：Acceptor 在 main loop 中监听
 * 新连接，每条连接被分配到 threadPool_ 中的某个 IO loop，并在该 loop 中
 * 完成读写和回调。
 */
class TcpServer {
 public:
  /**
   * @brief 构造 TcpServer，初始化 Acceptor 和线程池，但不开始监听。
   * @param loop       main loop，用于接受新连接。
   * @param listenAddr 监听地址与端口。
   * @param name       服务器名称，用于日志和连接命名。
   * @param option     是否开启 SO_REUSEPORT。
   */
  TcpServer(EventLoop* loop, const InetAddress listenAddr, std::string name, Option option = kNoReusePort);

  /**
   * @brief 析构 TcpServer，销毁所有存活连接。
   */
  ~TcpServer();

  /**
   * @brief 启动线程池并开始监听，可多次调用但只生效一次。
   *
   * 内部通过 compare_exchange_strong 保证线程安全的一次性启动。
   */
  void start();

  /**
   * @brief 设置 IO 线程数量，必须在 start() 前调用。
   * @param numThreads IO 线程数，0 表示在 main loop 中处理所有连接。
   */
  void setThreadNum(int numThreads) {
    threadPool_->setThreadNum(numThreads);
  }

  /**
   * @brief 设置每个 IO 线程启动时的初始化回调。
   * @param cb 初始化回调，参数为该线程的 EventLoop 指针。
   */
  void setThreadInitCallback(const ThreadInitCallback& cb) {
    threadInitCallback_ = cb;
  }

  /**
   * @brief 设置连接建立/断开时的回调。
   * @param cb 回调函数，参数为对应的 TcpConnectionPtr。
   */
  void setConnectionCallback(const ConnectionCallback& cb) {
    connectionCallback_ = cb;
  }

  /**
   * @brief 设置有数据可读时的回调。
   * @param cb 回调函数，参数为连接指针、输入缓冲区、事件时间戳。
   */
  void setMessageCallback(const MessageCallback& cb) {
    messageCallback_ = cb;
  }

  /**
   * @brief 启用连接空闲超时，必须在 start() 前调用。
   *
   * 内部为每个 IO 线程创建一个 TimingWheel，空闲超过 seconds 秒的连接将被强制断开。
   * @param seconds 空闲超时秒数，0 表示禁用（默认）。
   */
  void setIdleTimeout(int seconds) {
    idleTimeout_ = seconds;
  }

 private:
  EventLoop* loop_;           ///< main loop，运行 Acceptor。
  const std::string name_;    ///< 服务器名称。
  const std::string ipPort_;  ///< 监听地址字符串，用于连接命名。

  std::unique_ptr<Acceptor> acceptor_;               ///< 监听 socket 的封装，负责 accept 新连接。
  std::shared_ptr<EventLoopThreadPool> threadPool_;  ///< IO 线程池，持有所有 IO EventLoop。

  ConnectionCallback connectionCallback_;        ///< 连接建立/断开回调
  MessageCallback messageCallback_;              ///< 数据可读回调
  WriteCompleteCallback writeCompleteCallback_;  ///< 写完成回调
  ThreadInitCallback threadInitCallback_;        ///< IO 线程初始化回调

  std::atomic<int> started_;  ///< start() 是否已执行的原子标志，防止重复启动。
  int nextConnId_;            ///< 自增连接 ID，用于生成唯一连接名称。
  std::unordered_multimap<std::string, TcpConnectionPtr> connections_;  ///< 已建立的所有连接，以连接名称为 key。

  int idleTimeout_ = 0;  ///< 空闲超时秒数，0 表示禁用。
  std::unordered_map<EventLoop*, std::shared_ptr<TimingWheel>> wheels_;  ///< 每个 IO loop 对应的时间轮。

  /**
   * @brief Acceptor 的新连接回调，在 main loop 中将连接分配给 IO loop。
   * @param sockfd   已 accept 的 socket fd。
   * @param peerAddr 对端地址。
   */
  void newConnection(int sockfd, const InetAddress& peerAddr);

  /**
   * @brief 连接关闭时的通知入口，将实际操作投递到 main loop。
   * @param conn 待移除的连接。
   */
  void removeConnection(const TcpConnectionPtr& conn);

  /**
   * @brief 在 main loop 中从 connections_ 移除连接并调用 connectDestroyed。
   * @param conn 待移除的连接。
   */
  void removeConnectionInLoop(const TcpConnectionPtr& conn);
};

}  // namespace net
