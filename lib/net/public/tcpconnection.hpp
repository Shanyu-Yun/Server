#pragma once
#include <any>
#include <atomic>
#include <memory>
#include <string>

#include "buffer.hpp"
#include "noncopyable.hpp"
#include "callbacks.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "socket.hpp"

namespace net {

/**
 * @brief TCP 连接当前所处的生命周期状态。
 */
enum class StateE : int { kDisconnected, kConnecting, kConnected, kDisconnecting };

/**
 * @brief 表示一条已建立或正在建立的 TCP 连接。
 *
 * TcpConnection 以 shared_ptr 形式在 TcpServer、Channel 回调、用户回调之间共享。
 * 它拥有 socket 的生命周期，并通过 channel_ 在所属 EventLoop 上监听读写事件。
 */
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection> {
 public:
  /**
   * @brief 构造一条 TCP 连接对象（由 TcpServer::newConnection 调用）。
   * @param loop      该连接所属的 IO EventLoop。
   * @param name      连接的唯一名称，用于日志。
   * @param sockfd    已 accept 的 socket 文件描述符。
   * @param localAddr 本端地址。
   * @param peerAddr  对端地址。
   */
  TcpConnection(EventLoop* loop, std::string name, int sockfd, const InetAddress& localAddr,
                const InetAddress& peerAddr);

  /**
   * @brief 析构 TcpConnection，断言连接已处于 kDisconnected 状态。
   */
  ~TcpConnection();

  /**
   * @brief 发送消息，可从任意线程调用。
   *
   * 若在 IO 线程则直接写；否则通过 sendInLoop 跨线程投递。
   * @param message 要发送的字符串数据。
   */
  void send(const std::string& message);

  /**
   * @brief 半关闭连接写端，触发 FIN 发送。
   *
   * 若输出缓冲区仍有数据，等待写完后再实际关闭。
   */
  void shutdown();

  /**
   * @brief 设置连接建立/断开时的回调。
   */
  void setConnectionCallback(const ConnectionCallback& cb) {
    connectionCallback_ = cb;
  }

  /**
   * @brief 设置有数据可读时的回调。
   */
  void setMessageCallback(const MessageCallback& cb) {
    messageCallback_ = cb;
  }

  /**
   * @brief 设置输出缓冲区清空时的回调。
   */
  void setWriteCompleteCallback(const WriteCompleteCallback& cb) {
    writeCompleteCallback_ = cb;
  }

  /**
   * @brief 设置输出缓冲区首次超过高水位时的回调。
   * @param cb            回调函数。
   * @param highWaterMark 高水位阈值（字节数）。
   */
  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark) {
    highWaterMarkCallback_ = cb;
    highWaterMark_ = highWaterMark;
  }

  /**
   * @brief 设置连接关闭时的回调（内部由 TcpServer 使用）。
   */
  void setCloseCallback(const CloseCallback& cb) {
    closeCallback_ = cb;
  }

  /**
   * @brief 返回所属 IO EventLoop。
   */
  EventLoop* getLoop() const {
    return loop_;
  }

  /**
   * @brief 返回连接名称。
   */
  const std::string& getName() const {
    return name_;
  }

  /**
   * @brief 返回本端地址。
   */
  const InetAddress& getLocalAddr() const {
    return localAddr_;
  }

  /**
   * @brief 返回对端地址。
   */
  const InetAddress& getPeerAddr() const {
    return peerAddr_;
  }

  /**
   * @brief 返回连接是否处于已建立状态。
   */
  bool isConnected() const {
    return state_ == StateE::kConnected;
  }

  /**
   * @brief 连接建立完成时由 TcpServer 调用，启用读事件并触发 connectionCallback_。
   *
   * 必须在所属 IO EventLoop 线程中调用。
   */
  void connectEstablished();

  /**
   * @brief 连接销毁时由 TcpServer 调用，从 Poller 注销 channel_。
   *
   * 必须在所属 IO EventLoop 线程中调用。
   */
  void connectDestroyed();

  /**
   * @brief 设置用户自定义的上下文数据，可以在回调中通过 getContext() 获取。
   * 
   * @param ctx 用户自定义的上下文数据，可以是任意类型。
   */
  inline void setContext(const std::any& ctx) {
    context_ = ctx;
  }

  /**
   * @brief 获取用户自定义的上下文数据，可以在回调中通过 setContext() 设置。 
   * 
   * @return 用户自定义的上下文数据，可能包含任意类型，可以在回调中通过 setContext() 设置。
  */
  inline const std::any& getContext() const {
    return context_;
  }

  /**
   * @brief 获取用户自定义的上下文数据的可修改引用，可以在回调中通过 setContext() 设置。 
   * 
   * @return 用户自定义的上下文数据的可修改引用，可能包含任意类型，可以在回调中通过 setContext() 设置。
   */
  inline std::any* getMutableContext() {
    return &context_;
  }

  /**
   * @brief 强制关闭连接，立即断开并触发 closeCallback_。
   * 
   */
  void forceClose();

  /**
   * @brief 强制关闭连接，延迟 seconds 秒后断开并触发 closeCallback_。
   * 
   * @param seconds 延迟秒数，单位为秒。
   */
  void forceCloseWithDelay(double seconds);

 private:
  EventLoop* loop_;            ///< 所属 IO 事件循环。
  const std::string name_;     ///< 连接唯一名称。
  std::atomic<StateE> state_;  ///< 连接当前状态，原子读写。
  bool reading_;               ///< 是否正在读（channel_ 是否已注册读事件）。

  std::unique_ptr<Socket> socket_;    ///< 持有 socket fd 的 RAII 对象。
  std::unique_ptr<Channel> channel_;  ///< socket fd 对应的 Channel，负责 IO 事件分发。

  const InetAddress localAddr_;  ///< 本端地址。
  const InetAddress peerAddr_;   ///< 对端地址。

  ConnectionCallback connectionCallback_;        ///< 连接建立/断开回调
  MessageCallback messageCallback_;              ///< 数据可读回调
  WriteCompleteCallback writeCompleteCallback_;  ///< 写完成回调
  HighWaterMarkCallback highWaterMarkCallback_;  ///< 高水位回调
  CloseCallback closeCallback_;                  ///< 关闭回调（TcpServer 内部）

  size_t highWaterMark_;  ///< 输出缓冲区高水位阈值（字节）。
  Buffer inputBuffer_;    ///< 输入缓冲区，缓存从 socket 读取的数据。
  Buffer outputBuffer_;   ///< 输出缓冲区，缓存待写入 socket 的数据。

  std::any context_;  ///< 用户自定义的上下文数据

  /**
   * @brief 处理 socket 可读事件，从 fd 读数据到 inputBuffer_。
   */
  void handleRead(Timestamp receiveTime);

  /**
   * @brief 处理 socket 可写事件，将 outputBuffer_ 中的数据写入 fd。
   */
  void handleWrite();

  /**
   * @brief 处理连接关闭事件，转换状态并触发 closeCallback_。
   */
  void handleClose();

  /**
   * @brief 处理 socket 错误事件，记录日志。
   */
  void handleError();

  /**
   * @brief 在 IO 线程中执行实际的数据发送逻辑。
   */
  void sendInLoop(const std::string& message);

  /**
   * @brief 在 IO 线程中执行实际的写端半关闭逻辑。
   */
  void shutdownInLoop();

  /**
   * @brief 在 IO 线程中执行实际的强制关闭连接逻辑。
   */
  void forceCloseInLoop();
};

}  // namespace net
