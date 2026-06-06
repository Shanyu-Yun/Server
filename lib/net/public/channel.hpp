#pragma once

#include <sys/epoll.h>

#include <functional>
#include <memory>
#include <utility>

#include "noncopyable.hpp"
#include "timestamp.hpp"

namespace net {

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
   * @brief 无参回调类型，用于写、关闭、错误事件。
   */
  using EventCallback = std::function<void()>;

  /**
   * @brief 读事件回调类型，携带 Poller 返回时间戳。
   */
  using ReadEventCallback = std::function<void(Timestamp)>;

  /**
   * @brief 构造一个绑定到指定 EventLoop 和 fd 的 Channel。
   * @param loop 所属事件循环。
   * @param fd   监听的文件描述符，Channel 不拥有其生命周期。
   */
  Channel(EventLoop* loop, int fd);

  /**
   * @brief 析构 Channel，此时 Channel 必须已从 Poller 注销。
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
   * @brief 设置连接关闭回调。
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
   * @brief 注册读事件并更新 Poller。
   */
  inline void enableReading() {
    events_ |= kReadEvent;
    update();
  }

  /**
   * @brief 取消读事件并更新 Poller。
   */
  inline void disableReading() {
    events_ &= ~kReadEvent;
    update();
  }

  /**
   * @brief 注册写事件并更新 Poller。
   */
  inline void enableWriting() {
    events_ |= kWriteEvent;
    update();
  }

  /**
   * @brief 取消写事件并更新 Poller。
   */
  inline void disableWriting() {
    events_ &= ~kWriteEvent;
    update();
  }

  /**
   * @brief 取消所有关注的事件并更新 Poller。
   */
  inline void disableAll() {
    events_ = kNoneEvent;
    update();
  }

  /**
   * @brief 返回是否未关注任何事件。
   */
  inline bool isNoneEvent() const {
    return events_ == kNoneEvent;
  }

  /**
   * @brief 返回是否正在关注写事件。
   */
  inline bool isWriting() const {
    return (events_ & kWriteEvent) != 0;
  }

  /**
   * @brief 返回是否正在关注读事件。
   */
  inline bool isReading() const {
    return (events_ & kReadEvent) != 0;
  }

  /**
   * @brief 绑定 Channel 的 owner，避免事件处理过程中 owner 被析构。
   * @param owner owner 的 shared_ptr，Channel 内部持有 weak_ptr。
   */
  void tie(const std::shared_ptr<void>& owner);

  /**
   * @brief 通知所属 EventLoop 更新该 Channel 在 Poller 中的注册状态。
   */
  void update();

  /**
   * @brief 根据 Poller 返回的活跃事件分发到对应回调。
   * @param receiveTime Poller 返回时的时间戳。
   */
  void handleEvent(Timestamp receiveTime);

  /**
   * @brief 从所属 EventLoop/Poller 中注销并移除该 Channel。
   */
  void remove();

  /**
   * @brief 返回 Poller 中的注册状态（kNew/kAdded/kDeleted）。
   */
  inline int pollerState() const {
    return pollerState_;
  }

  /**
   * @brief 返回关联的文件描述符。
   */
  inline int fd() const {
    return fd_;
  }

  /**
   * @brief 返回当前关注的事件掩码。
   */
  inline int events() const {
    return events_;
  }

  /**
   * @brief 由 Poller 设置本次实际发生的事件。
   */
  inline void setRevents(int revents) {
    revents_ = revents;
  }

  /**
   * @brief 由 Poller 更新 Channel 的注册状态。
   */
  inline void setPollerState(int state) {
    pollerState_ = state;
  }

 private:
  EventLoop* loop_;          ///< 所属的事件循环。
  const int fd_;             ///< 关联的文件描述符，不由 Channel 负责关闭。
  int events_;               ///< 当前关注的事件掩码（EPOLLIN/EPOLLOUT 等）。
  int revents_;              ///< Poller 返回的本次活跃事件掩码。
  int pollerState_;          ///< 在 Poller 中的状态：kNew / kAdded / kDeleted。
  bool tied_;                ///< 是否已调用过 tie()。
  std::weak_ptr<void> tie_;  ///< owner 的弱引用，用于延长事件处理期间的生命周期。

  ReadEventCallback readCallback_;  ///< 读事件回调
  EventCallback writeCallback_;     ///< 写事件回调
  EventCallback closeCallback_;     ///< 关闭事件回调
  EventCallback errorCallback_;     ///< 错误事件回调

  /**
   * @brief 在持有 tie_ 的前提下真正执行事件分发。
   */
  void handleEventWithGuard(Timestamp receiveTime);
};

}  // namespace net
