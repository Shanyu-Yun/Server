# muduo-learn 完整实现方案

> 本文档是一份**完整项目实现规格**，目标是让你能照着它独立写出一个 muduo 风格的 C++ 非阻塞 TCP 网络库。
>
> 全文按 **「自底向上」** 的顺序组织：你按章节顺序实现，就能保证每一步只依赖前面写过的代码。

---

## 目录

- [0. 项目概览](#0-项目概览)
- [1. 总体架构](#1-总体架构)
- [2. 目录与构建结构](#2-目录与构建结构)
- [3. 模块详细设计](#3-模块详细设计)
  - [3.1 base：基础工具](#31-base基础工具)
  - [3.2 net：事件循环核心](#32-net事件循环核心)
  - [3.3 thread：线程与线程池](#33-thread线程与线程池)
  - [3.4 socket：地址与套接字](#34-socket地址与套接字)
  - [3.5 buffer：应用层缓冲区](#35-buffer应用层缓冲区)
  - [3.6 tcp：连接与服务器](#36-tcp连接与服务器)
- [4. 实现里程碑](#4-实现里程碑)
- [5. 测试与验证方案](#5-测试与验证方案)
- [6. 关键设计要点与陷阱清单](#6-关键设计要点与陷阱清单)
- [7. 参考 API 速查](#7-参考-api-速查)

---

## 0. 项目概览

### 0.1 目标
实现一个能跑 echo / chat / http 等基础服务的 **非阻塞 TCP 网络库**，对外暴露 `TcpServer` 接口，用户只需注册回调即可。

### 0.2 核心特性
- **Reactor 模式**：基于 epoll（LT 模式）的事件驱动
- **one loop per thread**：每个 IO 线程独占一个 EventLoop，无锁化处理连接
- **mainLoop + subLoop 线程池**：主 loop 只接受连接，子 loop 处理已连接的 IO
- **跨线程安全**：通过 `runInLoop / queueInLoop + eventfd` 实现
- **RAII 资源管理**：fd、线程、loop 都用 RAII 包装
- **shared_ptr 管理连接生命周期**：避免悬空指针

### 0.3 技术栈
- C++17（`std::function`, `std::thread`, `std::atomic`, `std::shared_ptr`）
- Linux 系统调用：`epoll`, `eventfd`, `socket`, `readv`
- 构建：CMake 3.10+

---

## 1. 总体架构

### 1.1 Reactor 模式简述
```
       ┌──────────────┐
       │  Poller      │ ←─ epoll_wait 返回活跃 channels
       │  (epoll)     │
       └──────┬───────┘
              │ activeChannels
              ↓
       ┌──────────────┐
       │  EventLoop   │ ←─ 调用 channel->handleEvent()
       └──────┬───────┘
              │
              ↓
       ┌──────────────┐
       │  Channel     │ ←─ 根据 revents 分发到具体回调
       └──────────────┘
              │
              ↓
       业务回调（TcpConnection::handleRead 等）
```

### 1.2 主从 Reactor 多线程模型
```
   mainLoop (1 个线程)               subLoop[i] (N 个线程)
   ┌────────────────────┐           ┌────────────────────┐
   │  Acceptor          │           │  TcpConnection×K   │
   │  ↓ accept          │ getNext─→ │  Channel×K         │
   │  newConnection     │           │  Poller (epoll)    │
   │  ↓ runInLoop       │           │  EventLoop         │
   │  (轮询分发 subLoop)│           └────────────────────┘
   └────────────────────┘
```

### 1.3 类依赖关系图
```
                ┌─────────────┐
                │  TcpServer  │
                └──────┬──────┘
          ┌────────────┼────────────────┐
          ↓            ↓                ↓
   ┌────────────┐  ┌──────────┐  ┌──────────────────────┐
   │  Acceptor  │  │ TcpConn  │  │ EventLoopThreadPool  │
   └─────┬──────┘  └────┬─────┘  └──────────┬───────────┘
         │              │                   │
         │              │                   ↓
         │              │           ┌──────────────────┐
         │              │           │ EventLoopThread  │
         │              │           └────────┬─────────┘
         │              │                    │
         ↓              ↓                    ↓
   ┌──────────┐    ┌──────────┐       ┌────────────┐
   │ Socket   │    │ Channel  │       │ EventLoop  │
   └──────────┘    │ Buffer   │       └─────┬──────┘
                   └────┬─────┘             │
                        │                   ↓
                        │              ┌──────────┐
                        └──────────────│  Poller  │←── EPollPoller
                                       └──────────┘
```

---

## 2. 目录与构建结构

### 2.1 推荐目录
```
muduo_learn/
├── CMakeLists.txt
├── DESIGN.md                  ← 本文件
├── include/
│   ├── base/
│   │   ├── noncopyable.hpp
│   │   ├── Timestamp.hpp
│   │   ├── Logger.hpp
│   │   └── CurrentThread.hpp
│   ├── net/
│   │   ├── Channel.hpp
│   │   ├── Poller.hpp
│   │   ├── EPollPoller.hpp
│   │   ├── EventLoop.hpp
│   │   ├── Callbacks.hpp
│   │   ├── InetAddress.hpp
│   │   ├── Socket.hpp
│   │   ├── Acceptor.hpp
│   │   ├── Buffer.hpp
│   │   ├── TcpConnection.hpp
│   │   └── TcpServer.hpp
│   └── thread/
│       ├── Thread.hpp
│       ├── EventLoopThread.hpp
│       └── EventLoopThreadPool.hpp
├── src/
│   └── (与 include 对应的 .cpp)
└── examples/
    ├── echo_server.cpp
    └── chat_server.cpp
```

> 现有代码用的是 `include/` 平铺。可保留现状，也可以重构成分模块结构。后者更接近 muduo 原版。

### 2.2 CMake 建议
- 把库目标做成 `add_library(muduo_learn STATIC ...)`（不再用 INTERFACE，因为有 .cpp）
- examples 目录里每个 cpp 编成一个可执行程序，`target_link_libraries(... muduo_learn pthread)`
- 链接 `pthread`（线程必需）

---

## 3. 模块详细设计

> 每个类我会给出：**职责 / 依赖 / 字段 / 方法 / 关键实现思路 / 注意事项**。
>
> 字段后的注释里 `[T]` 表示该字段是**线程安全相关**，需要 atomic 或加锁保护。

---

### 3.1 base：基础工具

#### 3.1.1 `noncopyable` ✅
（已完成，保持原状即可）

#### 3.1.2 `Timestamp`
**职责**：微秒级时间戳。

| 字段 | 类型 | 说明 |
|---|---|---|
| `microSecondsSinceEpoch_` | `int64_t` | 自 Epoch 起的微秒数 |

| 方法 | 签名 | 说明 |
|---|---|---|
| 构造 | `Timestamp()` | 初始化为 0 |
| 构造 | `explicit Timestamp(int64_t us)` | 显式构造 |
| `now` | `static Timestamp now()` | 用 `gettimeofday` 取当前时间 |
| `toString` | `std::string toString() const` | 格式 `"YYYY/MM/DD HH:MM:SS"` |

**实现要点**
- `gettimeofday(&tv, nullptr)`，然后 `tv.tv_sec * 1000000 + tv.tv_usec`
- `toString` 用 `localtime_r` + `snprintf`

---

#### 3.1.3 `Logger` + 日志宏
**职责**：单例日志器，配合宏使用。

```cpp
enum class LogLevel { INFO, ERROR, FATAL, DEBUG };
```

| 字段 | 类型 |
|---|---|
| `logLevel_` | `LogLevel` |

| 方法 | 签名 |
|---|---|
| `instance` | `static Logger& instance()` |
| `setLogLevel` | `void setLogLevel(LogLevel)` |
| `log` | `void log(std::string msg)` |

**配套宏**（写在头文件中）
```cpp
#define LOG_INFO(fmt, ...) \
    do { \
        Logger& l = Logger::instance(); \
        l.setLogLevel(LogLevel::INFO); \
        char buf[1024] = {0}; \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        l.log(buf); \
    } while (0)

#define LOG_ERROR(fmt, ...)   /* 类似 */
#define LOG_DEBUG(fmt, ...)   /* 仅 MUDUO_DEBUG 编译开启 */
#define LOG_FATAL(fmt, ...)   /* 输出后 exit(-1) */
```

**输出格式**：`[INFO][2026/05/22 10:23:45]: msg`
- 时间戳从 `Timestamp::now().toString()` 取

---

#### 3.1.4 `CurrentThread`（namespace）
**职责**：缓存当前线程 tid，避免每次 syscall。

```cpp
namespace CurrentThread {
    extern __thread int t_cachedTid;
    void cacheTid();
    inline int tid() {
        if (__builtin_expect(t_cachedTid == 0, 0)) cacheTid();
        return t_cachedTid;
    }
}
```
- `cacheTid` 调 `syscall(SYS_gettid)`
- 用 `__thread` 关键字让每个线程独立缓存

---

### 3.2 net：事件循环核心

> 这一节的 Channel / Poller / EventLoop 是整个库的核心，强相互依赖。**建议先把三个头文件草稿写完再开始写 .cpp**。

#### 3.2.1 `Channel`
**职责**：封装「一个 fd + 它关心的事件 + 发生事件后的回调」。**不拥有 fd**。

**依赖**：`EventLoop` (前向声明)、`Timestamp`

| 字段 | 类型 | 说明 |
|---|---|---|
| `loop_` | `EventLoop*` | 所属 loop |
| `fd_` | `const int` | 监听的 fd |
| `events_` | `int` | 注册的事件位 |
| `revents_` | `int` | Poller 返回的实际事件 |
| `index_` | `int` | 在 Poller 中的状态 (kNew/kAdded/kDeleted) |
| `tied_` | `bool` | 是否绑定了 owner |
| `tie_` | `std::weak_ptr<void>` | 绑定的 owner（通常是 TcpConnection） |
| `readCallback_` | `ReadEventCallback` | `std::function<void(Timestamp)>` |
| `writeCallback_` | `EventCallback` | `std::function<void()>` |
| `closeCallback_` | `EventCallback` | |
| `errorCallback_` | `EventCallback` | |

**静态常量**
```cpp
static const int kNoneEvent  = 0;
static const int kReadEvent  = EPOLLIN | EPOLLPRI;
static const int kWriteEvent = EPOLLOUT;
```

| 方法 | 签名 | 说明 |
|---|---|---|
| 构造 | `Channel(EventLoop*, int fd)` | 不创建 fd，只绑定 |
| 析构 | `~Channel()` | 不关闭 fd |
| 4 个 setXxxCallback | `void setReadCallback(ReadEventCallback)` 等 | 用 move |
| `handleEvent` | `void handleEvent(Timestamp)` | 入口：根据 revents 分发 |
| `enableReading` | `void enableReading()` | `events_ |= kReadEvent; update();` |
| `disableReading` | | `events_ &= ~kReadEvent; update();` |
| `enableWriting/disableWriting/disableAll` | | 类似 |
| `isReading/isWriting/isNoneEvent` | `bool ... const` | |
| `tie` | `void tie(const std::shared_ptr<void>&)` | TcpConnection 调用 |
| `update` | `void update()` (private) | `loop_->updateChannel(this)` |
| `remove` | `void remove()` | `loop_->removeChannel(this)` |
| `index/setIndex` | `int/void` | 给 EPollPoller 用 |

**`handleEvent` 实现思路**
```cpp
void Channel::handleEvent(Timestamp receiveTime) {
    if (tied_) {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard) handleEventWithGuard(receiveTime);
        // else: owner 已销毁，直接返回
    } else {
        handleEventWithGuard(receiveTime);
    }
}

void Channel::handleEventWithGuard(Timestamp t) {
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & EPOLLERR) {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI)) {
        if (readCallback_) readCallback_(t);
    }
    if (revents_ & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
}
```

**注意事项**
- `tie` 机制专为 TcpConnection 设计：避免在 `handleEvent` 执行过程中 TcpConnection 被销毁。
- Channel 自己不能拥有 fd（fd 由 Socket 或 Acceptor 拥有）。

---

#### 3.2.2 `Poller`（抽象基类）
**职责**：IO 多路复用的抽象。

| 字段 | 类型 |
|---|---|
| `channels_` | `std::unordered_map<int, Channel*>` |
| `ownerLoop_` | `EventLoop*` |

| 方法 | 签名 |
|---|---|
| 构造 | `Poller(EventLoop*)` |
| 析构 | `virtual ~Poller() = default` |
| `poll` | `virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0` |
| `updateChannel` | `virtual void updateChannel(Channel*) = 0` |
| `removeChannel` | `virtual void removeChannel(Channel*) = 0` |
| `hasChannel` | `virtual bool hasChannel(Channel*) const` |
| 静态工厂 | `static Poller* newDefaultPoller(EventLoop*)` |

`ChannelList = std::vector<Channel*>`，定义在 EventLoop.hpp 里更合适，或者单独 typedef。

---

#### 3.2.3 `EPollPoller`
**职责**：基于 `epoll_create1 + epoll_wait` 的 Poller。

| 字段 | 类型 |
|---|---|
| `epollfd_` | `int` |
| `events_` | `std::vector<epoll_event>` 初始大小 16 |

**常量**
```cpp
static const int kNew     = -1;   // Channel 未注册
static const int kAdded   =  1;   // Channel 已在 epoll
static const int kDeleted =  2;   // Channel 曾在 epoll，已 DEL，但 channels_ 中仍存在
```

| 方法 | 实现要点 |
|---|---|
| 构造 | `epollfd_ = ::epoll_create1(EPOLL_CLOEXEC)` |
| `poll` | `::epoll_wait(epollfd_, events_.data(), events_.size(), timeoutMs)`；若返回值 == size，下次扩容 ×2 |
| `updateChannel` | 根据 channel->index() 决定 ADD/MOD：若 kNew/kDeleted 则 ADD，置 kAdded；若 kAdded 则按 isNoneEvent 决定 DEL 或 MOD |
| `removeChannel` | channels_.erase(fd)，若状态是 kAdded 还要 epoll_ctl(DEL) |
| `fillActiveChannels` | 把 epoll_wait 返回的 epoll_event[].data.ptr（指向 Channel）填入 activeChannels，并设置 revents_ |
| `update` | 内部辅助：`epoll_ctl(epollfd_, op, fd, &event)` |

**注意**
- `epoll_event.data.ptr` 存 `Channel*`，这样从 `epoll_wait` 返回后能直接拿到 channel。
- 出错时（除了 EINTR）用 LOG_ERROR/LOG_FATAL。

---

#### 3.2.4 `EventLoop`
**职责**：Reactor 主体，负责事件循环、跨线程任务调度。

**字段**
| 字段 | 类型 | 说明 |
|---|---|---|
| `looping_` | `std::atomic<bool>` | 是否在 loop 中 [T] |
| `quit_` | `std::atomic<bool>` | 退出标志 [T] |
| `callingPendingFunctors_` | `std::atomic<bool>` | 是否在执行回调队列 [T] |
| `threadId_` | `const pid_t` | 创建本 loop 的线程 id |
| `pollReturnTime_` | `Timestamp` | poll 最近一次返回的时间 |
| `poller_` | `std::unique_ptr<Poller>` | |
| `wakeupFd_` | `int` | `eventfd()` 创建 |
| `wakeupChannel_` | `std::unique_ptr<Channel>` | |
| `activeChannels_` | `ChannelList` | |
| `pendingFunctors_` | `std::vector<Functor>` | 待执行任务 [T] |
| `mutex_` | `std::mutex` | 保护 pendingFunctors_ |

`Functor = std::function<void()>`

**方法**
| 方法 | 签名 | 说明 |
|---|---|---|
| 构造 | `EventLoop()` | 创建 wakeupFd、绑定 wakeupChannel 的读回调 |
| 析构 | `~EventLoop()` | disableAll + remove wakeupChannel；close(wakeupFd) |
| `loop` | `void loop()` | 主循环 |
| `quit` | `void quit()` | 设置 quit_；若在其他线程调用，需 wakeup() |
| `runInLoop` | `void runInLoop(Functor cb)` | 本线程直接执行，否则 queueInLoop |
| `queueInLoop` | `void queueInLoop(Functor cb)` | 加锁入队；若不在 loop 线程 / 正在 doPendingFunctors，需 wakeup() |
| `wakeup` | `void wakeup()` | 向 wakeupFd 写 1 (uint64_t) |
| `handleRead` | `void handleRead()` (private) | 读 wakeupFd 清空事件 |
| `doPendingFunctors` | `void doPendingFunctors()` (private) | swap 后执行，避免长时间持锁 |
| `updateChannel/removeChannel/hasChannel` | | 转发给 poller_ |
| `isInLoopThread` | `bool isInLoopThread() const` | `threadId_ == CurrentThread::tid()` |

**`loop` 实现思路**
```cpp
void EventLoop::loop() {
    looping_ = true;
    quit_ = false;
    while (!quit_) {
        activeChannels_.clear();
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel* ch : activeChannels_) ch->handleEvent(pollReturnTime_);
        doPendingFunctors();
    }
    looping_ = false;
}
```

**`doPendingFunctors` 实现要点**
```cpp
void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        functors.swap(pendingFunctors_);
    }
    for (auto& f : functors) f();
    callingPendingFunctors_ = false;
}
```

**`queueInLoop` 中的 wakeup 条件**
```cpp
if (!isInLoopThread() || callingPendingFunctors_) wakeup();
```
> 必须包含 `callingPendingFunctors_` 这一项，否则新加入的 functor 会等到下一次 poll 超时才被执行。

---

### 3.3 thread：线程与线程池

#### 3.3.1 `Thread`
**职责**：`std::thread` 的轻量封装，提供 tid 和 name。

| 字段 | 类型 |
|---|---|
| `started_` | `bool` |
| `joined_` | `bool` |
| `thread_` | `std::shared_ptr<std::thread>` |
| `tid_` | `pid_t` |
| `func_` | `ThreadFunc = std::function<void()>` |
| `name_` | `std::string` |
| 静态 | `static std::atomic_int numCreated_` |

| 方法 | 签名 |
|---|---|
| 构造 | `Thread(ThreadFunc, const std::string& name = "")` |
| 析构 | `~Thread()`（若 started 且未 join，调 thread_->detach()） |
| `start` | `void start()` |
| `join` | `void join()` |
| `started/tid/name/numCreated` | getter |

**`start` 实现要点**
- 使用 `std::shared_ptr<std::thread>` 持有；
- 在新线程内 `tid_ = CurrentThread::tid()` 后通过 `sem_t` 或 `条件变量` 通知主线程
- 主线程等待 tid 设置完成才返回，否则 `tid()` 可能拿到 0

---

#### 3.3.2 `EventLoopThread`
**职责**：开一个新线程，并在里面跑一个 EventLoop。

| 字段 | 类型 |
|---|---|
| `loop_` | `EventLoop*` |
| `exiting_` | `bool` |
| `thread_` | `Thread` |
| `mutex_` | `std::mutex` |
| `cond_` | `std::condition_variable` |
| `callback_` | `ThreadInitCallback = std::function<void(EventLoop*)>` |

| 方法 | 签名 |
|---|---|
| 构造 | `EventLoopThread(const ThreadInitCallback& cb = {}, const std::string& name = "")` |
| 析构 | 调 loop_->quit() 并 join |
| `startLoop` | `EventLoop* startLoop()` |
| 私有 | `void threadFunc()` |

**`threadFunc` 实现**
```cpp
void EventLoopThread::threadFunc() {
    EventLoop loop;             // 栈上构造
    if (callback_) callback_(&loop);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    loop.loop();                // 阻塞在此
    std::lock_guard<std::mutex> lk(mutex_);
    loop_ = nullptr;
}
```

**`startLoop` 实现**
```cpp
EventLoop* EventLoopThread::startLoop() {
    thread_.start();
    std::unique_lock<std::mutex> lk(mutex_);
    cond_.wait(lk, [this]{ return loop_ != nullptr; });
    return loop_;
}
```

---

#### 3.3.3 `EventLoopThreadPool`
**职责**：管理多个 IO 线程，提供 round-robin 分发。

| 字段 | 类型 |
|---|---|
| `baseLoop_` | `EventLoop*` (用户传入的 mainLoop) |
| `name_` | `std::string` |
| `started_` | `bool` |
| `numThreads_` | `int` |
| `next_` | `int` |
| `threads_` | `std::vector<std::unique_ptr<EventLoopThread>>` |
| `loops_` | `std::vector<EventLoop*>` |

| 方法 | 签名 |
|---|---|
| 构造 | `EventLoopThreadPool(EventLoop* baseLoop, const std::string& name)` |
| `setThreadNum` | `void setThreadNum(int n)` |
| `start` | `void start(const ThreadInitCallback& cb = {})` |
| `getNextLoop` | `EventLoop* getNextLoop()` |
| `getAllLoops` | `std::vector<EventLoop*> getAllLoops()` |

**`getNextLoop` 实现要点**
```cpp
EventLoop* loop = baseLoop_;
if (!loops_.empty()) {
    loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
}
return loop;
```

---

### 3.4 socket：地址与套接字

#### 3.4.1 `InetAddress`
**职责**：`sockaddr_in` 的封装。

| 字段 | 类型 |
|---|---|
| `addr_` | `sockaddr_in` |

| 方法 | 签名 |
|---|---|
| 构造 | `InetAddress(uint16_t port = 0, std::string ip = "127.0.0.1")` |
| 构造 | `explicit InetAddress(const sockaddr_in& addr)` |
| `toIp` | `std::string toIp() const` |
| `toPort` | `uint16_t toPort() const` |
| `toIpPort` | `std::string toIpPort() const` |
| `getSockAddr` | `const sockaddr_in* getSockAddr() const` |
| `setSockAddr` | `void setSockAddr(const sockaddr_in& addr)` |

**实现要点**
- 构造时 `addr_.sin_family = AF_INET`；
- `inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr)`；
- `addr_.sin_port = htons(port)`。

---

#### 3.4.2 `Socket`（RAII）
**职责**：fd 的 RAII 持有 + 常用 setsockopt。

| 字段 | 类型 |
|---|---|
| `sockfd_` | `const int` |

| 方法 | 签名 |
|---|---|
| 构造 | `explicit Socket(int sockfd)` |
| 析构 | `~Socket()` → `::close(sockfd_)` |
| `fd` | `int fd() const` |
| `bindAddress` | `void bindAddress(const InetAddress&)` |
| `listen` | `void listen()` |
| `accept` | `int accept(InetAddress* peeraddr)` |
| `shutdownWrite` | `void shutdownWrite()` |
| 4 个 setOpt | `setTcpNoDelay/setReuseAddr/setReusePort/setKeepAlive(bool)` |

**`accept` 实现要点**
- 用 `::accept4(fd, ..., SOCK_NONBLOCK | SOCK_CLOEXEC)`，一次性设置非阻塞 + close-on-exec。

---

#### 3.4.3 `Acceptor`
**职责**：跑在 mainLoop，监听端口、接受连接。

**依赖**：`EventLoop`, `Socket`, `Channel`, `InetAddress`

| 字段 | 类型 |
|---|---|
| `loop_` | `EventLoop*` |
| `acceptSocket_` | `Socket` |
| `acceptChannel_` | `Channel` |
| `newConnectionCallback_` | `std::function<void(int sockfd, const InetAddress&)>` |
| `listening_` | `bool` |

| 方法 | 签名 |
|---|---|
| 构造 | `Acceptor(EventLoop*, const InetAddress& listenAddr, bool reusePort)` |
| 析构 | disableAll + remove channel |
| `setNewConnectionCallback` | |
| `listening` | `bool listening() const` |
| `listen` | `void listen()` |
| 私有 | `void handleRead()` |

**构造实现要点**
1. `int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)`
2. `acceptSocket_(sockfd)` 接管
3. `setReuseAddr(true)`, `setReusePort(reusePort)`
4. `bindAddress(listenAddr)`
5. `acceptChannel_.setReadCallback(handleRead)`

**`handleRead` 实现要点**
```cpp
InetAddress peerAddr;
int connfd = acceptSocket_.accept(&peerAddr);
if (connfd >= 0) {
    if (newConnectionCallback_) newConnectionCallback_(connfd, peerAddr);
    else ::close(connfd);
}
```

---

### 3.5 buffer：应用层缓冲区

#### 3.5.1 `Buffer`
**职责**：每个 TcpConnection 持有一对（input/output）。

**内存模型**
```
| prependable (>=8) | readable | writable |
0                reader      writer     size
```
- `prependable` 用来在前面填消息长度（避免拷贝）
- `readable` 是有效数据
- `writable` 是空闲空间

**常量**
```cpp
static const size_t kCheapPrepend = 8;
static const size_t kInitialSize  = 1024;
```

**字段**
| 字段 | 类型 |
|---|---|
| `buffer_` | `std::vector<char>` |
| `readerIndex_` | `size_t` |
| `writerIndex_` | `size_t` |

**方法**
| 方法 | 签名 | 说明 |
|---|---|---|
| 构造 | `Buffer(size_t initSize = kInitialSize)` | size = `kCheapPrepend + initSize` |
| `readableBytes` | `size_t readableBytes() const` | `writerIndex_ - readerIndex_` |
| `writableBytes` | `size_t writableBytes() const` | `buffer_.size() - writerIndex_` |
| `prependableBytes` | `size_t prependableBytes() const` | `readerIndex_` |
| `peek` | `const char* peek() const` | `begin() + readerIndex_` |
| `retrieve` | `void retrieve(size_t len)` | 调整 readerIndex |
| `retrieveAll` | | 重置 readerIndex/writerIndex |
| `retrieveAsString` | `std::string retrieveAsString(size_t len)` | |
| `retrieveAllAsString` | | |
| `ensureWritableBytes` | `void ensureWritableBytes(size_t len)` | 不够时 makeSpace |
| `append` | `void append(const char* data, size_t len)` | |
| `beginWrite` | `char* beginWrite()` | |
| `readFd` | `ssize_t readFd(int fd, int* savedErrno)` | **核心，见下** |
| `writeFd` | `ssize_t writeFd(int fd, int* savedErrno)` | |
| 私有 `makeSpace` | `void makeSpace(size_t len)` | |

**`readFd` 实现（关键技巧）**
用 `readv` + 栈上 64KB 临时区：一次系统调用读到尽可能多数据。
```cpp
ssize_t Buffer::readFd(int fd, int* savedErrno) {
    char extrabuf[65536];
    iovec vec[2];
    size_t writable = writableBytes();
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len  = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len  = sizeof(extrabuf);
    int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)  *savedErrno = errno;
    else if (static_cast<size_t>(n) <= writable) writerIndex_ += n;
    else {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);  // 触发扩容
    }
    return n;
}
```

**`makeSpace` 实现**
```cpp
void Buffer::makeSpace(size_t len) {
    if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
        buffer_.resize(writerIndex_ + len);
    } else {
        // 把已读数据腾到最前面
        size_t readable = readableBytes();
        std::copy(begin() + readerIndex_, begin() + writerIndex_,
                  begin() + kCheapPrepend);
        readerIndex_ = kCheapPrepend;
        writerIndex_ = readerIndex_ + readable;
    }
}
```

---

### 3.6 tcp：连接与服务器

#### 3.6.1 `Callbacks.hpp`
```cpp
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

using ConnectionCallback    = std::function<void(const TcpConnectionPtr&)>;
using CloseCallback         = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;
using MessageCallback       = std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>;
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, size_t)>;
```

---

#### 3.6.2 `TcpConnection`
继承 `std::enable_shared_from_this<TcpConnection>`，**不可拷贝**。

**生命周期状态**
```cpp
enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
```
- 构造时 `kConnecting`
- `connectEstablished` 后 `kConnected`
- `shutdown` 触发 `kDisconnecting`
- `handleClose` 后 `kDisconnected`

**字段**
| 字段 | 类型 |
|---|---|
| `loop_` | `EventLoop*` (subLoop) |
| `name_` | `const std::string` |
| `state_` | `std::atomic<int>` [T] |
| `reading_` | `bool` |
| `socket_` | `std::unique_ptr<Socket>` |
| `channel_` | `std::unique_ptr<Channel>` |
| `localAddr_` | `const InetAddress` |
| `peerAddr_` | `const InetAddress` |
| `connectionCallback_` | `ConnectionCallback` |
| `messageCallback_` | `MessageCallback` |
| `writeCompleteCallback_` | `WriteCompleteCallback` |
| `highWaterMarkCallback_` | `HighWaterMarkCallback` |
| `closeCallback_` | `CloseCallback` |
| `highWaterMark_` | `size_t` (默认 64MB) |
| `inputBuffer_` | `Buffer` |
| `outputBuffer_` | `Buffer` |

**对外方法**
| 方法 | 签名 |
|---|---|
| 构造 | `TcpConnection(EventLoop*, std::string name, int sockfd, const InetAddress& local, const InetAddress& peer)` |
| 析构 | log + 默认即可 |
| `send` | `void send(const std::string& msg)` |
| `shutdown` | `void shutdown()` |
| `setXxxCallback` | 5 个 setter |
| `connectEstablished` | `void connectEstablished()` |
| `connectDestroyed` | `void connectDestroyed()` |
| getter | name/loop/peer/local/connected |

**私有方法（Channel 回调）**
- `void handleRead(Timestamp)`
- `void handleWrite()`
- `void handleClose()`
- `void handleError()`
- `void sendInLoop(const void* data, size_t len)`
- `void shutdownInLoop()`

**`handleRead` 实现思路**
```cpp
int savedErrno = 0;
ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
if (n > 0) {
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
} else if (n == 0) {
    handleClose();
} else {
    errno = savedErrno;
    handleError();
}
```

**`sendInLoop` 实现思路**（重点）
```cpp
// 1. 若 outputBuffer 为空且 channel 未 enableWriting，先尝试直接 write 到 fd
// 2. 剩余数据 append 到 outputBuffer，并 enableWriting
// 3. 若 outputBuffer 长度 >= highWaterMark，回调 highWaterMarkCallback
// 4. 注意 EWOULDBLOCK / EPIPE / ECONNRESET 等 errno
```

**`handleWrite` 实现思路**
```cpp
// 1. 把 outputBuffer 中的数据写到 fd
// 2. 若全部写完：disableWriting；若 writeCompleteCallback 存在，queueInLoop 调用
// 3. 若状态是 kDisconnecting，调 shutdownInLoop()
```

**`handleClose` 实现思路**
```cpp
setState(kDisconnected);
channel_->disableAll();
TcpConnectionPtr guardThis = shared_from_this();
connectionCallback_(guardThis);  // 通知上层
closeCallback_(guardThis);       // 通知 TcpServer 移除
```

**注意事项**
- `connectEstablished` 中调 `channel_->tie(shared_from_this())`！
- 整个对象生命周期由 `TcpServer::connections_` 中的 shared_ptr 维持，**移除时要 `runInLoop(connectDestroyed)` 配合 `bind` 延长生命**。

---

#### 3.6.3 `TcpServer`
**对外接口（用户使用的入口）**

| 字段 | 类型 |
|---|---|
| `loop_` | `EventLoop*` (mainLoop，用户传入，不能为空) |
| `ipPort_` | `const std::string` |
| `name_` | `const std::string` |
| `acceptor_` | `std::unique_ptr<Acceptor>` |
| `threadPool_` | `std::shared_ptr<EventLoopThreadPool>` |
| `connectionCallback_` | |
| `messageCallback_` | |
| `writeCompleteCallback_` | |
| `threadInitCallback_` | |
| `started_` | `std::atomic_int` |
| `nextConnId_` | `int` |
| `connections_` | `std::unordered_map<std::string, TcpConnectionPtr>` |

| 方法 | 签名 |
|---|---|
| 枚举 | `enum Option { kNoReusePort, kReusePort }` |
| 构造 | `TcpServer(EventLoop*, const InetAddress&, std::string name, Option = kNoReusePort)` |
| 析构 | 遍历 connections_ 强制销毁 |
| `setThreadInitCallback` | |
| `setConnectionCallback` | |
| `setMessageCallback` | |
| `setWriteCompleteCallback` | |
| `setThreadNum` | `void setThreadNum(int n)` |
| `start` | `void start()` |
| 私有 | `void newConnection(int sockfd, const InetAddress& peerAddr)` |
| 私有 | `void removeConnection(const TcpConnectionPtr&)` |
| 私有 | `void removeConnectionInLoop(const TcpConnectionPtr&)` |

**`newConnection` 实现思路（Acceptor 触发）**
```cpp
// 在 mainLoop 线程中
EventLoop* ioLoop = threadPool_->getNextLoop();
char buf[64];
snprintf(buf, sizeof(buf), "-%s#%d", ipPort_.c_str(), nextConnId_++);
std::string connName = name_ + buf;

// 取本地地址
sockaddr_in local;
socklen_t addrlen = sizeof(local);
::getsockname(sockfd, (sockaddr*)&local, &addrlen);
InetAddress localAddr(local);

auto conn = std::make_shared<TcpConnection>(ioLoop, connName, sockfd, localAddr, peerAddr);
connections_[connName] = conn;

// 转发用户回调
conn->setConnectionCallback(connectionCallback_);
conn->setMessageCallback(messageCallback_);
conn->setWriteCompleteCallback(writeCompleteCallback_);
conn->setCloseCallback([this](const TcpConnectionPtr& c) {
    removeConnection(c);  // 注意线程：可能在 ioLoop
});

ioLoop->runInLoop([conn]{ conn->connectEstablished(); });
```

**`removeConnection` 实现思路**
```cpp
void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
    loop_->runInLoop([this, conn]{ removeConnectionInLoop(conn); });
}
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
    connections_.erase(conn->name());
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop([conn]{ conn->connectDestroyed(); });
    // queueInLoop + bind 是关键：把 shared_ptr 拷贝进 lambda，延长生命到 connectDestroyed 执行完
}
```

---

## 4. 实现里程碑

把整个项目切成 6 个里程碑，每个里程碑结束都应该能**编译通过 + 跑通验证程序**。

### 🟢 M1. 基础工具（1 天）
**实现**：`noncopyable`, `Timestamp`, `Logger` + 宏, `CurrentThread`
**验证**：main 里 `LOG_INFO("hello %s", "world");`，输出含时间戳和级别。

### 🟢 M2. 单线程 Reactor（2~3 天）
**实现**：`Channel`, `Poller`, `EPollPoller`, `EventLoop`, `InetAddress`, `Socket`, `Acceptor`
**验证**：写一个 mini 服务器，accept 后立即关闭。验证 epoll 流程畅通。
```cpp
EventLoop loop;
InetAddress addr(8080);
Acceptor acceptor(&loop, addr, false);
acceptor.setNewConnectionCallback([](int fd, const InetAddress& peer){
    LOG_INFO("new conn from %s", peer.toIpPort().c_str());
    ::close(fd);
});
acceptor.listen();
loop.loop();
```

### 🟢 M3. Buffer + Echo 雏形（1~2 天）
**实现**：`Buffer`, 简陋版 `TcpConnection`（不完整，但能读能写）
**验证**：手写一个 echo 程序，accept 后直接读写。

### 🟢 M4. 线程支持（2 天）
**实现**：`Thread`, `EventLoopThread`, `EventLoopThreadPool`
**验证**：写一个 main，启动 4 个 subLoop，每个 loop 打印自己的 tid。

### 🟢 M5. 完整 TcpConnection + TcpServer（2~3 天）
**实现**：`Callbacks.hpp`, 完整 `TcpConnection`, `TcpServer`
**验证**：`examples/echo_server.cpp`
```cpp
class EchoServer {
  EventLoop* loop_;
  TcpServer server_;
public:
  EchoServer(EventLoop* loop, const InetAddress& addr)
    : loop_(loop), server_(loop, addr, "EchoServer") {
    server_.setConnectionCallback([](const TcpConnectionPtr& c){
        LOG_INFO("%s %s", c->peerAddress().toIpPort().c_str(),
                 c->connected() ? "UP" : "DOWN");
    });
    server_.setMessageCallback([](const TcpConnectionPtr& c, Buffer* buf, Timestamp){
        c->send(buf->retrieveAllAsString());
    });
    server_.setThreadNum(3);
  }
  void start() { server_.start(); }
};
```
用 `nc 127.0.0.1 8080` 或 `telnet` 测试。

### 🟢 M6. 健壮性与扩展（按需）
- 压力测试（连接数、QPS）
- 增加 HTTP server 示例
- 增加客户端 `TcpClient`

---

## 5. 测试与验证方案

### 5.1 单元测试
- `Timestamp::now().toString()` 输出格式正确
- `Buffer` 各种 append/retrieve/readFd 后状态正确
- `InetAddress` 解析与回填

### 5.2 集成测试
| 阶段 | 测试方法 |
|---|---|
| M2 | `telnet 127.0.0.1 8080` 看是否 accept 后立即断开 |
| M3 | 写 echo，`echo hello | nc 127.0.0.1 8080` |
| M4 | 用 LOG_INFO 打印 tid，确认每个 loop 独立 |
| M5 | 同 M3，但开 3~4 个客户端并发，观察分发到不同 subLoop |

### 5.3 压力测试
- 用 `wrk` / `ab` 或自写 client。
- 监控点：CPU 占用、连接建立速率、吞吐。

---

## 6. 关键设计要点与陷阱清单

### 6.1 跨线程安全
- **所有跨线程调用走 `runInLoop` / `queueInLoop`**。不要直接操作其他 loop 的数据结构。
- `Channel` / `Poller` 操作只能在所属 EventLoop 的线程中执行。

### 6.2 生命周期
- **Channel 不拥有 fd**。fd 由 Socket / Acceptor / wakeupFd 拥有。
- **TcpConnection 用 shared_ptr 管理**：被 `TcpServer::connections_` 和 `Channel::tie_` 共同持有。
- 移除连接时，**必须 `bind` 延长 shared_ptr 生命周期到 `connectDestroyed` 执行完**。

### 6.3 epoll 相关
- 用 **LT 模式**（默认），更容错。
- 必须处理 `EPOLLHUP` / `EPOLLERR`，否则会 busy loop。
- `epoll_event.data.ptr` 存 `Channel*`。

### 6.4 wakeup 机制
- 用 `eventfd()` 比 pipe 轻。
- `read` 时务必读 8 字节（uint64_t），否则永远可读。

### 6.5 Buffer
- `readFd` 使用 `readv` + 栈 buf，一次 syscall 读尽。
- `append` 时按需 `makeSpace`：先 `std::copy` 腾挪，不够再 resize。

### 6.6 EMFILE 问题（高级）
- 当 fd 用尽时 `accept` 会失败，且 epoll 会持续触发。
- 经典做法：预先打开一个 `idleFd_ = open("/dev/null")`，遇到 EMFILE 时关掉 idleFd_、accept 再 close，重新 open idleFd_。
- 第一版可不实现，但要在 LOG_ERROR 留 TODO。

### 6.7 highWaterMark
- 用户写得太快、对端读得太慢，outputBuffer 会无限增长。
- `sendInLoop` 中检测 outputBuffer 大小，超过阈值就回调 `highWaterMarkCallback`，让用户决定是否暂停发送。

### 6.8 重入
- `doPendingFunctors` 中要先 swap 出来再执行，否则用户在 functor 里调 `queueInLoop` 会导致迭代失败 / 死锁。

---

## 7. 参考 API 速查

### 7.1 epoll
```cpp
int epoll_create1(int flags);                    // EPOLL_CLOEXEC
int epoll_ctl(int epfd, int op, int fd, epoll_event* ev);  // ADD/MOD/DEL
int epoll_wait(int epfd, epoll_event* events, int maxevents, int timeout_ms);
```

### 7.2 eventfd
```cpp
int eventfd(unsigned int initval, int flags);    // EFD_NONBLOCK | EFD_CLOEXEC
// read: 读到 uint64_t，eventfd 内部计数清零
// write: 写 uint64_t，计数累加
```

### 7.3 socket
```cpp
int socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
int accept4(int sockfd, sockaddr*, socklen_t*, SOCK_NONBLOCK | SOCK_CLOEXEC);
int bind / listen / shutdown;
int setsockopt(SO_REUSEADDR / SO_REUSEPORT / SO_KEEPALIVE / TCP_NODELAY);
```

### 7.4 readv / writev
```cpp
struct iovec { void* iov_base; size_t iov_len; };
ssize_t readv(int fd, const struct iovec* iov, int iovcnt);
ssize_t writev(int fd, const struct iovec* iov, int iovcnt);
```

### 7.5 错误处理常见 errno
- `EAGAIN / EWOULDBLOCK`：非阻塞下没有数据，重试
- `EINTR`：被信号中断，重试
- `ECONNRESET`：对端 RST
- `EPIPE`：对端已关闭，继续写产生 SIGPIPE（建议忽略 SIGPIPE）
- `EMFILE`：fd 用尽

---

## 附：推荐学习顺序

1. **先读懂本文档**，画出三大件（Channel/Poller/EventLoop）的关系图。
2. **写 M1 + M2**，到这里你已经能看到 epoll 跑起来了。
3. **回头看 `handleEvent` 的事件分发**，理解 LT 模式下的事件处理顺序。
4. **写 M3 的 Buffer**，亲手实现 readv 那段，理解为什么要双 buffer。
5. **写 M4 的线程支持**，重点理解条件变量等待 tid 的握手。
6. **写 M5 的 TcpConnection**，重点理解 `shared_from_this` + `tie` 怎么解决生命周期问题。
7. **跑通 echo server 后**，去读 muduo 源码对照。

祝顺利！动手开始吧。
