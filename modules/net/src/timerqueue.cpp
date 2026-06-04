#include "net/timerqueue.hpp"

#include <assert.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cstdint>

namespace net {

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      // CLOCK_MONOTONIC 确保 timerfd 不受系统时间调整影响，TFD_NONBLOCK | TFD_CLOEXEC 确保非阻塞且 exec 后自动关闭
      timerfd_(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      timerfdChannel_(loop, timerfd_),
      callingExpiredTimers_(false) {
  assert(timerfd_ >= 0);
  timerfdChannel_.setReadCallback([this](Timestamp) { handleRead(); });
  timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue() {
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  for (auto& [ts, timer] : timers_) {
    delete timer;
  }
}

TimerId TimerQueue::addTimer(TimerCallback cb, Timestamp when, double interval) {
  Timer* timer = new Timer(std::move(cb), when, interval);
  // 必须按值捕获 timer，runInLoop 可能在本函数返回后才执行
  loop_->runInLoop([this, timer]() { addTimerInLoop(timer); });
  return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId) {
  loop_->runInLoop([this, timerId]() { cancelInLoop(timerId); });
}

void TimerQueue::addTimerInLoop(Timer* timer) {
  // earliestChanged 用于判断是否需要重设 timerfd 的到期时间
  bool earliestChanged = insert(timer);
  if (earliestChanged) {
    resetTimerfd(timer->expiration());
  }
}

void TimerQueue::cancelInLoop(TimerId timerId) {
  auto it = activeTimers_.find(timerId.sequence_);
  // 校验指针，防止 sequence 复用（ABA 问题）
  if (it != activeTimers_.end() && it->second == timerId.timer_) {
    size_t n = timers_.erase(Entry(it->second->expiration(), it->second));
    assert(n == 1);
    // void(n) 用于显式忽略未使用的变量，避免编译器警告
    (void)n;
    delete it->second;
    activeTimers_.erase(it);

  }  // 配合handleRead中的callingExpiredTimers_
  else if (callingExpiredTimers_) {
    // 此定时器已被 getExpired 移出索引，但仍在 expired 列表里、其回调正在栈上执行。
    // 不能在此 delete：callback_ 存在 Timer 内部，删了就是 use-after-free；
    // 故只登记 sequence，由 reset() 统一销毁（跳过重新入队）。
    cancelingTimers_.insert(timerId.sequence_);
  }
}

void TimerQueue::handleRead() {
  // 函数被调用时说明timerfd到期了，获取当前时间戳并读走timerfd的数据，否则epoll会持续触发
  Timestamp now(Timestamp::now());
  uint64_t howmany;
  ssize_t n = ::read(timerfd_, &howmany, sizeof(howmany));
  (void)n;

  // 取出所有到期定时器
  std::vector<Entry> expired = getExpired(now);
  // 设置callingExpiredTimers_的目的是为了识别回调内自注销的定时器，cancelInLoop 通过 cancelingTimers_ 跳过它们的重入队
  callingExpiredTimers_ = true;
  for (const Entry& entry : expired) {
    // 执行定时器回调
    entry.second->run();
  }
  callingExpiredTimers_ = false;

  // 执行完回调后，重置周期定时器或删除一次性定时器，并重设 timerfd 的下次到期时间
  reset(expired, now);
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now) {
  // 从timers中取出所有 expiration <= now 的定时器，并且将这些定时器从timers和activeTimers_中移除
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  auto end = timers_.lower_bound(sentry);
  std::vector<Entry> expired(timers_.begin(), end);

  // 从 timers_ 中删除已到期的定时器，并从 activeTimers_ 中删除对应的 sequence
  timers_.erase(timers_.begin(), end);
  for (const Entry& entry : expired) {
    activeTimers_.erase(entry.second->sequence());
  }
  return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now) {
  for (const Entry& entry : expired) {
    Timer* timer = entry.second;
    // 如果timer是周期性的且没有在回调内被取消，就重新入队；否则删除timer
    if (timer->repeat() && cancelingTimers_.find(timer->sequence()) == cancelingTimers_.end()) {
      timer->restart(now);
      insert(timer);
    } else {
      // 不是周期性的，或者是周期性的但在回调内被取消了，直接删除
      delete timer;
    }
  }
  // 执行完回调后，清空 cancelingTimers_，为下一轮回调做好准备
  cancelingTimers_.clear();

  // 重设 timerfd 的下次到期时间，如果没有定时器了就 disarm（设置一个很远的未来时间）
  if (!timers_.empty()) {
    resetTimerfd(timers_.begin()->first);
  }
}

bool TimerQueue::insert(Timer* timer) {
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  // 说明新插入的定时器比当前 timers_ 中最早到期的定时器还早，需要重设 timerfd 的到期时间
  if (timers_.empty() || when < timers_.begin()->first) {
    earliestChanged = true;
  }
  timers_.insert(Entry(when, timer));
  activeTimers_[timer->sequence()] = timer;

  return earliestChanged;
}

void TimerQueue::resetTimerfd(Timestamp when) {
  int64_t microseconds = when.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100)
    microseconds = 100;

  itimerspec newValue{};
  newValue.it_value.tv_sec = static_cast<time_t>(microseconds / 1000000);
  newValue.it_value.tv_nsec = static_cast<long>((microseconds % 1000000) * 1000);
  // 设置下一次触发时刻，flags 置 0 表示相对时间，如果需要绝对时间则置 TFD_TIMER_ABSTIME
  ::timerfd_settime(timerfd_, 0, &newValue, nullptr);
}

}  // namespace net
