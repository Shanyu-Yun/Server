#include "net/timer.hpp"

#include <cstdint>
#include <utility>

namespace net {

// 全局自增序号计数器，进程内唯一。在此处给出定义（类内只是声明）。
std::atomic<int64_t> Timer::s_numCreated_{0};

Timer::Timer(TimerCallback cb, Timestamp when, double interval)
    : callback_(std::move(cb)),
      expiration_(when),
      interval_(interval),
      repeat_(interval > 0.0),
      sequence_(s_numCreated_.fetch_add(1, std::memory_order_relaxed)) {}

void Timer::restart(Timestamp now) {
  if (repeat_) {
    // interval_ 单位是秒，operator+ 接受微秒，需要转换
    expiration_ = now + static_cast<int64_t>(interval_ * 1000000);
  } else {
    expiration_ = Timestamp::invalid();
  }
}

}  // namespace net
