#pragma once
#include <atomic>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/timestamp.hpp"
#include "net/callbacks.hpp"
#include "net/eventloop.hpp"
#include "net/timer.hpp"  // Timer + TimerId

namespace tinynet {

/**
 * @brief 定时器队列：用 timerfd 把"时间到达"变成 fd 可读事件，融入 Reactor。
 *
 * timerfd 注册成一个普通的可读 Channel，到期即触发 handleRead，因此无需改动
 * Poller 的超时参数。所有公开接口（addTimer/cancel）线程安全，内部统一通过
 * runInLoop 把真正的操作投递回所属 loop 线程执行，故 timers_ 等成员无需加锁。
 */
class TimerQueue {
 public:
  /**
   * @brief 构造定时器队列，创建 timerfd 并注册到所属 EventLoop 的 Poller。
   * @param loop 所属事件循环，TimerQueue 不拥有其生命周期。
   */
  explicit TimerQueue(EventLoop* loop);

  /**
   * @brief 析构：注销并关闭 timerfd，释放所有未到期的 Timer。
   */
  ~TimerQueue();

  /**
   * @brief 注册一个定时器（线程安全，可跨线程调用）。
   * @param cb       到期回调。
   * @param when     首次到期时刻。
   * @param interval 重复间隔（秒）；<=0 表示一次性定时器。
   * @return 用于后续 cancel 的句柄。
   */
  TimerId addTimer(TimerCallback cb, Timestamp when, double interval);

  /**
   * @brief 取消一个定时器（线程安全）。
   */
  void cancel(TimerId timerId);

 private:
  using Entry = std::pair<Timestamp, Timer*>;  ///< 主索引元素：按到期时间排序。
  using TimerList = std::set<Entry>;           ///< 按到期时间排序的定时器集合。

  EventLoop* loop_;                                   ///< 所属事件循环，不拥有其生命周期。
  const int timerfd_;                                 ///< timerfd_create 创建的定时器 fd。
  Channel timerfdChannel_;                            ///< 包装 timerfd_ 的 Channel，监听可读事件。
  TimerList timers_;                                  ///< 主索引：按到期时间排序，用于取最早。
  std::unordered_map<int64_t, Timer*> activeTimers_;  ///< 辅索引：sequence → Timer*，与 timers_ 一一对应，供 cancel。
  std::atomic<bool> callingExpiredTimers_;            ///< 是否正在执行到期回调，用于识别"回调内自注销"。

  std::unordered_set<int64_t> cancelingTimers_;  ///< 回调执行期间被请求取消的定时器 sequence，reset 时跳过不重新入队。

  /**
   * @brief 在 loop 线程内插入定时器，必要时重设 timerfd。
   */
  void addTimerInLoop(Timer* timer);

  /**
   * @brief 在 loop 线程内取消定时器，处理"回调内自注销"的情况。
   */
  void cancelInLoop(TimerId timerId);

  /**
   * @brief timerfd 可读回调：取出到期定时器、逐个执行、再重新入队/重设。
   */
  void handleRead();

  /**
   * @brief 取出所有 expiration <= now 的定时器，并从两个 set 中移除。
   * @param now 当前时刻。即时间到期的基准，所有早于now的定时器都被认为已到期。
   * @return 已到期的定时器列表。
   */
  std::vector<Entry> getExpired(Timestamp now);

  /**
   * @brief 周期定时器 restart 后重新入队，其余 delete；并重设 timerfd。
   * @param expired getExpired 取出的已到期定时器列表。
   * @param now     当前时刻，作为周期定时器重算到期的基准。
   */
  void reset(const std::vector<Entry>& expired, Timestamp now);

  /**
   * @brief 向 timers_ 和 activeTimers_ 同步插入一个定时器。
   * @param timer 待插入的定时器。
   * @return 是否改变了最早到期时刻（true 时调用方需重设 timerfd）。
   */
  bool insert(Timer* timer);

  /**
   * @brief 用 timerfd_settime 设置下次触发时刻。
   * @param when 下次到期的绝对时刻。
   */
  void resetTimerfd(Timestamp when);
};

}  // namespace tinynet
