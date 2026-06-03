#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "base/currentthread.hpp"
#include "base/timestamp.hpp"
#include "net/channel.hpp"
#include "net/poller.hpp"
#include "net/timer.hpp"

namespace tinynet {

class TimerQueue;
/**
 * @brief 事件循环，负责在所属线程中等待 IO 事件并分发回调。
 *
 * 每个 EventLoop 只属于一个线程，通过 Poller 等待活跃 Channel，
 * 再调用 Channel 分发对应回调。也支持跨线程通过 runInLoop/queueInLoop
 * 投递任务，内部使用 eventfd 实现跨线程唤醒。
 */
class EventLoop {
 public:
  /**
   * @brief 可投递给事件循环的任务类型。
   */
  using Functor = std::function<void()>;

  /**
   * @brief 构造 EventLoop，绑定到当前线程。
   */
  EventLoop();

  /**
   * @brief 析构 EventLoop，关闭 wakeupFd。
   */
  ~EventLoop();

  /**
   * @brief 启动事件循环，阻塞直到 quit() 被调用。
   *
   * 必须在创建 EventLoop 的线程中调用。
   */
  void loop();

  /**
   * @brief 退出事件循环。
   *
   * 可从任意线程调用；若从非 loop 线程调用，会通过 wakeupFd 唤醒 loop 线程。
   */
  void quit();

  /**
   * @brief 在 EventLoop 线程中执行回调。
   *
   * 若当前已在 loop 线程则立即执行，否则转为 queueInLoop。
   * @param cb 待执行的任务。
   */
  void runInLoop(const Functor& cb);

  /**
   * @brief 将回调加入待执行队列，由 loop 线程在下一轮事件处理后执行。
   * @param cb 待入队的任务。
   */
  void queueInLoop(const Functor& cb);

  /**
   * @brief 将 Channel 的事件变化同步到 Poller。
   * @param channel 需要更新的 Channel。
   */
  void updateChannel(Channel* channel);

  /**
   * @brief 从 Poller 中注销并移除 Channel。
   * @param channel 需要移除的 Channel。
   */
  void removeChannel(Channel* channel);

  /**
   * @brief 检查 Channel 是否已在 Poller 中注册。
   * @param channel 要查询的 Channel。
   */
  bool hasChannel(Channel* channel) const;

  /**
   * @brief 判断当前调用是否在 EventLoop 所属线程中。
   * @return 是返回 true，否返回 false。
   */
  bool isInLoopThread() const {
    return threadId_ == CurrentThread::tid();
  }

  TimerId runAt(Timestamp time, TimerCallback cb);

  TimerId runAfter(double delay, TimerCallback cb);

  TimerId runEvery(double interval, TimerCallback cb);

  void cancel(TimerId timerId);

 private:
  /**
   * @brief 向 wakeupFd 写一个字节，唤醒阻塞在 poll 中的 loop 线程。
   */
  void wakeup();

  /**
   * @brief 读取 wakeupFd 的数据，清除可读事件。
   */
  void handleRead();

  /**
   * @brief 逐个执行 pendingFunctors_ 中的任务。
   */
  void doPendingFunctors();

  std::atomic<bool> looping_;                 ///< 事件循环是否正在运行。
  std::atomic<bool> quit_;                    ///< 是否已请求退出。
  std::atomic<bool> callingPendingFunctors_;  ///< 是否正在处理 pendingFunctors_。

  const pid_t threadId_;      ///< 创建该 EventLoop 的线程 ID，用于线程安全断言。
  Timestamp pollReturnTime_;  ///< 上一次 Poller::poll 返回的时间戳。

  std::unique_ptr<Poller> poller_;      ///< 底层 IO 多路复用器。
  Poller::ChannelList activeChannels_;  ///< 本轮 poll 返回的活跃 Channel 列表。

  int wakeupFd_;                            ///< 用于跨线程唤醒的 eventfd 文件描述符。
  std::unique_ptr<Channel> wakeupChannel_;  ///< 封装 wakeupFd_ 的 Channel。

  std::mutex mutex_;                      ///< 保护 pendingFunctors_ 的互斥锁。
  std::vector<Functor> pendingFunctors_;  ///< 跨线程投递的待执行任务队列。

  std::unique_ptr<TimerQueue> timerQueue_;  ///< 定时器队列，管理定时器相关逻辑。
};

}  // namespace tinynet
