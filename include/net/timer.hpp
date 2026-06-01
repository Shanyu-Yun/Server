#pragma once
#include <atomic>
#include <cstdint>

#include "base/noncopyable.hpp"
#include "base/timestamp.hpp"
#include "net/callbacks.hpp"

namespace tinynet {

/**
 * @brief 一个定时任务本身（内部类，不对用户直接暴露）。
 *
 * 记录一次定时任务的全部信息：到期回调、到期时刻、重复间隔，以及一个全局
 * 唯一的自增序号。一次性定时器到期后由 TimerQueue 负责 delete；周期定时器
 * 通过 restart() 以当前时刻为基准重算下次到期时刻后重新入队。
 */
class Timer : noncopyable {
 public:
  /**
   * @brief 构造函数
   *
   * @param cb       到期回调。
   * @param when     首次到期时刻。
   * @param interval 重复间隔（秒）；<=0 表示一次性定时器。
   */
  Timer(TimerCallback cb, Timestamp when, double interval);

  /**
   * @brief 执行到期回调。
   */
  void run() const {
    callback_();
  }

  /**
   * @brief 返回本定时器的到期时刻。
   */
  Timestamp expiration() const {
    return expiration_;
  }

  /**
   * @brief 返回是否为周期定时器（interval > 0）。
   */
  bool repeat() const {
    return repeat_;
  }

  /**
   * @brief 返回全局唯一自增序号，配合 TimerId 唯一标识一个定时器。
   */
  int64_t sequence() const {
    return sequence_;
  }

  /**
   * @brief 周期定时器以 now 为基准重新计算下次到期时刻。
   * @param now 当前时刻；非周期定时器调用后到期时刻被置为 invalid。
   */
  void restart(Timestamp now);

  ~Timer() = default;

 private:
  const TimerCallback callback_;  ///< 到期时执行的回调。
  Timestamp expiration_;          ///< 下次到期时刻；restart() 会修改，故非 const。
  const double interval_;         ///< 重复间隔（秒），<=0 表示一次性。
  const bool repeat_;             ///< 是否为周期定时器。
  const int64_t sequence_;        ///< 全局唯一自增序号。

  static std::atomic<int64_t> s_numCreated_;  ///< 生成 sequence_ 的全局原子计数器。
};

/**
 * @brief 对外句柄，用于 cancel 一个定时器。
 *
 * 同时持有 Timer 指针和它的 sequence：一次性 Timer 被 delete 后其地址可能
 * 被新 Timer 复用，只比指针会误删（ABA 问题），(Timer*, sequence) 二元组
 * 才能唯一标识一个定时器。
 */
class TimerId {
 public:
  /**
   * @brief 构造一个空句柄（不指向任何定时器）。
   */
  TimerId() : timer_(nullptr), sequence_(0) {}

  /**
   * @brief 构造指向指定定时器的句柄。
   * @param timer 目标定时器指针。
   * @param seq   该定时器的全局唯一序号。
   */
  TimerId(Timer* timer, int64_t seq) : timer_(timer), sequence_(seq) {}
  ~TimerId() = default;

  friend class TimerQueue;  // 只有 TimerQueue 能读私有成员

 private:
  Timer* timer_;      ///< 目标定时器指针，可能已失效，需配合 sequence_ 校验。
  int64_t sequence_;  ///< 目标定时器的全局唯一序号。
};

}  // namespace tinynet
