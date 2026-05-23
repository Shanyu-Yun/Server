
#pragma once

#include <sys/epoll.h>

#include <functional>
#include <memory>
#include <utility>

#include "noncopyable.hpp"
#include "timestamp.hpp"

class EventLoop;

/**
 * @brief Channel 不关注任何事件。
 */
inline constexpr int kNoneEvent = 0;

/**
 * @brief Channel 关注可读事件，包括普通读和高优先级数据。
 */
inline constexpr int kReadEvent = EPOLLIN | EPOLLPRI;

/**
 * @brief Channel 关注可写事件。
 */
inline constexpr int kWriteEvent = EPOLLOUT;

/**
 * @brief 封装一个 fd 及其关心的 IO 事件。
 *
 * Channel 只负责记录 fd、关注的事件、Poller 返回的实际事件，
 * 并在事件发生时分发到对应回调。Channel 不拥有 fd，fd 的生命周期
 * 由 Socket、Acceptor 或 EventLoop 等外部对象管理。
 */
class Channel : noncopyable {
 public:
  /**
   * @brief 不带时间戳的事件回调。
   */
  using EventCallback = std::function<void()>;

  /**
   * @brief 读事件回调，参数为 Poller 返回事件时的时间戳。
   */
  using ReadEventCallback = std::function<void(Timestamp)>;

  /**
   * @brief 构造一个绑定到指定 EventLoop 和 fd 的 Channel。
   * @param loop 所属 EventLoop。
   * @param fd Channel 监听的文件描述符。
   */
  Channel(EventLoop* loop, int fd);

  /**
   * @brief 析构 Channel。
   *
   * Channel 不拥有 fd，因此析构时不关闭 fd。
   */
  ~Channel();

  /**
   * @brief 设置读事件回调。
   */
  inline void setReadCallback(ReadEventCallback cb) {
    readCallback_ = std::move(cb);
  }

  /**
   * @brief 设置写事件回调。
   */
  inline void setWriteCallback(EventCallback cb) {
    writeCallback_ = std::move(cb);
  }

  /**
   * @brief 设置连接关闭事件回调。
   */
  inline void setCloseCallback(EventCallback cb) {
    closeCallback_ = std::move(cb);
  }

  /**
   * @brief 设置错误事件回调。
   */
  inline void setErrorCallback(EventCallback cb) {
    errorCallback_ = std::move(cb);
  }

  /**
   * @brief 开启读事件监听。
   */
  inline void enableReading() {
    events_ |= kReadEvent;
    update();
  }

  /**
   * @brief 关闭读事件监听。
   */
  inline void disableReading() {
    events_ &= ~kReadEvent;
    update();
  }

  /**
   * @brief 开启写事件监听。
   */
  inline void enableWriting() {
    events_ |= kWriteEvent;
    update();
  }

  /**
   * @brief 关闭写事件监听。
   */
  inline void disableWriting() {
    events_ &= ~kWriteEvent;
    update();
  }

  /**
   * @brief 关闭所有事件监听。
   */
  inline void disableAll() {
    events_ = kNoneEvent;
    update();
  }

  /**
   * @brief 判断当前是否没有关注任何事件。
   */
  inline bool isNoneEvent() const {
    return events_ == kNoneEvent;
  }

  /**
   * @brief 判断当前是否关注写事件。
   */
  inline bool isWriting() const {
    return (events_ & kWriteEvent) != 0;
  }

  /**
   * @brief 判断当前是否关注读事件。
   */
  inline bool isReading() const {
    return (events_ & kReadEvent) != 0;
  }

  /**
   * @brief 绑定 Channel 的 owner，避免事件处理过程中 owner 被析构。
   * @param owner 通常指向 TcpConnection 的 shared_ptr。
   */
  void tie(const std::shared_ptr<void>& owner);

  /**
   * @brief 通知所属 EventLoop 更新该 Channel 在 Poller 中的注册状态。
   */
  void update();

  /**
   * @brief 根据 Poller 返回的事件分发回调。
   * @param receiveTime Poller 返回活跃事件时的时间戳。
   */
  void handleEvent(Timestamp receiveTime);

  /**
   * @brief 从所属 EventLoop/Poller 中移除该 Channel。
   */
  void remove();

  /**
   * @brief 获取 Channel 在 Poller 中的注册状态。
   */
  inline int pollerState() const {
    return pollerState_;
  }

  /**
   * @brief 获取 Channel 监听的文件描述符。
   */
  inline int fd() const {
    return fd_;
  }

  /**
   * @brief 获取当前关注的事件集合。
   */
  inline int events() const {
    return events_;
  }

  /**
   * @brief 设置 Poller 返回的实际发生事件集合。
   */
  inline void setRevents(int revents) {
    revents_ = revents;
  }

  /**
   * @brief 设置 Channel 在 Poller 中的注册状态。
   */
  inline void setPollerState(int state) {
    pollerState_ = state;
  }

 private:
  /**
   * @brief 所属事件循环。
   */
  EventLoop* loop_;

  /**
   * @brief 监听的文件描述符。Channel 不拥有该 fd。
   */
  const int fd_;

  /**
   * @brief 当前关注的事件集合。
   */
  int events_;

  /**
   * @brief Poller 返回的实际发生事件集合。
   */
  int revents_;

  /**
   * @brief Poller 内部使用的 Channel 注册状态。
   */
  int pollerState_;

  /**
   * @brief 是否已经绑定 owner。
   */
  bool tied_;

  /**
   * @brief 弱引用 owner，通常指向 TcpConnection。
   * @note 为什么使用void而不是具体类型？因为 Channel 不需要知道 owner 的具体类型，使用 void 可以降低耦合。
   * 只有在 handleEvent 时才需要提升为 shared_ptr 来保证 owner 的生命周期。
   */
  std::weak_ptr<void> tie_;

  /**
   * @brief 读事件回调。
   */
  ReadEventCallback readCallback_;

  /**
   * @brief 写事件回调。
   */
  EventCallback writeCallback_;

  /**
   * @brief 关闭事件回调。
   */
  EventCallback closeCallback_;

  /**
   * @brief 错误事件回调。
   */
  EventCallback errorCallback_;

  /**
   * @brief 在锁保护下处理事件。
   * @param receiveTime Poller 返回活跃事件时的时间戳。
   */
  void handleEventWithGuard(Timestamp receiveTime);
};
