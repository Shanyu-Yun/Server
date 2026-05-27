# 阶段 1：Timer / TimerQueue 详细设计

> 目标：让 `EventLoop` 支持 `runAt / runAfter / runEvery / cancel`，作为后续阶段（重连退避、空闲淘汰、心跳）的基础。

---

## 1. 设计概览

```
        ┌────────────────────────────────────────────┐
        │              EventLoop                     │
        │  + runAt(when, cb)        ──┐              │
        │  + runAfter(delay, cb)    ──┼─► 转发给     │
        │  + runEvery(interval, cb) ──┤              │
        │  + cancel(timerId)        ──┘              │
        │                                            │
        │       std::unique_ptr<TimerQueue>          │
        └──────────────────┬─────────────────────────┘
                           │ owns
                           ▼
        ┌────────────────────────────────────────────┐
        │              TimerQueue                    │
        │                                            │
        │  timerfd ── Channel ── 注册到 Poller       │
        │                                            │
        │  std::set<(Timestamp, Timer*)>  timers_    │
        │  std::set<(Timer*, sequence)>   activeTimers_     │
        │  std::set<(Timer*, sequence)>   cancelingTimers_  │
        │  bool                          callingExpiredTimers_ │
        └────────────────────────────────────────────┘
```

**核心 idiom**：用 `timerfd_create` 把"等到某时刻"转成"等 fd 可读"，从而完美融入现有的 `Channel` + `Poller` 体系，无需让 `Poller::poll` 计算 timeout。

---

## 2. 新增类型

### 2.1 `TimerCallback` 别名

加到 `include/net/callbacks.hpp`：

```cpp
using TimerCallback = std::function<void()>;
```

### 2.2 `Timer` 类

新建 `include/net/timer.hpp`：

```cpp
class Timer : noncopyable {
 public:
  Timer(TimerCallback cb, Timestamp when, double intervalSec);

  void run() const { callback_(); }

  Timestamp expiration() const { return expiration_; }
  bool      repeat()     const { return repeat_; }
  int64_t   sequence()   const { return sequence_; }

  // 周期 timer 到期后重置 expiration_
  void restart(Timestamp now);

  static int64_t numCreated() { return s_numCreated_.load(); }

 private:
  const TimerCallback callback_;
  Timestamp           expiration_;
  const double        interval_;   // 秒，<=0 表示一次性
  const bool          repeat_;
  const int64_t       sequence_;

  static std::atomic<int64_t> s_numCreated_;
};
```

构造时：
- `repeat_ = (intervalSec > 0.0)`
- `sequence_ = ++s_numCreated_`

`restart(now)`：若 repeat_ 则 `expiration_ = now + interval`，否则 `expiration_ = Timestamp()`。

### 2.3 `TimerId` 类

新建 `include/net/timerid.hpp`。这是给用户的"取消令牌"，只用于 `cancel`：

```cpp
class TimerId {
 public:
  TimerId() : timer_(nullptr), sequence_(0) {}
  TimerId(Timer* timer, int64_t seq) : timer_(timer), sequence_(seq) {}

  friend class TimerQueue;

 private:
  Timer*  timer_;
  int64_t sequence_;
};
```

**为什么同时存 `Timer*` 和 `sequence_`？**
- `Timer*` 用作主键查找
- `sequence_` 防止 ABA：旧 Timer 析构后地址被新 Timer 复用，仅靠指针会误删

### 2.4 `TimerQueue` 类

新建 `include/net/timerqueue.hpp`：

```cpp
class TimerQueue : noncopyable {
 public:
  explicit TimerQueue(EventLoop* loop);
  ~TimerQueue();

  // 线程安全：可从任意线程调用
  TimerId addTimer(TimerCallback cb, Timestamp when, double intervalSec);
  void    cancel(TimerId timerId);

 private:
  using Entry          = std::pair<Timestamp, Timer*>;
  using TimerList      = std::set<Entry>;
  using ActiveTimer    = std::pair<Timer*, int64_t>;
  using ActiveTimerSet = std::set<ActiveTimer>;

  // 只在 loop 线程内调用
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);

  // timerfd 可读回调
  void handleRead();

  // 取出所有到期 timer 并从 timers_ / activeTimers_ 删除
  std::vector<Entry> getExpired(Timestamp now);

  // 处理到期 timer 后，重置周期 timer，并更新 timerfd
  void reset(const std::vector<Entry>& expired, Timestamp now);

  // 插入 timer，返回是否需要重置 timerfd（即新 timer 比当前最早还早）
  bool insert(Timer* timer);

  EventLoop*               loop_;
  const int                timerfd_;
  std::unique_ptr<Channel> timerfdChannel_;

  TimerList      timers_;          // 按到期时间排序
  ActiveTimerSet activeTimers_;    // 按指针+seq 排序，用于 cancel 查找
  bool           callingExpiredTimers_;
  ActiveTimerSet cancelingTimers_; // 处理到期回调期间被取消的 timer
};
```

**两套 set 的作用：**
- `timers_` 按时间排序，`begin()` 就是最近要到期的 timer
- `activeTimers_` 按 (Timer*, seq) 排序，`cancel` 时 O(log N) 查找

---

## 3. Linux API 用法速查

### 3.1 `timerfd_create`

```cpp
#include <sys/timerfd.h>
int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
```
- `CLOCK_MONOTONIC`：单调时钟，不受系统时间跳变影响
- `TFD_NONBLOCK`：read 不阻塞
- `TFD_CLOEXEC`：exec 时自动关闭

### 3.2 `timerfd_settime`

```cpp
struct itimerspec newValue;
newValue.it_value    = howMuchTimeFromNow(when);  // 首次触发剩余时间
newValue.it_interval = {0, 0};                     // 我们不用内核周期，自己重设
timerfd_settime(timerfd_, 0, &newValue, nullptr);
```

辅助函数 `howMuchTimeFromNow`（写在 timerqueue.cpp 的匿名命名空间）：
```cpp
struct timespec howMuchTimeFromNow(Timestamp when) {
  int64_t microseconds = when.microSecondsSinceEpoch()
                       - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100) microseconds = 100;  // 太短就给个最小值
  struct timespec ts;
  ts.tv_sec  = static_cast<time_t>(microseconds / 1'000'000);
  ts.tv_nsec = static_cast<long>((microseconds % 1'000'000) * 1000);
  return ts;
}
```

> ⚠️ 当前 `Timestamp` 没有 `microSecondsSinceEpoch()` getter，需要补一个 public const 方法。

### 3.3 timerfd 可读时

读 8 字节清掉就绪标志：
```cpp
uint64_t howmany;
::read(timerfd_, &howmany, sizeof(howmany));
```

---

## 4. 关键流程详解

### 4.1 添加 timer

```
用户线程                       loop 线程
─────────                       ────────
loop->runAfter(2.0, cb)
  │
  └─► timerQueue_->addTimer(cb, when, 0)
        │
        │  new Timer(cb, when, 0)  ← 在调用线程构造
        │
        └─► loop_->runInLoop([timer]{ addTimerInLoop(timer); })
                                          │
                                          ▼
                                    addTimerInLoop(timer)
                                    ├─ earliestChanged = insert(timer)
                                    └─ if (earliestChanged)
                                         resetTimerfd(timer->expiration())
```

**`insert(timer)` 实现要点：**
```cpp
bool TimerQueue::insert(Timer* timer) {
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  auto it = timers_.begin();
  if (it == timers_.end() || when < it->first) {
    earliestChanged = true;
  }
  timers_.insert({when, timer});
  activeTimers_.insert({timer, timer->sequence()});
  return earliestChanged;
}
```

### 4.2 timerfd 可读 → 触发回调

```cpp
void TimerQueue::handleRead() {
  Timestamp now = Timestamp::now();
  readTimerfd(timerfd_, now);  // 读 8 字节

  std::vector<Entry> expired = getExpired(now);

  callingExpiredTimers_ = true;
  cancelingTimers_.clear();
  for (const auto& [_, timer] : expired) {
    timer->run();
  }
  callingExpiredTimers_ = false;

  reset(expired, now);  // 把周期 timer 重新插回去，并更新 timerfd
}
```

**`getExpired` 实现要点：**
```cpp
std::vector<Entry> TimerQueue::getExpired(Timestamp now) {
  std::vector<Entry> expired;
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  auto end = timers_.lower_bound(sentry);
  std::copy(timers_.begin(), end, std::back_inserter(expired));
  timers_.erase(timers_.begin(), end);

  for (const auto& [_, timer] : expired) {
    activeTimers_.erase({timer, timer->sequence()});
  }
  return expired;
}
```

**`reset` 实现要点：**
```cpp
void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now) {
  for (const auto& [_, timer] : expired) {
    ActiveTimer at(timer, timer->sequence());
    if (timer->repeat() && cancelingTimers_.count(at) == 0) {
      timer->restart(now);
      insert(timer);  // 复用
    } else {
      delete timer;
    }
  }

  if (!timers_.empty()) {
    resetTimerfd(timers_.begin()->first);
  }
}
```

### 4.3 取消 timer

```cpp
void TimerQueue::cancelInLoop(TimerId timerId) {
  ActiveTimer at(timerId.timer_, timerId.sequence_);
  auto it = activeTimers_.find(at);
  if (it != activeTimers_.end()) {
    timers_.erase({it->first->expiration(), it->first});
    delete it->first;
    activeTimers_.erase(it);
  } else if (callingExpiredTimers_) {
    // 该 timer 正在 handleRead 的 expired 列表里执行，
    // 标记为 canceling，reset 阶段会跳过重新插入
    cancelingTimers_.insert(at);
  }
  // 其他情况：timer 已被销毁，无需操作
}
```

---

## 5. EventLoop 集成

### 5.1 头文件改动

`include/net/eventloop.hpp`：

```cpp
#include "net/timerid.hpp"
class TimerQueue;  // 前置声明，避免循环依赖

class EventLoop {
 public:
  // ... 现有 ...

  TimerId runAt(Timestamp when, TimerCallback cb);
  TimerId runAfter(double delaySec, TimerCallback cb);
  TimerId runEvery(double intervalSec, TimerCallback cb);
  void    cancel(TimerId timerId);

 private:
  std::unique_ptr<TimerQueue> timerQueue_;  // 在构造函数初始化列表里 make_unique
};
```

### 5.2 .cpp 改动

`src/net/eventloop.cpp`：

```cpp
#include "net/timerqueue.hpp"

EventLoop::EventLoop()
    : /* 现有初始化 */,
      timerQueue_(std::make_unique<TimerQueue>(this)) {
  // ...
}

TimerId EventLoop::runAt(Timestamp when, TimerCallback cb) {
  return timerQueue_->addTimer(std::move(cb), when, 0.0);
}

TimerId EventLoop::runAfter(double delaySec, TimerCallback cb) {
  Timestamp when = addTime(Timestamp::now(), delaySec);
  return runAt(when, std::move(cb));
}

TimerId EventLoop::runEvery(double intervalSec, TimerCallback cb) {
  Timestamp when = addTime(Timestamp::now(), intervalSec);
  return timerQueue_->addTimer(std::move(cb), when, intervalSec);
}

void EventLoop::cancel(TimerId timerId) {
  timerQueue_->cancel(timerId);
}
```

### 5.3 `addTime` 辅助函数

加到 `base/timestamp.hpp`：

```cpp
inline Timestamp addTime(Timestamp t, double seconds) {
  int64_t delta = static_cast<int64_t>(seconds * 1'000'000);
  return Timestamp(t.microSecondsSinceEpoch() + delta);
}
```

同时给 `Timestamp` 加 public getter（如果还没有）：
```cpp
int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }
bool    valid() const { return microSecondsSinceEpoch_ > 0; }
```

---

## 6. CMake 改动

`CMakeLists.txt` 的 `muduo_core` 源文件列表添加：

```cmake
src/net/timer.cpp
src/net/timerqueue.cpp
```

---

## 7. 实现步骤建议

按以下顺序提交，每步独立可编译：

1. **第 1 步**：补 `Timestamp::microSecondsSinceEpoch()` getter 和 `addTime` 辅助函数
2. **第 2 步**：实现 `Timer` 类（timer.hpp + timer.cpp，仅含构造、run、restart、static counter）
3. **第 3 步**：实现 `TimerId` 类（纯头文件）
4. **第 4 步**：实现 `TimerQueue` 类（timerqueue.hpp + .cpp），先实现单次定时（不管 cancel）
5. **第 5 步**：在 `EventLoop` 中暴露 `runAt/runAfter/runEvery`，编译通过即可
6. **第 6 步**：在 `src/main.cpp` 加一行 `loop.runAfter(2.0, []{ LOGINFO("hi"); })` 跑通验证
7. **第 7 步**：补全周期 timer 和 cancel 逻辑
8. **第 8 步**：写 `benchmark/timer_test.cpp` 完整覆盖

---

## 8. 验证清单

实现完成后，依次跑：

- [ ] `cmake --build build/` 通过，无 warning
- [ ] `./build/muduo` 启动后日志看到 alive 心跳（如果 main.cpp 加了 runEvery）
- [ ] `./build/timer_test`：
  - 6 个 runAfter 任务按设定时间顺序触发（误差 ≤ 5ms）
  - runEvery 任务在 cancel 前精确触发 N 次，cancel 后不再触发
  - 跨线程 runAfter 能正确投递到 loop 线程执行
- [ ] 用 `valgrind ./build/timer_test` 检查无内存泄漏

---

## 9. 容易踩的坑

| 坑 | 表现 | 修复 |
|----|------|------|
| timer 在 cancel 后真正被回调 | 程序崩溃 | 在 `reset` 中用 `cancelingTimers_` 跳过 |
| activeTimers_ 与 timers_ 不同步 | cancel 删不掉 | 任何 timer 进入/离开 timers_ 时务必同步 activeTimers_ |
| `Timer*` ABA | cancel 误删新 timer | TimerId 必须 (Timer*, seq) 双键 |
| timerfd_settime 传 0 时间 | 不会触发 | `howMuchTimeFromNow` 给最小 100 微秒兜底 |
| 跨线程添加 timer 直接改 set | 数据竞争 | `addTimer` 必须 `runInLoop` 路由到 loop 线程 |
| `repeat_ == false` 还重设 timerfd | 不影响功能但浪费 | `reset` 末尾只在 `timers_` 非空时 resetTimerfd |

---

## 10. 完成后的下一步

更新 `docs/个人笔记.md`，新增章节：
- timerfd 与 epoll 融合的优雅之处
- 为何 muduo 选 `std::set` 而非小顶堆（cancel 友好）
- TimerId 双键设计（防 ABA）
- 周期 timer 的 cancel 时机分裂（未触发 / 正在触发）

笔记写完后，开始阶段 2 或阶段 4 的工作（独立小修，可任选）。
